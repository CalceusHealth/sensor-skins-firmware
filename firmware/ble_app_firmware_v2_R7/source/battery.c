/*===========================================
//
// battery.c
// Written by Alex Gilmour
// Copyright (c) 2024, CarbonCircuits
// All rights reserved.
//
//=========================================*/

#include "battery.h"
#include "configure_firmware.h"
#include "drivers/adc.h"
#include "drivers/system.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"

// Resting/discharge curve, based off 0.2C on an unaged battery: pack mV at
// 0 %, 10 %, ... 100 %.
// SINGLE CURVE (battery reliability): the same table is used on and off charge.
// The old dual-curve design switched to an elevated charging table whenever
// PG/CHG was asserted; the two tables disagree by tens of % at the same mV
// (e.g. 3780mV = 52% resting vs 9% charging) and the switch had no debounce,
// so every plug event or PG/CHG flicker made the reported level leap. Charge
// status is surfaced separately via battery_current_state().
static const uint16_t batv_curve[11] = { 3100, 3580, 3660, 3710, 3740, 3770, 3810, 3870, 3940, 4040, 4150 };
#define BATV_FULL		4150
#define BATTERY_UVLO	2800

// ---- averaged voltage --------------------------------------------------------

static uint16_t bat_voltage = 0;
static int32_t bat_voltage_raw = 0;
// 6 samples at VBAT_SAMPLE_PERIOD_MS (~5s) = ~30s window: the average shifts
// meaningfully within ~1 min so the low-battery floor trips promptly across the
// acute discharge knee, while still smoothing per-sample ADC noise.
#define VBAT_AVERAGE_N 6
// A fresh reading this far above the running average is treated as a recharge:
// the moving-average history is flushed so the report snaps to the true level
// instead of washing stale low samples out one slot at a time. Above sample
// noise, well below a real charge step.
#define VBAT_JUMP_RESEED_MV 150
// SEN-101: require this many CONSECUTIVE qualifying samples before flushing.
// One relaxation spike on a degraded high-impedance cell (the known field
// failure mode) must not reseed the average high and instantly cancel
// low-battery protection. A real recharge qualifies on every sample, so the
// snap still lands within ~10 s at the 5 s vbat cadence.
#define VBAT_JUMP_RESEED_SAMPLES 2
static volatile uint16_t vbat_average_buffer[VBAT_AVERAGE_N] = {0};
static volatile int16_t vbat_average_counter = -1;
static volatile uint8_t vbat_jump_streak = 0;
// SEN-101: consecutive fresh *averages* at/above the protection wake floor.
// Counted per sample (not per query), so the debounce can't be satisfied by
// re-reading one cached average.
static volatile uint8_t vbat_wake_streak = 0;
// SEN-106: consecutive fresh averages below the System OFF floor.
static volatile uint8_t vbat_shutdown_streak = 0;

// Folds one raw vbat sample into the moving average and the protection streaks.
static void battery_submit_raw(int32_t batt_raw)
{
	uint16_t batt_reading = adc_vbat_raw_to_mv(batt_raw);

	bat_voltage_raw = batt_raw;
	if ((batt_reading > 500)&&(batt_reading < 5000)) {
		// First sample after boot, or a debounced step up (recharge): drop
		// stale history and reseed every slot with the fresh reading.
		// Upward-only so genuine discharge readings stay smoothed; SEN-101:
		// two consecutive qualifying samples required so a single relaxation
		// spike on a high-impedance cell can't flush the average.
		uint8_t jump = ((int32_t)batt_reading - (int32_t)bat_voltage > VBAT_JUMP_RESEED_MV);
		if (jump) { if (vbat_jump_streak < 0xFF) ++vbat_jump_streak; }
		else vbat_jump_streak = 0;

		if ((vbat_average_counter < 0) || (vbat_jump_streak >= VBAT_JUMP_RESEED_SAMPLES)) {
			for (uint16_t i=0; i<VBAT_AVERAGE_N; ++i) vbat_average_buffer[i] = batt_reading;
			vbat_average_counter = 0;
			vbat_jump_streak = 0;
		} else {
			if (++vbat_average_counter >= VBAT_AVERAGE_N) vbat_average_counter = 0;
			vbat_average_buffer[vbat_average_counter] = batt_reading;
		}
	}
	int32_t average = 0;
	for (uint16_t i=0; i<VBAT_AVERAGE_N; ++i) average += vbat_average_buffer[i];
	if (average != 0) average /= VBAT_AVERAGE_N;
	bat_voltage = average;

	// SEN-101: per-sample wake-floor streak for the protection latch debounce.
	if (bat_voltage >= LOW_BATTERY_WAKE_MIN_MV) { if (vbat_wake_streak < 0xFF) ++vbat_wake_streak; }
	else vbat_wake_streak = 0;

	// SEN-106: per-sample below-System-OFF-floor streak (firmware UVLO).
	if ((bat_voltage > 500) && (bat_voltage < SYSTEM_OFF_VBAT_MV)) { if (vbat_shutdown_streak < 0xFF) ++vbat_shutdown_streak; }
	else vbat_shutdown_streak = 0;
}

