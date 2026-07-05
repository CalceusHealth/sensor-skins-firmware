/*===========================================
//
// main.c
// Written by Alex Gilmour
// Copyright (c) 2024, CarbonCircuits
// All rights reserved.
//
//=========================================*/

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "configure_firmware.h"
#include "gpio.h"
#include "app_timer.h"
#include "nrf_delay.h"
#include "system.h"
#include "flash.h"
#include "adc.h"
#include "battery.h"
#include "messaging.h"
#include "ble_reid.h"
#include "lmt01.h"
#include "i2c.h"
#include "lsm6dsm.h"
#include "measure.h"

#include "nordic_common.h"
#include "nrf.h"
#include "app_util_platform.h"

#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

static void log_init(void);
static void main_init(void);
static void main_deinit(void);
static uint16_t count_fsr_delta_score(const reid_ble_packet_t* current, const reid_ble_packet_t* previous);
static uint16_t count_cap_delta_score(const reid_ble_packet_t* current, const reid_ble_packet_t* previous);
static uint8_t sensor_activity_detected(const reid_ble_packet_t* current, const reid_ble_packet_t* previous, uint8_t has_previous);
static void flush_summary_if_pending(int32_t* summary_counter);
static void stream_reset_pending(void);
static void stream_send_measurement(const reid_ble_packet_t* data);
static void stream_send_temp(const reid_ble_packet_t* data);
static void stream_send_battery(const reid_ble_packet_t* data);
static void stream_flush_pending(uint8_t force);
static void update_charging_state_history(void);
static uint8_t battery_sleep_protection_required(void);
static uint8_t ble_activity_detected(void);
static void enter_device_sleep(uint64_t* timer, int32_t* summary_counter, uint8_t recovery_sleep);

volatile reid_ble_packet_t ble_data = {0};
volatile reid_ble_summary_packet_t summary_data = {0};
volatile uint8_t battery_query_pause = 0;
// Runtime main-loop period (ms) = stream sample rate. Default from the compile
// constant; settable live via the ;CF <hz> command (SEN-57).
volatile uint16_t main_loop_period_ms = MAIN_LOOP_TIME_MS;
volatile uint8_t bench_keepawake = 0;
volatile uint8_t session_active = 0;
volatile uint64_t last_ble_activity_ms = 0;

static uint8_t charging_state_history[CHARGING_STATE_HISTORY_SAMPLES] = {0};
static uint8_t charging_state_history_index = 0;
static uint8_t charging_state_history_fill = 0;
static uint8_t charging_non_c_streak = 0;
static uint8_t charging_confirmed = 0;
static uint8_t low_battery_sleep_latched = 0;
static reid_ble_packet_t previous_ble_data = {0};
static uint8_t has_previous_ble_data = 0;
// SEN-96: hold timestamps for the motion/load-gated session latch. 0 = never.
static uint64_t last_disconnect_ms = 0;
static uint64_t last_motion_ms = 0;
static uint64_t last_worn_load_ms = 0;
// SEN-98: IDLE_CONNECTED — connected but idle: internal cadence drops to
// IDLE_CONNECTED_PERIOD_MS and the IMU goes to wake-on-motion. The user's ;CF
// rate (main_loop_period_ms) is untouched and resumes on exit.
static uint8_t idle_connected = 0;

#ifdef STREAM_PROTOCOL_BINARY_V2
typedef struct stream_binary_v2_state_t {
	reid_ble_stream_row_v2_t rows[STREAM_BINARY_V2_MAX_ROWS];
	uint64_t row_time_ms[STREAM_BINARY_V2_MAX_ROWS];
	uint8_t row_count;
	uint16_t sequence;
} stream_binary_v2_state_t;

static stream_binary_v2_state_t stream_binary_v2_state = {0};
#endif

static void log_init(void)
{
    ret_code_t err_code = NRF_LOG_INIT(NULL);
    // APP_ERROR_CHECK(err_code); // is always NRF_SUCCESS
    NRF_LOG_DEFAULT_BACKENDS_INIT();
}

static void main_init(void)
{
    // Initialize.
	#ifdef ENABLE_CODE_PROTECT // Set Level 1 code protection
	system_set_code_protection();
	#endif // ENABLE_CODE_PROTECT
	system_init();
	gpio_init();
	adc_init();
	battery_init();
	flash_init();
	#ifdef ENABLE_DEBUG
	log_init();
	#endif
	lmt01_init();
	// IMU bring-up (SEN-54): I2C + LSM6DSM. i2c_init() is otherwise only invoked
	// by the i2c.c bus-recovery path, so it must be started here before any
	// LSM6DSM access. Streaming the IMU into the frame is deferred (SEN-68).
	i2c_init();
	lsm6dsm_init();
	#ifdef ENABLE_DEBUG
	NRF_LOG_INFO("LSM6DSM WHO_AM_I=0x%02X (expect 0x%02X)", lsm6dsm_whoami(), LSM6DSM_WHO_AM_I_VALUE);
	NRF_LOG_FLUSH();
	#endif
	msg_init();
    //ble_reid_init(); // inside msg_init()
}

