/*===========================================
//
// power.c
// Written by Alex Gilmour
// Copyright (c) 2024, CarbonCircuits
// All rights reserved.
//
//=========================================*/

#include "power.h"
#include <string.h>
#include "configure_firmware.h"
#include "battery.h"
#include "stream.h"
#include "drivers/ble_reid.h"
#include "drivers/lsm6dsm.h"
#include "drivers/system.h"

typedef enum power_state_t
{
	POWER_AWAKE,
	POWER_SLEEP,			// idle: wakes on motion or a BLE connection
	POWER_PROTECTION_SLEEP	// low battery: wakes only when the battery has recovered
} power_state_t;

static power_state_t power_state = POWER_AWAKE;
// SEN-98: connected but idle. A mode of the awake state: frames slow to
// IDLE_CONNECTED_PERIOD_MS and the IMU goes to wake-on-motion. The ;CF rate is
// untouched and resumes on exit.
static uint8_t idle_connected = 0;

// ---- inputs ------------------------------------------------------------------

static volatile uint8_t bench_keepawake = 0;
static volatile uint8_t session_active = 0;
static volatile uint64_t last_ble_activity_ms = 0;	// 0 = never
static volatile uint16_t frame_period_ms = MAIN_LOOP_TIME_MS;
static volatile uint8_t skip_next_frame = 0;

void power_set_bench_keepawake(uint8_t on)	{ bench_keepawake = on; }
void power_set_session_active(uint8_t on)	{ session_active = on; }
uint8_t power_session_active(void)			{ return session_active; }
void power_note_command(void)				{ last_ble_activity_ms = system_time_ms(); }
uint16_t power_frame_period_ms(void)		{ return frame_period_ms; }
void power_skip_next_frame(void)			{ skip_next_frame = 1; }

void power_set_frame_rate_hz(uint32_t hz)
{
	if (hz < MIN_STREAM_RATE_HZ) hz = MIN_STREAM_RATE_HZ;
	if (hz > MAX_STREAM_RATE_HZ) hz = MAX_STREAM_RATE_HZ;
	frame_period_ms = (uint16_t)(1000u / hz);
}

// ---- activity ----------------------------------------------------------------

// The previous frame, for frame-to-frame FSR/cap deltas.
static reid_ble_packet_t previous_frame = {0};
static uint8_t has_previous_frame = 0;
static uint8_t sensor_active_now = 0;

// SEN-96: hold timestamps for the motion/load-gated session latch. 0 = never.
static uint64_t last_disconnect_ms = 0;
static uint64_t last_motion_ms = 0;
static uint64_t last_worn_load_ms = 0;

// SEN-100: idle timers are wall-clock timestamps (0 = not idling), not
// per-loop accumulators, so the timeouts hold at every ;CF rate.
static uint64_t idle_since_ms = 0;
static uint64_t charging_idle_since_ms = 0;

static inline uint16_t abs_delta(uint16_t a, uint16_t b)
{
	return (a > b) ? (a - b) : (b - a);
}

