/*===========================================
//
// adc.h
// Written by Alex Gilmour
// Copyright (c) 2024, CarbonCircuits
// All rights reserved.
//
//=========================================*/

// SAADC and the sensor multiplexers in front of it.

#ifndef ADC_H_
#define ADC_H_

#include <stdint.h>

#define ADC_AVG_SAMPLES	(2)
#define ADC_TOP			(4096l)
// SEN-102: vbat divider settle time before sampling. The ADC node RC is
// (100k||100k)*100n = 5 ms; 30 ms = 6 tau (99.75% settled, ratio-metric).
#define VBAT_SETTLE_MS	(30)

// Number of inputs on each sensor mux. Input 0 has no sensor on any of them.
#define ADC_MUX_INPUTS	8
#define ADC_FSR_BANKS	3
#define ADC_CAP_BANKS	2

void adc_init(void);

uint16_t adc_read_vdd_mv(void);

// ---- sensor matrix -----------------------------------------------------------
// Five 8:1 muxes fan the FSRs (3 banks) and cap electrodes (2 banks) into the
// ADC. One frame goes: sensors_on, fsr_begin, fsr_read for inputs 1..7,
// fsr_end, cap_select + cap_read for inputs 0..7, sensors_off. Inputs must be
// visited in ascending order.
void adc_sensors_on(void);		// power the muxes and FSR dividers
void adc_sensors_off(void);

void adc_fsr_begin(void);
// Selects a mux input, lets it settle and reads the 3 FSR banks in one EasyDMA
// scan (SEN-58). Each value is the sum of ADC_AVG_SAMPLES conversions.
void adc_fsr_read(uint8_t input, int32_t bank[ADC_FSR_BANKS]);
void adc_fsr_end(void);

// Selects a cap mux input and lets it settle.
void adc_cap_select(uint8_t input);
// One cap electrode on the selected input of the given bank.
int32_t adc_cap_read(uint8_t bank);

// ---- battery voltage ---------------------------------------------------------
// All raw values are the sum of ADC_AVG_SAMPLES conversions.
// adc_read_vbat_raw() blocks for VBAT_SETTLE_MS; the awake measurement loop
// instead calls settle_begin in one frame and presettled in a later one
// (SEN-102). The _fresh read re-initialises the SAADC first (;QB RAW=).
int32_t adc_read_vbat_raw(void);
void adc_vbat_settle_begin(void);
int32_t adc_read_vbat_raw_presettled(void);
int32_t adc_read_vbat_raw_fresh(void);
uint16_t adc_vbat_raw_to_mv(int32_t reading);

#endif // ADC_H_