static uint16_t count_fsr_delta_score(const reid_ble_packet_t* current, const reid_ble_packet_t* previous)
{
	uint16_t score = 0;
	const uint16_t* current_fsr = &current->fsr1;
	const uint16_t* previous_fsr = &previous->fsr1;

	for (uint8_t i = 0; i < 19; ++i) {
		uint16_t delta = (current_fsr[i] > previous_fsr[i]) ? (current_fsr[i] - previous_fsr[i]) : (previous_fsr[i] - current_fsr[i]);
		if (delta >= FSR_DELTA_THRESHOLD) ++score;
	}

	return score;
}

static uint16_t count_cap_delta_score(const reid_ble_packet_t* current, const reid_ble_packet_t* previous)
{
	uint16_t score = 0;
	const uint16_t* current_cap = &current->cap1s;
	const uint16_t* previous_cap = &previous->cap1s;

	for (uint8_t i = 0; i < 12; ++i) {
		uint16_t delta = (current_cap[i] > previous_cap[i]) ? (current_cap[i] - previous_cap[i]) : (previous_cap[i] - current_cap[i]);
		if (delta >= CAP_DELTA_THRESHOLD) ++score;
	}

	return score;
}

static uint8_t sensor_activity_detected(const reid_ble_packet_t* current, const reid_ble_packet_t* previous, uint8_t has_previous)
{
	uint16_t fsr_delta_score;
	uint16_t cap_delta_score;

	if (!has_previous) return 1;

	fsr_delta_score = count_fsr_delta_score(current, previous);
	cap_delta_score = count_cap_delta_score(current, previous);

	return (fsr_delta_score >= FSR_DELTA_WAKE_MIN) || (cap_delta_score >= CAP_DELTA_WAKE_MIN);
}

static void flush_summary_if_pending(int32_t* summary_counter)
{
	if (*summary_counter > 0) {
#ifdef ENABLE_FLASH_SUMMARY
		flash_write_record((reid_ble_summary_packet_t*) &summary_data);
#endif
		measure_reset_summary((reid_ble_summary_packet_t*) &summary_data);
		*summary_counter = 0;
	}
}

static void stream_reset_pending(void)
{
#ifdef STREAM_PROTOCOL_BINARY_V2
	memset((void*) &stream_binary_v2_state, 0, sizeof(stream_binary_v2_state));
#endif
}