void battery_init(void)
{
	battery_update();
}

void battery_update(void)
{
	int32_t batt_raw = adc_read_vbat_raw();
	if (batt_raw > 0) battery_submit_raw(batt_raw);
}

// SEN-102: two-phase read -- enable the gated divider in one frame, sample on a
// later frame once >= VBAT_SETTLE_MS has elapsed. A blocking 30 ms settle would
// stall ~3 frames at 100 Hz every 5 s; this way the settle costs zero frame
// time (divider on-time ~30 ms..1 frame period, ~20 uA while on). A sample
// taken with the gate off after a sleep transition reads ~0 and is skipped.
void battery_sample(uint64_t now_ms)
{
	static uint64_t last_vbat_ms = 0;
	static uint64_t vbat_settle_started_ms = 0;

	if (vbat_settle_started_ms != 0) {
		if (now_ms - vbat_settle_started_ms >= VBAT_SETTLE_MS) {
			vbat_settle_started_ms = 0;
			int32_t vbat_raw = adc_read_vbat_raw_presettled();
			if (vbat_raw > 0) battery_submit_raw(vbat_raw);
		}
	} else if ((last_vbat_ms == 0) || (now_ms - last_vbat_ms >= VBAT_SAMPLE_PERIOD_MS)) {
		last_vbat_ms = now_ms;
		adc_vbat_settle_begin();
		vbat_settle_started_ms = now_ms;
	}
}

int32_t battery_read_raw_fresh(void)
{
	return adc_read_vbat_raw_fresh();
}

// ---- readings ----------------------------------------------------------------

uint16_t battery_pack_voltage_mv(void)
{
	return bat_voltage;
}

int32_t battery_pack_voltage_raw(void)
{
	return bat_voltage_raw;
}

// The resting-curve % of the averaged mV, linearly interpolated between the
// 10 % points. The sleep logic keys off the averaged mV, not this.
uint8_t battery_pack_charge(void)
{
	uint16_t mv = bat_voltage;

	static uint16_t report_counter = 0;
	if (++report_counter > 1000) {
		NRF_LOG_INFO("Batt=%umV", mv);
		NRF_LOG_FLUSH();
		report_counter = 0;
	}

	if (mv >= batv_curve[10]) return 100;
	for (int8_t i = 9; i >= 0; --i) {
		if (mv >= batv_curve[i]) return i * 10 + (((mv - batv_curve[i]) * 10) / (batv_curve[i + 1] - batv_curve[i]));
	}
	return 0;
}

battery_state_t battery_current_state(void)
{
	static uint16_t hysteresis = 0;

	if ((system_charger_power_good())||(system_charger_charging())) {
		if (bat_voltage >= (BATV_FULL-hysteresis)) {
			hysteresis = 20;
			return battery_charged;
		} else {
			hysteresis = 0;
			return battery_charging;
		}
	} else {
		if (bat_voltage >= BATTERY_UVLO-hysteresis) {
			hysteresis = 20;
			return battery_ok;
		} else {
			hysteresis = 0;
			return battery_low;
		}
	}
}

// ---- charger -----------------------------------------------------------------
// "Charging" is only acted on once it shows in CHARGING_STATE_CONFIRM_MIN_C_SAMPLES
// of the last CHARGING_STATE_HISTORY_SAMPLES samples, and is dropped again after
// CHARGING_STATE_CLEAR_AFTER_NON_C_SAMPLES samples in a row without it.

static uint8_t charging_state_history[CHARGING_STATE_HISTORY_SAMPLES] = {0};
static uint8_t charging_state_history_index = 0;
static uint8_t charging_state_history_fill = 0;
static uint8_t charging_non_c_streak = 0;
static uint8_t charging_confirmed = 0;

void battery_sample_charger(void)
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

uint8_t battery_charging_confirmed(void)
{
	return charging_confirmed;
}

// ---- protection --------------------------------------------------------------

uint8_t battery_protection_required(void)
{
	static uint8_t low_battery_latched = 0;

	if (low_battery_latched) {
		// SEN-101: clearing the latch needs LOW_BATTERY_WAKE_CONFIRM_SAMPLES
		// consecutive fresh averages at/above the wake floor -- one rest-
		// recovery or relaxation-spike sample on an aged cell must not wake a
		// device that will immediately sag back below the 3250 floor.
		if (vbat_wake_streak >= LOW_BATTERY_WAKE_CONFIRM_SAMPLES) low_battery_latched = 0;
		else return 1;
	}

	if (bat_voltage < LOW_BATTERY_SLEEP_MIN_MV) {
		low_battery_latched = 1;
		return 1;
	}

	return (charging_confirmed != 0) && (bat_voltage < CHARGE_RECOVERY_VBAT_MIN_MV);
}

// Never while on the puck: the charger is about to bring the cell back up.
uint8_t battery_shutdown_required(void)
{
	return !charging_confirmed && !system_charger_power_good() &&
		(vbat_shutdown_streak >= SYSTEM_OFF_CONFIRM_SAMPLES);
}
