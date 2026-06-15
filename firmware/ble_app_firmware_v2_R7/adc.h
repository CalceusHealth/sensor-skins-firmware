/*===========================================
//
// adc.h
// Written by Alex Gilmour
// Copyright (c) 2024, CarbonCircuits
// All rights reserved.
//
//=========================================*/

#ifndef ADC_H_
#define ADC_H_

#include "gpio.h"
#include <stdint.h>
#include "nrfx_saadc.h"
#include "system.h"
#include "configure_firmware.h"
#include "nrf_saadc.h"

//speed is 16MHz/amount
#define ADC_SAMPLE_SPEED	(85)
//#define ADC_SAMPLE_SPEED	(355)

#define ADC_AVG_SAMPLES	(2)
#define ADC_TOP			(4096l)

void adc_init(void);
void adc_deinit(void);

// SEN-58: bracket the FSR mux loop so the 3 banks are read in one EasyDMA scan.
void adc_banks_begin(void);
void adc_banks_end(void);
int32_t adc_read_bank1(void);
int32_t adc_read_bank2(void);
int32_t adc_read_bank3(void);
uint16_t adc_read_bank1_mv(void);
uint16_t adc_read_bank2_mv(void);
uint16_t adc_read_bank3_mv(void);
uint16_t adc_read_vdd_mv(void);
uint16_t adc_read_vbat_mv(void);
int32_t adc_read_vbat_raw(void);
uint16_t adc_read_vbat_mv_fresh(void);
int32_t adc_read_vbat_raw_fresh(void);
uint16_t adc_vbat_raw_to_mv(int32_t reading);
int32_t adc_read_cap1(void);
int32_t adc_read_cap2(void);

uint32_t adc_generate_random(void);



#endif // ADC_H_ 