static void stream_send_measurement(const reid_ble_packet_t* data)
{
#ifdef STREAM_PROTOCOL_ASCII_V1
	#define TX_STR_LEN_MAX 1024
	uint8_t tx_string[TX_STR_LEN_MAX] = {0};
	uint16_t tx_string_len = 0;
	tx_string_len = snprintf(tx_string, TX_STR_LEN_MAX, "%llu,%u,%u,%u,%u,%u,",\
														(unsigned long long)data->time_ms,\
														data->fsr1*4,\
														data->fsr2*4,\
														data->fsr3*4,\
														data->fsr4*4,\
														data->fsr5*4);
	tx_string_len += snprintf(tx_string+tx_string_len, TX_STR_LEN_MAX-tx_string_len, "%u,%u,%u,%u,%u,",\
														data->fsr6*4,\
														data->fsr7*4,\
														data->fsr8*4,\
														data->fsr9*4,\
														data->fsr10*4);
	tx_string_len += snprintf(tx_string+tx_string_len, TX_STR_LEN_MAX-tx_string_len, "%u,%u,%u,%u,%u,",\
														data->fsr11*4,\
														data->fsr12*4,\
														data->fsr13*4,\
														data->fsr14*4,\
														data->fsr15*4);
	tx_string_len += snprintf(tx_string+tx_string_len, TX_STR_LEN_MAX-tx_string_len, "%u,%u,%u,%u,",\
														data->fsr16*4,\
														data->fsr17*4,\
														data->fsr18*4,\
														data->fsr19*4);
	tx_string_len += snprintf(tx_string+tx_string_len, TX_STR_LEN_MAX-tx_string_len, "%d,%d,%d,%d,%d,",\
														data->temp1,\
														data->temp2,\
														data->temp3,\
														data->temp4,\
														data->temp5);
	tx_string_len += snprintf(tx_string+tx_string_len, TX_STR_LEN_MAX-tx_string_len, "%u,%u,%u,%u,",\
														data->cap1s*3,\
														data->cap1n*3,\
														data->cap2s*3,\
														data->cap2n*3);
	tx_string_len += snprintf(tx_string+tx_string_len, TX_STR_LEN_MAX-tx_string_len, "%u,%u,%u,%u,",\
														data->cap3s*3,\
														data->cap3n*3,\
														data->cap4s*3,\
														data->cap4n*3);
	tx_string_len += snprintf(tx_string+tx_string_len, TX_STR_LEN_MAX-tx_string_len, "%u,%u,%u,%u\r\n",\
														data->cap5s*3,\
														data->cap5n*3,\
														data->cap6s*3,\
														data->cap6n*3);
	ble_reid_tx(tx_string, tx_string_len);
#endif

#ifdef STREAM_PROTOCOL_BINARY_V2
	if (!ble_is_connected()) {
		stream_reset_pending();
		return;
	}

	if (stream_binary_v2_state.row_count >= STREAM_BINARY_V2_MAX_ROWS) {
		stream_flush_pending(1);
	}

	reid_ble_stream_row_v2_t* row = &stream_binary_v2_state.rows[stream_binary_v2_state.row_count];
	memset((void*) row, 0, sizeof(*row));

	row->fsr[0] = data->fsr1 * 4;
	row->fsr[1] = data->fsr2 * 4;
	row->fsr[2] = data->fsr3 * 4;
	row->fsr[3] = data->fsr4 * 4;
	row->fsr[4] = data->fsr5 * 4;
	row->fsr[5] = data->fsr6 * 4;
	row->fsr[6] = data->fsr7 * 4;
	row->fsr[7] = data->fsr8 * 4;
	row->fsr[8] = data->fsr9 * 4;
	row->fsr[9] = data->fsr10 * 4;
	row->fsr[10] = data->fsr11 * 4;
	row->fsr[11] = data->fsr12 * 4;
	row->fsr[12] = data->fsr13 * 4;
	row->fsr[13] = data->fsr14 * 4;
	row->fsr[14] = data->fsr15 * 4;
	row->fsr[15] = data->fsr16 * 4;
	row->fsr[16] = data->fsr17 * 4;
	row->fsr[17] = data->fsr18 * 4;
	row->fsr[18] = data->fsr19 * 4;

	row->cap[0] = data->cap1s * 3;
	row->cap[1] = data->cap1n * 3;
	row->cap[2] = data->cap2s * 3;
	row->cap[3] = data->cap2n * 3;
	row->cap[4] = data->cap3s * 3;
	row->cap[5] = data->cap3n * 3;
	row->cap[6] = data->cap4s * 3;
	row->cap[7] = data->cap4n * 3;
	row->cap[8] = data->cap5s * 3;
	row->cap[9] = data->cap5n * 3;
	row->cap[10] = data->cap6s * 3;
	row->cap[11] = data->cap6n * 3;

	// SEN-68 (v3): IMU accel/gyro, refreshed by lsm6dsm_update() in measure_sensors.
	row->acc[0] = lsm6dsm_read_ax();
	row->acc[1] = lsm6dsm_read_ay();
	row->acc[2] = lsm6dsm_read_az();
	row->gyro[0] = lsm6dsm_read_gx();
	row->gyro[1] = lsm6dsm_read_gy();
	row->gyro[2] = lsm6dsm_read_gz();

	stream_binary_v2_state.row_time_ms[stream_binary_v2_state.row_count] = data->time_ms;
	++stream_binary_v2_state.row_count;
	stream_flush_pending(0);
#endif
}

