/*===========================================
//
// battery.h
// Written by Alex Gilmour
// Copyright (c) 2024, CarbonCircuits
// All rights reserved.
//
//=========================================*/

#ifndef BATTERY_H_
#define BATTERY_H_

#include <stdint.h>
#include "configure_firmware.h"


typedef enum battery_state_t
{
	battery_ok			= 'K',
	battery_charging	= 'C',
	battery_charged		= 'D',
	battery_low			= 'L',
	battery_unknown		= 'U'
} battery_state_t;

void battery_init();
void battery_deinit();
void battery_update(void);
// Feed one raw vbat ADC sample into the moving average / protection value.
// Call from a context where the SAADC is already idle (e.g. mid measurement
// sequence). battery_update() is the read-and-submit convenience for the
// sleep loop / QB query where it owns the idle converter.
void battery_submit_raw(int32_t batt_raw);
int32_t battery_pack_voltage_raw(void);
// SEN-101: consecutive fresh averages at/above LOW_BATTERY_WAKE_MIN_MV.
// Advances per submitted sample (not per query); used to debounce clearing
// the low-battery protection latch.
uint8_t battery_wake_streak(void);

battery_state_t battery_current_state(void);

uint16_t battery_pack_voltage_mv(void);
uint8_t battery_pack_charge_from_mv(uint16_t bat_voltage);
uint8_t battery_pack_charge(void); // 100 = 100%, 0=0%

#endif // BATTERY_H_ 

