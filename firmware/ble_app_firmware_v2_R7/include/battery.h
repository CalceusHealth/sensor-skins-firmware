/*===========================================
//
// battery.h
// Written by Alex Gilmour
// Copyright (c) 2024, CarbonCircuits
// All rights reserved.
//
//=========================================*/

// Everything about the battery: the averaged voltage, the reported level, the
// charger state and the decisions that protect the cell. The thresholds are in
// configure_firmware.h.

#ifndef BATTERY_H_
#define BATTERY_H_

#include <stdint.h>

typedef enum battery_state_t
{
	battery_ok			= 'K',
	battery_charging	= 'C',
	battery_charged		= 'D',
	battery_low			= 'L',
	battery_unknown		= 'U'
} battery_state_t;

void battery_init(void);

// ---- sampling ----------------------------------------------------------------
// Awake: call once per frame while the SAADC is idle. Takes one vbat sample
// every VBAT_SAMPLE_PERIOD_MS without blocking (the divider is switched on in
// one frame and read in a later one), so it costs nothing at high frame rates.
void battery_sample(uint64_t now_ms);
// Asleep, or for ;QB: takes one sample now. Blocks for VBAT_SETTLE_MS.
void battery_update(void);
// ;QB RAW=: re-initialises the ADC and returns one unaveraged raw reading.
int32_t battery_read_raw_fresh(void);

// ---- readings ----------------------------------------------------------------
uint16_t battery_pack_voltage_mv(void);	// moving average
int32_t battery_pack_voltage_raw(void);	// last raw ADC sample
uint8_t battery_pack_charge(void);		// 100 = 100%, 0=0%
battery_state_t battery_current_state(void);

// ---- charger -----------------------------------------------------------------
// Feeds the current charger state into the debounce (3 of the last 5 samples).
void battery_sample_charger(void);
uint8_t battery_charging_confirmed(void);

// ---- protection --------------------------------------------------------------
// Nonzero while the device must stay in protection sleep: the average is below
// LOW_BATTERY_SLEEP_MIN_MV (latched until it has recovered to
// LOW_BATTERY_WAKE_MIN_MV), or it is charging from below
// CHARGE_RECOVERY_VBAT_MIN_MV. Updates the latch, so call it once per decision.
uint8_t battery_protection_required(void);
// SEN-106: nonzero when the cell has fallen below SYSTEM_OFF_VBAT_MV off the
// charger and the device should power off completely.
uint8_t battery_shutdown_required(void);

#endif // BATTERY_H_