static void stream_flush_pending(uint8_t force)
{
#ifdef STREAM_PROTOCOL_BINARY_V2
	if (stream_binary_v2_state.row_count == 0) return;
	if (!ble_is_connected()) {
		stream_reset_pending();
		return;
	}

	const uint16_t tx_max_len = ble_reid_max_tx_len();
	const uint16_t header_len = sizeof(reid_ble_stream_frame_v2_header_t);
	const uint16_t row_len = sizeof(reid_ble_stream_row_v2_t);
	if (tx_max_len <= header_len || (tx_max_len - header_len) < row_len) return;

	uint8_t max_rows = (uint8_t)((tx_max_len - header_len) / row_len);
	if (max_rows > STREAM_BINARY_V2_MAX_ROWS) max_rows = STREAM_BINARY_V2_MAX_ROWS;
	if (max_rows == 0) return;

	const uint64_t first_time_ms = stream_binary_v2_state.row_time_ms[0];
	const uint64_t last_time_ms = stream_binary_v2_state.row_time_ms[stream_binary_v2_state.row_count - 1];
	if (!force &&
		stream_binary_v2_state.row_count < max_rows &&
		(last_time_ms - first_time_ms) < STREAM_BINARY_V2_MAX_LATENCY_MS) {
		return;
	}

	const uint8_t rows_to_send = (stream_binary_v2_state.row_count < max_rows) ? stream_binary_v2_state.row_count : max_rows;
	uint8_t frame_buffer[sizeof(reid_ble_stream_frame_v2_header_t) + (STREAM_BINARY_V2_MAX_ROWS * sizeof(reid_ble_stream_row_v2_t))] = {0};
	const uint16_t frame_len = header_len + (rows_to_send * row_len);
	reid_ble_stream_frame_v2_header_t header = {
		.magic = REID_STREAM_BINARY_V2_MAGIC,
		.version = REID_STREAM_BINARY_V2_VERSION,
		.frame_type = REID_STREAM_BINARY_V2_FRAME_SENSOR_ROWS,
		.flags = REID_STREAM_BINARY_V2_FLAG_FSR_X4 | REID_STREAM_BINARY_V2_FLAG_CAP_X3 | REID_STREAM_BINARY_V2_FLAG_IMU,
		.row_count = rows_to_send,
		.sequence = stream_binary_v2_state.sequence++,
		.base_time_ms = first_time_ms,
	};

	memcpy(frame_buffer, &header, sizeof(header));
	for (uint8_t i = 0; i < rows_to_send; ++i) {
		reid_ble_stream_row_v2_t row = stream_binary_v2_state.rows[i];
		row.delta_time_ms = (uint16_t)(stream_binary_v2_state.row_time_ms[i] - first_time_ms);
		memcpy(frame_buffer + header_len + (i * row_len), &row, row_len);
	}
	ble_reid_tx_stream(frame_buffer, frame_len); // SEN-59: non-blocking, don't stall the measurement loop

	if (rows_to_send < stream_binary_v2_state.row_count) {
		const uint8_t remaining = stream_binary_v2_state.row_count - rows_to_send;
		memmove((void*) &stream_binary_v2_state.rows[0], (const void*) &stream_binary_v2_state.rows[rows_to_send], remaining * sizeof(stream_binary_v2_state.rows[0]));
		memmove((void*) &stream_binary_v2_state.row_time_ms[0], (const void*) &stream_binary_v2_state.row_time_ms[rows_to_send], remaining * sizeof(stream_binary_v2_state.row_time_ms[0]));
		stream_binary_v2_state.row_count = remaining;
	} else {
		stream_binary_v2_state.row_count = 0;
	}
#else
	(void) force;
#endif
}

// SEN-68 (v3): temp moved out of the sensor row into its own low-rate frame
// (frame_type 2). Sent ~1 Hz; temp only changes every 60 s/sensor so this is
// cheap (26 B) and keeps the sensor row at 76 B / 3-per-frame. Non-blocking TX.
static void stream_send_temp(const reid_ble_packet_t* data)
{
#ifdef STREAM_PROTOCOL_BINARY_V2
	if (!ble_is_connected()) return;
	static uint16_t temp_sequence = 0;
	uint8_t buf[sizeof(reid_ble_stream_frame_v2_header_t) + sizeof(reid_ble_stream_temp_v3_t)] = {0};
	reid_ble_stream_frame_v2_header_t header = {
		.magic = REID_STREAM_BINARY_V2_MAGIC,
		.version = REID_STREAM_BINARY_V2_VERSION,
		.frame_type = REID_STREAM_BINARY_V2_FRAME_TEMP,
		.flags = 0,
		.row_count = 1,
		.sequence = temp_sequence++,
		.base_time_ms = data->time_ms,
	};
	reid_ble_stream_temp_v3_t temp = {
		.temp = { data->temp1, data->temp2, data->temp3, data->temp4, data->temp5 },
	};
	memcpy(buf, &header, sizeof(header));
	memcpy(buf + sizeof(header), &temp, sizeof(temp));
	ble_reid_tx_stream(buf, sizeof(buf));
#else
	(void) data;
#endif
}

// Battery frame (frame_type 3, ~1 Hz). Streams the AVERAGED vbat so in-session
// drain / battery-life metrics have a real time series (previously vbat was
// frozen per session because QB can't be polled during native streaming).
static void stream_send_battery(const reid_ble_packet_t* data)
{
#ifdef STREAM_PROTOCOL_BINARY_V2
	if (!ble_is_connected()) return;
	static uint16_t batt_sequence = 0;
	uint8_t buf[sizeof(reid_ble_stream_frame_v2_header_t) + sizeof(reid_ble_stream_battery_v3_t)] = {0};
	reid_ble_stream_frame_v2_header_t header = {
		.magic = REID_STREAM_BINARY_V2_MAGIC,
		.version = REID_STREAM_BINARY_V2_VERSION,
		.frame_type = REID_STREAM_BINARY_V2_FRAME_BATTERY,
		.flags = 0,
		.row_count = 1,
		.sequence = batt_sequence++,
		.base_time_ms = data->time_ms,
	};
	reid_ble_stream_battery_v3_t batt = {
		.vbat_mv = battery_pack_voltage_mv(),
		.vbat_raw = (int16_t) battery_pack_voltage_raw(),
		.pct = battery_pack_charge(),
		.state = (uint8_t) battery_current_state(),
	};
	memcpy(buf, &header, sizeof(header));
	memcpy(buf + sizeof(header), &batt, sizeof(batt));
	ble_reid_tx_stream(buf, sizeof(buf));
#else
	(void) data;
#endif
}

