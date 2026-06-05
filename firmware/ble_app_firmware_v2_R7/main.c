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
static uint16_t count_low_fsr_sensors(const reid_ble_packet_t* data);
static uint16_t count_fsr_delta_score(const reid_ble_packet_t* current, const reid_ble_packet_t* previous);
static uint16_t count_cap_delta_score(const reid_ble_packet_t* current, const reid_ble_packet_t* previous);
static uint8_t sensor_activity_detected(const reid_ble_packet_t* current, const reid_ble_packet_t* previous, uint8_t has_previous);
static void flush_summary_if_pending(int32_t* summary_counter);
static void stream_reset_pending(void);
static void stream_send_measurement(const reid_ble_packet_t* data);
static void stream_flush_pending(uint8_t force);
static void update_charging_state_history(void);
static uint8_t battery_sleep_protection_required(void);
static uint8_t ble_activity_detected(void);
static void enter_device_sleep(uint64_t* timer, int32_t* summary_counter, uint8_t recovery_sleep);

volatile reid_ble_packet_t ble_data = {0};
volatile reid_ble_summary_packet_t summary_data = {0};
volatile uint8_t battery_query_pause = 0;
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
	msg_init();
    //ble_reid_init(); // inside msg_init()
}

static uint16_t count_low_fsr_sensors(const reid_ble_packet_t* data)
{
	uint16_t num_low_sensors = 0;

	if (data->fsr1 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
	if (data->fsr2 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
	if (data->fsr3 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
	if (data->fsr4 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
	if (data->fsr5 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
	if (data->fsr6 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
	if (data->fsr7 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
	if (data->fsr8 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
	if (data->fsr9 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
	if (data->fsr10 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
	if (data->fsr11 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
	if (data->fsr12 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
	if (data->fsr13 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
	if (data->fsr14 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
	if (data->fsr15 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
	if (data->fsr16 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
	if (data->fsr17 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
	if (data->fsr18 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
	if (data->fsr19 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;

	return num_low_sensors;
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

	row->temp[0] = data->temp1;
	row->temp[1] = data->temp2;
	row->temp[2] = data->temp3;
	row->temp[3] = data->temp4;
	row->temp[4] = data->temp5;

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
		.flags = REID_STREAM_BINARY_V2_FLAG_FSR_X4 | REID_STREAM_BINARY_V2_FLAG_CAP_X3,
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
	ble_reid_tx(frame_buffer, frame_len);

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
		if (battery_mv >= LOW_BATTERY_WAKE_MIN_MV) low_battery_sleep_latched = 0;
		else return 1;
	}

	if (battery_mv < LOW_BATTERY_SLEEP_MIN_MV) {
		low_battery_sleep_latched = 1;
		return 1;
	}

	return (charging_confirmed != 0) && (battery_mv < CHARGE_RECOVERY_VBAT_MIN_MV);
}

static uint8_t ble_activity_detected(void)
{
	uint64_t now = system_time_ms();

	if (bench_keepawake || session_active || ble_is_connected()) return 1;
	if (last_ble_activity_ms == 0) return 0;
	return (now - last_ble_activity_ms) <= BLE_ACTIVITY_HOLD_MS;
}

static void enter_device_sleep(uint64_t* timer, int32_t* summary_counter, uint8_t recovery_sleep)
{
	stream_flush_pending(1);
	flush_summary_if_pending(summary_counter);
	system_wait_for_ms_no_bg(250);
	ble_reid_enter_lifeline();

	while (1)
	{
		while (system_time_ms() < (*timer + SLEEP_CHECK_EVERY_MS)) system_sleep();
		*timer = system_time_ms();

		battery_update();
		update_charging_state_history();

		if (recovery_sleep) {
			if (battery_sleep_protection_required()) continue;
			break;
		}

		if (battery_sleep_protection_required()) continue;
		if (ble_is_connected()) break;

		measure_sensors((reid_ble_packet_t*) &ble_data,0);
		if (sensor_activity_detected((reid_ble_packet_t*) &ble_data, &previous_ble_data, has_previous_ble_data)) break;
		if (count_low_fsr_sensors((reid_ble_packet_t*) &ble_data) < FSR_SLEEP_NUM) break;
	}

	ble_advertise_again();
}

int main(void)
{
	system_delay_cycles(100000);
	main_init();
	uint64_t timer = 0;
	static int32_t summary_counter = 0;
	uint32_t idle_sleep_ms = 0;
	uint32_t charging_idle_sleep_ms = 0;

	while (1)
	{
		while (system_time_ms() < (timer + MAIN_LOOP_TIME_MS)) system_sleep();
        timer = system_time_ms();

		if (battery_query_pause) {
			battery_query_pause = 0;
			continue;
		}

		static uint8_t battery_update_divider = 0;
		static uint8_t charging_state_sample_divider = 0;
		if (++battery_update_divider >= 8) {
			battery_update_divider = 0;
			battery_update();
			if (++charging_state_sample_divider >= CHARGING_STATE_SAMPLE_EVERY_N_BATTERY_UPDATES) {
				charging_state_sample_divider = 0;
				update_charging_state_history();
			}
			if (battery_sleep_protection_required()) {
				idle_sleep_ms = 0;
				charging_idle_sleep_ms = 0;
				enter_device_sleep(&timer, &summary_counter, 1);
			}
			continue;
		}
		measure_sensors((reid_ble_packet_t*) &ble_data,0);
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
			idle_sleep_ms = 0;
			charging_idle_sleep_ms = 0;
			enter_device_sleep(&timer, &summary_counter, 1);
		} else if (ble_activity_detected()) {
			idle_sleep_ms = 0;
			charging_idle_sleep_ms = 0;
		} else if (sensor_activity_detected((reid_ble_packet_t*) &ble_data, &previous_ble_data, has_previous_ble_data)) {
			idle_sleep_ms = 0;
			charging_idle_sleep_ms = 0;
		} else if (charging_confirmed) {
			idle_sleep_ms = 0;
			if (charging_idle_sleep_ms < CHARGING_IDLE_SLEEP_TIMEOUT_MS) charging_idle_sleep_ms += MAIN_LOOP_TIME_MS;
			if (charging_idle_sleep_ms >= CHARGING_IDLE_SLEEP_TIMEOUT_MS) {
				charging_idle_sleep_ms = 0;
				enter_device_sleep(&timer, &summary_counter, 0);
			}
		} else {
			charging_idle_sleep_ms = 0;
			if (idle_sleep_ms < IDLE_SLEEP_TIMEOUT_MS) idle_sleep_ms += MAIN_LOOP_TIME_MS;
			if (idle_sleep_ms >= IDLE_SLEEP_TIMEOUT_MS) {
				idle_sleep_ms = 0;
				enter_device_sleep(&timer, &summary_counter, 0);
			}
		}

		memcpy(&previous_ble_data, (const void*)&ble_data, sizeof(previous_ble_data));
		has_previous_ble_data = 1;
	}
}
