/*===========================================
//
// battery.c
// Written by Alex Gilmour
// Copyright (c) 2024, CarbonCircuits
// All rights reserved.
//
//=========================================*/

#include "battery.h"
#include "system.h"
#include "adc.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"

// based off 0.2C on unaged battery
#define batv_0		3100
#define batv_10		3580
#define batv_20		3660
#define batv_30		3710
#define batv_40		3740
#define batv_50		3770
#define batv_60		3810
#define batv_70		3870
#define batv_80		3940
#define batv_90		4040
#define batv_100	4150

// first attempt at a charging curve instead of discharge
#define batv_chg_0		3100
#define batv_chg_10		3850
#define batv_chg_20		3900
#define batv_chg_30		3925
#define batv_chg_40		3950
#define batv_chg_50		3975
#define batv_chg_60		4025
#define batv_chg_70		4075
#define batv_chg_80		4125
#define batv_chg_90		4175
#define batv_chg_100	4250

#define BATTERY_UVLO	2800

static uint16_t bat_voltage = 0;
static int32_t bat_voltage_raw = 0;
uint16_t batt_charging_hysteresis = 0;
// 6 samples at VBAT_SAMPLE_PERIOD_MS (~5s) = ~30s window: the average shifts
// meaningfully within ~1 min so the low-battery floor trips promptly across the
// acute discharge knee, while still smoothing per-sample ADC noise.
#define VBAT_AVERAGE_N 6
// A fresh reading this far above the running average is treated as a recharge:
// the moving-average history is flushed so the report snaps to the true level
// instead of washing stale low samples out one slot at a time. Above sample
// noise, well below a real charge step.
#define VBAT_JUMP_RESEED_MV 150
static volatile uint16_t vbat_average_buffer[VBAT_AVERAGE_N] = {0};
static volatile int16_t vbat_average_counter = -1;

void battery_init()
{
	battery_update();
	return;
}

void battery_deinit()
{
	return;
}

// Process one raw vbat sample into the moving average + protection value.
// Acquisition is owned by whoever has the SAADC idle (the measurement sequence
// while awake, the sleep loop / QB while asleep/idle), so this never touches the
// ADC and never has to cope with a busy/0 read. Previously battery_update() read
// the ADC opportunistically and, on a busy converter, fell back to a forced
// fresh read whose ~1.5x-high scale failed the validity guard below -> the whole
// update no-op'd and the average froze (see SEN-48/SEN-52). Sourcing the sample
// from an already-idle converter removes that failure mode entirely.
void battery_submit_raw(int32_t batt_raw)
{
	uint16_t batt_reading = adc_vbat_raw_to_mv(batt_raw);

	bat_voltage_raw = batt_raw;
	if ((batt_reading > 500)&&(batt_reading < 5000)) {
		// First sample after boot, or a clear step up (recharge): drop stale
		// history and reseed every slot with the fresh reading. Upward-only so
		// genuine discharge readings stay smoothed.
		if ((vbat_average_counter < 0) ||
		    ((int32_t)batt_reading - (int32_t)bat_voltage > VBAT_JUMP_RESEED_MV)) {
			for (uint16_t i=0; i<VBAT_AVERAGE_N; ++i) vbat_average_buffer[i] = batt_reading;
            vbat_average_counter = 0;
		} else {
			if (++vbat_average_counter >= VBAT_AVERAGE_N) vbat_average_counter = 0;
			vbat_average_buffer[vbat_average_counter] = batt_reading;
		}
	}
	int32_t average = 0;
	for (uint16_t i=0; i<VBAT_AVERAGE_N; ++i) average += vbat_average_buffer[i];
	if (average != 0) average /= VBAT_AVERAGE_N;
	bat_voltage = average;
}

// Acquire one vbat sample and process it. Call only where the SAADC is
// guaranteed idle: the sleep loop (where measure_sensors may not run) and the
// QB query (which pauses the main loop first). The awake path acquires inside
// measure_sensors() and calls battery_submit_raw() directly, keeping vbat
// sampling decoupled from the sensor frame rate.
void battery_update(void)
{
	int32_t batt_raw = adc_read_vbat_raw();
	if (batt_raw > 0) battery_submit_raw(batt_raw);
}

int32_t battery_pack_voltage_raw(void)
{
	return bat_voltage_raw;
}

battery_state_t battery_current_state(void)
{
	if ((gpio_bq_pg_asserted())||(gpio_bq_chg_asserted())) {
		if (bat_voltage >= (batv_100-batt_charging_hysteresis)) {
			batt_charging_hysteresis = 20;
			return battery_charged;
		} else {
			batt_charging_hysteresis = 0;
			return battery_charging;
		}
		return battery_unknown;
	} else {
		if (bat_voltage >= BATTERY_UVLO-batt_charging_hysteresis) {
			batt_charging_hysteresis = 20;
			return battery_ok;
		} else {
			batt_charging_hysteresis = 0;
			return battery_low;
		}
		return battery_unknown;
	}
}

uint16_t battery_pack_voltage_mv(void)
{
	return bat_voltage;
}