static void update_charging_state_history(void)
{
	uint8_t is_charging = (battery_current_state() == battery_charging);
	uint8_t charging_samples = 0;

	charging_state_history[charging_state_history_index] = is_charging;
	if (++charging_state_history_index >= CHARGING_STATE_HISTORY_SAMPLES) charging_state_history_index = 0;
	if (charging_state_history_fill < CHARGING_STATE_HISTORY_SAMPLES) ++charging_state_history_fill;

	if (is_charging) charging_non_c_streak = 0;
	else if (charging_non_c_streak < 0xFF) ++charging_non_c_streak;

	for (uint8_t i = 0; i < charging_state_history_fill; ++i) charging_samples += charging_state_history[i];

	if (charging_samples >= CHARGING_STATE_CONFIRM_MIN_C_SAMPLES) charging_confirmed = 1;
	else if (charging_non_c_streak >= CHARGING_STATE_CLEAR_AFTER_NON_C_SAMPLES) charging_confirmed = 0;
}

static uint8_t battery_sleep_protection_required(void)
{
	uint16_t battery_mv = battery_pack_voltage_mv();

	if (low_battery_sleep_latched) {
		// SEN-101: clearing the latch needs LOW_BATTERY_WAKE_CONFIRM_SAMPLES
		// consecutive fresh averages at/above the wake floor -- one rest-
		// recovery or relaxation-spike sample on an aged cell must not wake a
		// device that will immediately sag back below the 3250 floor.
		if (battery_wake_streak() >= LOW_BATTERY_WAKE_CONFIRM_SAMPLES) low_battery_sleep_latched = 0;
		else return 1;
	}

	if (battery_mv < LOW_BATTERY_SLEEP_MIN_MV) {
		low_battery_sleep_latched = 1;
		return 1;
	}

	return (charging_confirmed != 0) && (battery_mv < CHARGE_RECOVERY_VBAT_MIN_MV);
}

// SEN-96: refresh the motion and worn-load hold timestamps from the frame just
// measured. Motion = frame-to-frame accel delta on any axis (buffered IMU
// values, 208 Hz awake config). Worn load = any single FSR channel whose
// WORN_LOAD_WINDOW_MS-window median is >= WORN_LOAD_THRESHOLD; "median >= T"
// is evaluated exactly as "at least half the window's samples >= T". The load
// hold is only *consulted* while session_active (see session_intent_active),
// so static preload outside a session can never hold the device awake.
static void update_activity_holds(const reid_ble_packet_t* current)
{
	static int16_t prev_ax = 0, prev_ay = 0, prev_az = 0;
	static uint8_t has_prev_acc = 0;
	int16_t ax = lsm6dsm_read_ax();
	int16_t ay = lsm6dsm_read_ay();
	int16_t az = lsm6dsm_read_az();

	if (has_prev_acc) {
		int32_t dx = (int32_t)ax - prev_ax; if (dx < 0) dx = -dx;
		int32_t dy = (int32_t)ay - prev_ay; if (dy < 0) dy = -dy;
		int32_t dz = (int32_t)az - prev_az; if (dz < 0) dz = -dz;
		if ((dx >= MOTION_AWAKE_DELTA_LSB) || (dy >= MOTION_AWAKE_DELTA_LSB) || (dz >= MOTION_AWAKE_DELTA_LSB)) {
			last_motion_ms = current->time_ms;
		}
	}
	prev_ax = ax; prev_ay = ay; prev_az = az;
	has_prev_acc = 1;

	static uint64_t load_win_start_ms = 0;
	static uint16_t load_win_samples = 0;
	static uint16_t load_win_hits[19] = {0};
	const uint16_t* fsr = &current->fsr1;

	if (load_win_start_ms == 0) load_win_start_ms = current->time_ms;
	if (load_win_samples < 0xFFFF) ++load_win_samples;
	for (uint8_t i = 0; i < 19; ++i) {
		if (fsr[i] >= WORN_LOAD_THRESHOLD) ++load_win_hits[i];
	}
	if (current->time_ms - load_win_start_ms >= WORN_LOAD_WINDOW_MS) {
		for (uint8_t i = 0; i < 19; ++i) {
			if ((uint32_t)load_win_hits[i] * 2u >= load_win_samples) {
				last_worn_load_ms = current->time_ms;
				break;
			}
		}
		load_win_start_ms = current->time_ms;
		load_win_samples = 0;
		memset(load_win_hits, 0, sizeof(load_win_hits));
	}
}