// Sensor activity = enough FSR or cap channels moved since the previous frame.
static uint8_t sensor_activity_detected(const reid_ble_packet_t* current)
{
	uint16_t fsr_delta_score = 0;
	uint16_t cap_delta_score = 0;

	if (!has_previous_frame) return 1;

	for (uint8_t i = 0; i < NUM_FSR; ++i) {
		if (abs_delta(current->fsr[i], previous_frame.fsr[i]) >= FSR_DELTA_THRESHOLD) ++fsr_delta_score;
	}
	for (uint8_t i = 0; i < NUM_CAP; ++i) {
		if (abs_delta(current->cap[i], previous_frame.cap[i]) >= CAP_DELTA_THRESHOLD) ++cap_delta_score;
	}

	return (fsr_delta_score >= FSR_DELTA_WAKE_MIN) || (cap_delta_score >= CAP_DELTA_WAKE_MIN);
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
	static uint16_t load_win_hits[NUM_FSR] = {0};
	// SEN-181: per-channel minimum since the session started. Load is judged
	// above this floor, so a constant preload (laminated orthotic, creased
	// sensor) unloads to ~0 here and cannot pose as a worn foot. Re-armed on
	// every ;CX 0 -> 1 edge.
	static uint16_t session_fsr_floor[NUM_FSR];
	static uint8_t prev_session_active = 0;

	if (session_active && !prev_session_active) {
		for (uint8_t i = 0; i < NUM_FSR; ++i) session_fsr_floor[i] = 0xFFFF;
	}
	prev_session_active = session_active;

	if (load_win_start_ms == 0) load_win_start_ms = current->time_ms;
	if (load_win_samples < 0xFFFF) ++load_win_samples;
	for (uint8_t i = 0; i < NUM_FSR; ++i) {
		if (current->fsr[i] < session_fsr_floor[i]) session_fsr_floor[i] = current->fsr[i];
		if ((uint16_t)(current->fsr[i] - session_fsr_floor[i]) >= WORN_LOAD_THRESHOLD) ++load_win_hits[i];
	}
	if (current->time_ms - load_win_start_ms >= WORN_LOAD_WINDOW_MS) {
		for (uint8_t i = 0; i < NUM_FSR; ++i) {
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
	// SEN-181: hard cap. A latch disconnected this long is treated as leaked;
	// the flag itself survives for the app to reconcile via ;QI SESSION=.
	if ((last_disconnect_ms == 0) || ((now - last_disconnect_ms) > SESSION_DISCONNECTED_MAX_MS)) return 0;
	if ((now - last_disconnect_ms) <= SESSION_DISCONNECT_GRACE_MS) return 1;
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

// SEN-98: idle-connected. Exit on anything that means the connection is being
// used again: disconnect (the normal idle->sleep path takes over), session
// start, bench latch, sensor deltas, real motion (the wake-on-motion engine --
// the frame-delta path reads zero in that mode), or a ;CF rate change (the user
// asked for a specific stream rate). Plain query traffic (e.g. periodic ;QB
// polls) is served at the slow cadence and neither blocks entry nor forces exit.
static void update_idle_connected(void)
{
	static uint64_t idle_connected_since_ms = 0; // SEN-100: wall clock, 0 = not counting
	static uint16_t last_seen_period_ms = 0;
	uint8_t rate_changed = (last_seen_period_ms != 0) && (last_seen_period_ms != frame_period_ms);
	last_seen_period_ms = frame_period_ms;

	if (idle_connected) {
		if (!ble_is_connected() || session_active || bench_keepawake ||
			sensor_active_now || rate_changed || lsm6dsm_motion_detected()) {
			idle_connected = 0;
			idle_connected_since_ms = 0;
			lsm6dsm_set_mode(LSM6DSM_STREAMING);
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
				lsm6dsm_set_mode(LSM6DSM_WAKE_ON_MOTION);
			}
		}
	} else {
		idle_connected_since_ms = 0;
	}
}

// ---- sleeping ----------------------------------------------------------------

// SEN-99: nothing is measured while asleep (level- and delta-based FSR/cap
// wakes oscillated or drifted). You cannot don or use the insole without
// motion, so the wake sources are the IMU's wake-on-motion latch and a BLE
// connection. The vbat average and charger state are refreshed on every
// SLEEP_BATTERY_CHECK_EVERY_N-th check (~60 s).
static uint8_t sleep_battery_check_divider;
static uint64_t protection_conn_since_ms;

static void enter_sleep(power_state_t state)
{
	stream_flush(1);
	system_wait_for_ms_no_bg(250);
	ble_reid_enter_lifeline();
	// SEN-95: wake-on-motion costs ~5 uA against ~0.5 mA streaming, which used
	// to be the dominant sleep drain. Protection sleep powers the IMU down
	// entirely: motion must not wake a critically low cell.
	lsm6dsm_set_mode((state == POWER_PROTECTION_SLEEP) ? LSM6DSM_OFF : LSM6DSM_WAKE_ON_MOTION);

	sleep_battery_check_divider = SLEEP_BATTERY_CHECK_EVERY_N; // immediate first check
	protection_conn_since_ms = 0;
	idle_since_ms = 0;
	charging_idle_since_ms = 0;
	power_state = state;
}

static void wake(void)
{
	lsm6dsm_set_mode(LSM6DSM_STREAMING);
	ble_advertise_again();
	power_state = POWER_AWAKE;
}

// One check, every SLEEP_CHECK_EVERY_MS while asleep.
static void sleep_check(void)
{
	if (++sleep_battery_check_divider >= SLEEP_BATTERY_CHECK_EVERY_N) {
		sleep_battery_check_divider = 0;
		battery_update();
		battery_sample_charger();
	}

	if (power_state == POWER_PROTECTION_SLEEP) {
		// SEN-106: firmware UVLO. If the cell keeps falling, power off entirely
		// (~0.3 uA, wake = puck/reset) before deep-discharge damage.
		if (battery_shutdown_required()) system_enter_deep_shutdown(); // does not return

		// SEN-103: the slow advertising stays connectable and commands are
		// still served, so a phone could hold a 7.5-15 ms link against a
		// critically low cell. Give it PROTECTION_CONN_EJECT_MS to read ;QB and
		// show "battery critical", then drop it -- unless charging is confirmed
		// (the user is at the puck; let them watch it recover).
		if (ble_is_connected() && !battery_charging_confirmed()) {
			if (protection_conn_since_ms == 0) protection_conn_since_ms = system_time_ms();
			if (system_time_ms() - protection_conn_since_ms >= PROTECTION_CONN_EJECT_MS) {
				protection_conn_since_ms = 0;
				ble_reid_enter_lifeline(); // disconnect + resume slow advertising
			}
		} else {
			protection_conn_since_ms = 0;
		}

		if (!battery_protection_required()) wake();
		return;
	}

	// NB: an idle sleep that finds the battery low stays here as it is. It does
	// not move to protection sleep, so the System OFF check above is never
	// reached from this branch.
	if (battery_protection_required()) return;
	if (ble_is_connected()) { wake(); return; }
	if (lsm6dsm_motion_detected()) { last_motion_ms = system_time_ms(); wake(); } // SEN-95: latched wake-on-motion
}

// ---- the main loop's tick ----------------------------------------------------

uint16_t power_tick_period_ms(void)
{
	if (power_state != POWER_AWAKE) return SLEEP_CHECK_EVERY_MS;
	return idle_connected ? IDLE_CONNECTED_PERIOD_MS : frame_period_ms;
}

uint8_t power_tick_begin(void)
{
	if (power_state != POWER_AWAKE) {
		sleep_check();
		return 0;
	}

	if (skip_next_frame) {
		skip_next_frame = 0;
		return 0;
	}

	// SEN-96: record connection-drop instants for the session grace window.
	static uint8_t prev_ble_connected = 0;
	uint8_t now_connected = ble_is_connected();
	if (prev_ble_connected && !now_connected) last_disconnect_ms = system_time_ms();
	prev_ble_connected = now_connected;

	// Every 8th frame: check the protection floor against the vbat average, and
	// every CHARGING_STATE_SAMPLE_EVERY_N_BATTERY_UPDATES-th of those sample the
	// charger. Neither reads the ADC (battery_sample does, on its own clock), so
	// the frame is still measured (SEN-59). These two dividers count frames, so
	// their cadence follows the frame rate.
	static uint8_t battery_check_divider = 0;
	static uint8_t charger_sample_divider = 0;
	if (++battery_check_divider >= 8) {
		battery_check_divider = 0;
		if (++charger_sample_divider >= CHARGING_STATE_SAMPLE_EVERY_N_BATTERY_UPDATES) {
			charger_sample_divider = 0;
			battery_sample_charger();
		}
		if (battery_protection_required()) {
			enter_sleep(POWER_PROTECTION_SLEEP);
			return 0;
		}
	}
	return 1;
}

void power_frame_measured(const reid_ble_packet_t* frame)
{
	update_activity_holds(frame); // SEN-96: motion + worn-load holds
	sensor_active_now = sensor_activity_detected(frame);
	update_idle_connected();
}

// Priority order: battery protection, then anything that counts as use (BLE,
// session, bench latch, sensor deltas), then the charging and normal idle timers.
void power_tick_end(const reid_ble_packet_t* frame)
{
	memcpy(&previous_frame, frame, sizeof(previous_frame));
	has_previous_frame = 1;

	if (battery_protection_required()) {
		enter_sleep(POWER_PROTECTION_SLEEP);
	} else if (ble_activity_detected() || sensor_active_now) {
		idle_since_ms = 0;
		charging_idle_since_ms = 0;
	} else if (battery_charging_confirmed()) {
		idle_since_ms = 0;
		if (charging_idle_since_ms == 0) charging_idle_since_ms = system_time_ms();
		if (system_time_ms() - charging_idle_since_ms >= CHARGING_IDLE_SLEEP_TIMEOUT_MS) enter_sleep(POWER_SLEEP);
	} else {
		charging_idle_since_ms = 0;
		if (idle_since_ms == 0) idle_since_ms = system_time_ms();
		if (system_time_ms() - idle_since_ms >= IDLE_SLEEP_TIMEOUT_MS) enter_sleep(POWER_SLEEP);
	}
}