uint8_t battery_pack_charge_from_mv(uint16_t bat_voltage) // 100 = 100%, 0=0%
{
	static uint16_t report_counter = 0;
	if (++report_counter > 1000) {
		NRF_LOG_INFO("Batt=%umV", bat_voltage);
		NRF_LOG_FLUSH();
		report_counter = 0;
	}

	/*if ((gpio_bat_pg_asserted() != 0)&&(gpio_bat_chg_asserted()==0)) {
		return 100;
	} else*/ if ((gpio_bq_pg_asserted())||(gpio_bq_chg_asserted())) {
		if      (bat_voltage >= batv_chg_100) return 100;
		else if (bat_voltage >= batv_chg_90)  return  90 + (((bat_voltage - batv_chg_90)*10) / (batv_chg_100 - batv_chg_90));
		else if (bat_voltage >= batv_chg_80)  return  80 + (((bat_voltage - batv_chg_80)*10) / (batv_chg_90  - batv_chg_80));
		else if (bat_voltage >= batv_chg_70)  return  70 + (((bat_voltage - batv_chg_70)*10) / (batv_chg_80  - batv_chg_70));
		else if (bat_voltage >= batv_chg_60)  return  60 + (((bat_voltage - batv_chg_60)*10) / (batv_chg_70  - batv_chg_60));
		else if (bat_voltage >= batv_chg_50)  return  50 + (((bat_voltage - batv_chg_50)*10) / (batv_chg_60  - batv_chg_50));
		else if (bat_voltage >= batv_chg_40)  return  40 + (((bat_voltage - batv_chg_40)*10) / (batv_chg_50  - batv_chg_40));
		else if (bat_voltage >= batv_chg_30)  return  30 + (((bat_voltage - batv_chg_30)*10) / (batv_chg_40  - batv_chg_30));
		else if (bat_voltage >= batv_chg_20)  return  20 + (((bat_voltage - batv_chg_20)*10) / (batv_chg_30  - batv_chg_20));
		else if (bat_voltage >= batv_chg_10)  return  10 + (((bat_voltage - batv_chg_10)*10) / (batv_chg_20  - batv_chg_10));
		else if (bat_voltage >= batv_chg_0)	  return   0 + (((bat_voltage - batv_chg_0)*10)  / (batv_chg_10  - batv_chg_0));
		else return 0;
	} else {
		if      (bat_voltage >= batv_100) return 100;
		else if (bat_voltage >= batv_90)  return  90 + (((bat_voltage - batv_90)*10) / (batv_100 - batv_90));
		else if (bat_voltage >= batv_80)  return  80 + (((bat_voltage - batv_80)*10) / (batv_90  - batv_80));
		else if (bat_voltage >= batv_70)  return  70 + (((bat_voltage - batv_70)*10) / (batv_80  - batv_70));
		else if (bat_voltage >= batv_60)  return  60 + (((bat_voltage - batv_60)*10) / (batv_70  - batv_60));
		else if (bat_voltage >= batv_50)  return  50 + (((bat_voltage - batv_50)*10) / (batv_60  - batv_50));
		else if (bat_voltage >= batv_40)  return  40 + (((bat_voltage - batv_40)*10) / (batv_50  - batv_40));
		else if (bat_voltage >= batv_30)  return  30 + (((bat_voltage - batv_30)*10) / (batv_40  - batv_30));
		else if (bat_voltage >= batv_20)  return  20 + (((bat_voltage - batv_20)*10) / (batv_30  - batv_20));
		else if (bat_voltage >= batv_10)  return  10 + (((bat_voltage - batv_10)*10) / (batv_20  - batv_10));
		else if (bat_voltage >= batv_0)	  return   0 + (((bat_voltage - batv_0)*10)  / (batv_10  - batv_0));
		else return 0;
	}
}

// Reported charge %, with a monotonic clamp across a charge session.
//
// battery_pack_charge_from_mv() switches between the discharge table (batv_*)
// and the elevated charge table (batv_chg_*) the instant PG/CHG asserts. The two
// tables disagree by a lot at the same mV (e.g. 3780mV = 52% resting but only 9%
// on the charge curve; 4139mV = 99% resting but 82% charging), so plugging in
// made the reported level appear to crater (and unplugging made it leap). The
// charge curve is only meaningful once charge current has actually lifted the
// terminal voltage; right at plug-in the cell is still near its resting OCV and
// the charge table mis-reads it as nearly empty.
//
// Fix: while charging, seed a floor from the last resting report and never let
// the number fall below it or below the highest % already reached this charge
// session. The cell only gains charge while plugged in, so a non-decreasing
// report is physically correct: plug-in holds steady, then climbs. This affects
// only the reported pct -- the sleep matrix keys off the averaged mV, not %.
uint8_t battery_pack_charge(void)
{
	uint8_t curve_pct = battery_pack_charge_from_mv(battery_pack_voltage_mv());
	uint8_t charging = ((gpio_bq_pg_asserted())||(gpio_bq_chg_asserted())) ? 1 : 0;

	static uint8_t was_charging = 0;
	static uint8_t charge_session_pct = 0; // running floor while charging
	static uint8_t last_reported_pct = 0;  // last value handed out (resting ref)

	if (charging) {
		if (!was_charging) {
			// Entering charge: hold at the last resting % instead of dropping
			// to the charge-curve value for the still-unlifted terminal voltage.
			charge_session_pct = last_reported_pct;
			was_charging = 1;
		}
		if (curve_pct > charge_session_pct) charge_session_pct = curve_pct;
		last_reported_pct = charge_session_pct;
		return charge_session_pct;
	}

	// Off charge: report the discharge curve directly (accurate at rest).
	was_charging = 0;
	last_reported_pct = curve_pct;
	return curve_pct;
}