// SEN-96: the session latch (;CX 1) holds the device awake only while there is
// evidence the session is still real: a live connection, a recent disconnect
// (brief mid-recording BLE drops -- the flag's original purpose), recent
// motion, or a worn-shaped static load (long-seated wearer, phone away). A
// forgotten session with shoes off runs out of all four and sleeps; the flag
// itself survives so a reconnecting app finds consistent state.
static uint8_t session_intent_active(void)
{
	uint64_t now = system_time_ms();

	if (!session_active) return 0;
	if (ble_is_connected()) return 1;
	if ((last_disconnect_ms != 0) && ((now - last_disconnect_ms) <= SESSION_DISCONNECT_GRACE_MS)) return 1;
	if ((last_motion_ms != 0) && ((now - last_motion_ms) <= MOTION_HOLD_MS)) return 1;
	if ((last_worn_load_ms != 0) && ((now - last_worn_load_ms) <= WORN_LOAD_HOLD_MS)) return 1;
	return 0;
}

static uint8_t ble_activity_detected(void)
{
	uint64_t now = system_time_ms();

	if (bench_keepawake || session_intent_active() || ble_is_connected()) return 1;
	if (last_ble_activity_ms == 0) return 0;
	return (now - last_ble_activity_ms) <= BLE_ACTIVITY_HOLD_MS;
}

static void enter_device_sleep(uint64_t* timer, int32_t* summary_counter, uint8_t recovery_sleep)
{
	stream_flush_pending(1);
	flush_summary_if_pending(summary_counter);
	system_wait_for_ms_no_bg(250);
	ble_reid_enter_lifeline();
	// SEN-95: IMU into wake-on-motion (gyro off, accel low-power) for normal
	// sleep -- previously it kept running at 208 Hz A+G (~0.5 mA) and was the
	// dominant sleep drain (deep-discharge kill chain, see SLEEP_LOGIC_V2_PROPOSAL).
	// Protection (recovery) sleep powers the IMU down entirely: motion must not
	// wake a critically low cell, so the wake engine buys nothing there.
	if (recovery_sleep) lsm6dsm_deinit();
	else lsm6dsm_enter_wom();

	// SEN-99: the sleep loop no longer measures FSR/CAP at all. The old
	// level-based wake (>=5 FSRs above 10 counts) oscillated in-shoe-unworn
	// devices awake ~97% of the time, and the delta wake compared against a
	// baseline frozen at sleep entry so slow cap/thermal drift caused spurious
	// wakes. You cannot don or use the insole without motion: wake is the IMU
	// wake-on-motion latch or a BLE connection. Housekeeping (vbat average for
	// the protection floor + charging history) runs every 12th check (~60 s).
	uint8_t battery_check_divider = SLEEP_BATTERY_CHECK_EVERY_N; // immediate first check
	while (1)
	{
		while (system_time_ms() < (*timer + SLEEP_CHECK_EVERY_MS)) system_sleep();
		*timer = system_time_ms();

		if (++battery_check_divider >= SLEEP_BATTERY_CHECK_EVERY_N) {
			battery_check_divider = 0;
			battery_update();
			update_charging_state_history();
		}

		if (recovery_sleep) {
			if (battery_sleep_protection_required()) continue;
			break;
		}

		if (battery_sleep_protection_required()) continue;
		if (ble_is_connected()) break;
		if (lsm6dsm_motion_detected()) { last_motion_ms = system_time_ms(); break; } // SEN-95: latched wake-on-motion
	}

	if (recovery_sleep) lsm6dsm_init(); // was fully powered down; restore config
	lsm6dsm_exit_wom(); // SEN-95: restore 208 Hz A+G for the awake stream
	ble_advertise_again();
}

int main(void)
{
	system_delay_cycles(100000);
	main_init();
	uint64_t timer = 0;
	static int32_t summary_counter = 0;
	// SEN-100: idle timers are wall-clock timestamps (0 = not idling), not
	// per-loop accumulators -- the old += MAIN_LOOP_TIME_MS counted 125 ms per
	// iteration regardless of the runtime ;CF rate (14.4 s timeout at 100 Hz,
	// 24 min at 1 Hz).
	uint64_t idle_since_ms = 0;
	uint64_t charging_idle_since_ms = 0;

	while (1)
	{
		// SEN-98: in IDLE_CONNECTED the loop runs at the slow internal cadence;
		// otherwise at the ;CF-set stream period.
		while (system_time_ms() < (timer + (idle_connected ? IDLE_CONNECTED_PERIOD_MS : main_loop_period_ms))) system_sleep();
        timer = system_time_ms();

		if (battery_query_pause) {
			battery_query_pause = 0;
			continue;
		}

		// SEN-96: record connection-drop instants for the session grace window.
		static uint8_t prev_ble_connected = 0;
		{
			uint8_t now_connected = ble_is_connected();
			if (prev_ble_connected && !now_connected) last_disconnect_ms = system_time_ms();
			prev_ble_connected = now_connected;
		}

		static uint8_t battery_update_divider = 0;
		static uint8_t charging_state_sample_divider = 0;
		if (++battery_update_divider >= 8) {
			battery_update_divider = 0;
			// vbat is acquired in-sequence by measure_sensors() on its own
			// time-based cadence (VBAT_SAMPLE_PERIOD_MS), so no ADC read here --
			// this slot only samples charge state and checks the protection
			// floor against the already-maintained average. Keeping the read out
			// of this frame-counted slot is what makes it rate-independent for
			// the high-rate binary stream.
			if (++charging_state_sample_divider >= CHARGING_STATE_SAMPLE_EVERY_N_BATTERY_UPDATES) {
				charging_state_sample_divider = 0;
				update_charging_state_history();
			}
			if (battery_sleep_protection_required()) {
				idle_since_ms = 0;
				charging_idle_since_ms = 0;
				enter_device_sleep(&timer, &summary_counter, 1);
				continue;
			}
			// SEN-59: do NOT skip measurement for battery housekeeping. The work
			// above is cheap (no blocking ADC -- vbat is read in-sequence inside
			// measure_sensors), so the old unconditional `continue` here dropped a
			// sample every 8th loop: a deterministic 12.5% loss = the ~87.5 Hz
			// ceiling at a 100 Hz target (a 20 ms device-clock gap every 7th row,
			// confirmed regular in walk-test data). Fall through and measure this
			// slot too. This -- not BLE transport -- was the dominant "frame loss".
		}
		measure_sensors((reid_ble_packet_t*) &ble_data,0);
		update_activity_holds((reid_ble_packet_t*) &ble_data); // SEN-96: motion + worn-load holds
		uint8_t sensor_active_now = sensor_activity_detected((reid_ble_packet_t*) &ble_data, &previous_ble_data, has_previous_ble_data);

		// SEN-98: IDLE_CONNECTED state machine. Exit on anything that means the
		// connection is being used again: disconnect (normal idle->sleep path
		// takes over), session start, bench latch, sensor deltas, real motion
		// (WoM engine — the frame-delta path is parked at zero in WoM mode), or
		// a ;CF rate change (the user asked for a specific stream rate). Plain
		// query traffic (e.g. periodic ;QB polls) is served at the slow cadence
		// and neither blocks entry nor forces exit.
		{
			static uint64_t idle_connected_since_ms = 0; // SEN-100: wall clock, 0 = not counting
			static uint16_t last_seen_period_ms = 0;
			uint8_t rate_changed = (last_seen_period_ms != 0) && (last_seen_period_ms != main_loop_period_ms);
			last_seen_period_ms = main_loop_period_ms;

			if (idle_connected) {
				if (!ble_is_connected() || session_active || bench_keepawake ||
					sensor_active_now || rate_changed || lsm6dsm_motion_detected()) {
					idle_connected = 0;
					idle_connected_since_ms = 0;
					lsm6dsm_exit_wom();
				}
			} else if (ble_is_connected() && !session_active && !bench_keepawake) {
				uint8_t motion_recent = (last_motion_ms != 0) && ((system_time_ms() - last_motion_ms) <= 2000);
				if (sensor_active_now || motion_recent) {
					idle_connected_since_ms = 0;
				} else {
					if (idle_connected_since_ms == 0) idle_connected_since_ms = system_time_ms();
					if (system_time_ms() - idle_connected_since_ms >= IDLE_CONNECTED_TIMEOUT_MS) {
						idle_connected_since_ms = 0;
						idle_connected = 1;
						lsm6dsm_enter_wom();
					}
				}
			} else {
				idle_connected_since_ms = 0;
			}
		}
        measure_update_summary((reid_ble_summary_packet_t*) &summary_data, (reid_ble_packet_t*) &ble_data);
		if (++summary_counter >= NEW_SUMMARY_EVERY_N) {
#ifdef ENABLE_FLASH_SUMMARY
			// Persist the 5-minute summary to flash. Gated OFF in the stream and
			// nostream builds: flash_write_record() writes via raw NRF_NVMC, which
			// stalls the SoftDevice radio and drops the BLE link every ~343s while
			// connected. Only the dedicated `summary` build enables this (for
			// ;QR/;QM record retrieval); it should move to nrf_fstorage_sd so it
			// can persist without dropping the link. See the firmware build matrix.
			flash_write_record((reid_ble_summary_packet_t*) &summary_data);
#endif
			measure_reset_summary((reid_ble_summary_packet_t*) &summary_data);
			summary_counter = 0;
		}

		#ifdef ENABLE_DEBUG
		NRF_LOG_INFO("Reading at %u, vdd=%umV", ble_data.time_ms, ble_data.vdd_mv);
		NRF_LOG_INFO("FSR 1-5  %u,%u,%u,%u,%u",\
			ble_data.fsr1,\
			ble_data.fsr2,\
			ble_data.fsr3,\
			ble_data.fsr4,\
			ble_data.fsr5);
		NRF_LOG_INFO("FSR 6-10 %u,%u,%u,%u,%u",\
			ble_data.fsr6,\
			ble_data.fsr7,\
			ble_data.fsr8,\
			ble_data.fsr9,\
			ble_data.fsr10);
		NRF_LOG_INFO("FSR 11-15 %u,%u,%u,%u,%u",\
			ble_data.fsr11,\
			ble_data.fsr12,\
			ble_data.fsr13,\
			ble_data.fsr14,\
			ble_data.fsr15);
		NRF_LOG_INFO("FSR 16-19 %u,%u,%u,%u",\
			ble_data.fsr16,\
			ble_data.fsr17,\
			ble_data.fsr18,\
			ble_data.fsr19);
		NRF_LOG_INFO("TEMP %d,%d,%d,%d,%d",\
			ble_data.temp1,\
			ble_data.temp2,\
			ble_data.temp3,\
			ble_data.temp4,\
			ble_data.temp5);
		NRF_LOG_INFO("CAPN %u,%u,%u,%u,%u,%u",\
			ble_data.cap1n,\
			ble_data.cap2n,\
			ble_data.cap3n,\
			ble_data.cap4n,\
			ble_data.cap5n,\
			ble_data.cap6n);
		NRF_LOG_INFO("CAPS %u,%u,%u,%u,%u,%u",\
			ble_data.cap1s,\
			ble_data.cap2s,\
			ble_data.cap3s,\
			ble_data.cap4s,\
			ble_data.cap5s,\
			ble_data.cap6s);
		NRF_LOG_FLUSH();
		#endif
        
		#ifdef SEND_EVERY_MEAS_OVER_BLE
		#if defined(STREAM_PROTOCOL_ASCII_V1) || defined(STREAM_PROTOCOL_BINARY_V2)
		stream_send_measurement((reid_ble_packet_t*) &ble_data);
		// SEN-68 (v3): emit the low-rate temp frame ~1 Hz (temp is no longer in the row).
		static uint64_t last_temp_frame_ms = 0;
		if ((last_temp_frame_ms == 0) || (ble_data.time_ms - last_temp_frame_ms >= 1000)) {
			last_temp_frame_ms = ble_data.time_ms;
			stream_send_temp((reid_ble_packet_t*) &ble_data);
		}
		// Battery: low-rate vbat frame ~1 Hz for in-session drain metrics.
		static uint64_t last_batt_frame_ms = 0;
		if ((last_batt_frame_ms == 0) || (ble_data.time_ms - last_batt_frame_ms >= 1000)) {
			last_batt_frame_ms = ble_data.time_ms;
			stream_send_battery((reid_ble_packet_t*) &ble_data);
		}
		#else
		static uint8_t ble_message[150] = {0};
		ble_message[0] = ';';
		ble_message[1] = 'R';
		ble_message[2] = 'F';
		ble_message[3] = ' ';
		memcpy(ble_message+4,(reid_ble_packet_t*)&ble_data,sizeof(reid_ble_packet_t));
		ble_message[4+sizeof(reid_ble_packet_t)] = '\r';
		ble_message[5+sizeof(reid_ble_packet_t)] = '\n';
		ble_reid_tx(ble_message,sizeof(reid_ble_packet_t)+6);
		#endif
		#endif
		
		if (battery_sleep_protection_required()) {
			idle_since_ms = 0;
			charging_idle_since_ms = 0;
			enter_device_sleep(&timer, &summary_counter, 1);
		} else if (ble_activity_detected()) {
			idle_since_ms = 0;
			charging_idle_since_ms = 0;
		} else if (sensor_active_now) {
			idle_since_ms = 0;
			charging_idle_since_ms = 0;
		} else if (charging_confirmed) {
			idle_since_ms = 0;
			if (charging_idle_since_ms == 0) charging_idle_since_ms = system_time_ms();
			if (system_time_ms() - charging_idle_since_ms >= CHARGING_IDLE_SLEEP_TIMEOUT_MS) {
				charging_idle_since_ms = 0;
				enter_device_sleep(&timer, &summary_counter, 0);
			}
		} else {
			charging_idle_since_ms = 0;
			if (idle_since_ms == 0) idle_since_ms = system_time_ms();
			if (system_time_ms() - idle_since_ms >= IDLE_SLEEP_TIMEOUT_MS) {
				idle_since_ms = 0;
				enter_device_sleep(&timer, &summary_counter, 0);
			}
		}

		memcpy(&previous_ble_data, (const void*)&ble_data, sizeof(previous_ble_data));
		has_previous_ble_data = 1;
	}
}
