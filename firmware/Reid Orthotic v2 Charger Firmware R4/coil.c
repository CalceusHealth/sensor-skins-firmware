/*===========================================
//
// coil.c
// Written by Alex Gilmour
// Copyright (c) 2024, CarbonCircuits
// All rights reserved.
//
//=========================================*/

#include "configure_firmware.h"
#include "gpio.h"

#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/cpufunc.h>
#include <stdlib.h>

#define PWM_PRESCALE	0
#define PWM_TOP			52

int8_t coil_init(void);
int8_t coil_set_power(uint8_t power);


int8_t coil_init(void) {
	TCA0.SPLIT.CTRLA &= ~( 1 << 0 );					// disable TCA0
	TCA0.SPLIT.CTRLESET = (0b11 << 2) | (0b11 << 0);	// force reset of both timers
	TCA0.SPLIT.CTRLD = (1 << 0);						// Enable Split Mode
	TCA0.SPLIT.CTRLA = (PWM_PRESCALE << 1);				// set prescaler for timer clock
	
	TCA0.SPLIT.LPER = PWM_TOP;
	TCA0.SPLIT.HPER = PWM_TOP;
	TCA0.SPLIT.LCNT = PWM_TOP-1;
	TCA0.SPLIT.HCNT = PWM_TOP-1;
	TCA0.SPLIT.HCMP1 = PWM_TOP; // WO4
	TCA0.SPLIT.HCMP2 = PWM_TOP-1; // WO5
	
	//TCA0.SPLIT.CTRLB = 0b01100000;						// Enable waveform output on WO5 (DRV2), WO4 (DRV1) - high byte
	TCA0.SPLIT.CTRLB = 0b00100000;						// Enable waveform output on WO4 (DRV1) - high byte
	TCA0.SPLIT.CTRLC = 0;								// Clear compare output
	TCA0.SPLIT.INTCTRL = 0;								// No interrupts
	TCA0.SPLIT.DBGCTRL = 0x01;							// Run while in debug
	
	TCA0.SPLIT.CTRLA = TCA0.SPLIT.CTRLA | ( 1 << 0 );	// Enable
	
	return 0;
}


int8_t coil_test_presence(void) {
	TCA0.SPLIT.CTRLA &= ~( 1 << 0 );					// disable TCA0
	TCA0.SPLIT.CTRLESET = (0b11 << 2) | (0b11 << 0);	// force reset of both timers
	TCA0.SPLIT.CTRLD = (1 << 0);						// Enable Split Mode
	TCA0.SPLIT.CTRLA = (PWM_PRESCALE << 1);				// set prescaler for timer clock
	
	TCA0.SPLIT.LPER = 250;
	TCA0.SPLIT.HPER = 250;
	TCA0.SPLIT.LCNT = 250-1;
	TCA0.SPLIT.HCNT = 250-1;
	TCA0.SPLIT.HCMP1 = 250; // WO4
	TCA0.SPLIT.HCMP2 = 250-1; // WO5
	
	//TCA0.SPLIT.CTRLB = 0b01100000;						// Enable waveform output on WO5 (DRV2), WO4 (DRV1) - high byte
	TCA0.SPLIT.CTRLB = 0b00100000;						// Enable waveform output on WO4 (DRV1) - high byte
	TCA0.SPLIT.CTRLC = 0;								// Clear compare output
	TCA0.SPLIT.INTCTRL = 0;								// No interrupts
	TCA0.SPLIT.DBGCTRL = 0x01;							// Run while in debug
	
	TCA0.SPLIT.CTRLA = TCA0.SPLIT.CTRLA | ( 1 << 0 );	// Enable
	
	return 0;
}



int8_t coil_set_power(uint8_t power) {
	if (power > 100) power = 100;
	
	power = power >> 2; // up to 25, top is 50-60

	if (power == 0) {
		TCA0.SPLIT.HCMP1 = PWM_TOP; // WO4
		TCA0.SPLIT.HCMP2 = PWM_TOP-1; // WO5
	} else {
		TCA0.SPLIT.HCMP1 = PWM_TOP-power; // WO4
		TCA0.SPLIT.HCMP2 = PWM_TOP-power-1; // WO5
	}

	return 0;
}


uint16_t coil_read_fb(void) {
	ADC0.CTRLA &= ~ADC_ENABLE_bm;  // stop running ADC
	ADC0.CTRLA = 0; // 10bit resolution, no free running, don't run in standby
	ADC0.CTRLB = 0x02; // accumulate 4 results, so 12bit resolution
	ADC0.CTRLC = 0b01010011; // reduced input impedance, use VDD as reference, clk/16
	ADC0.CTRLD = 0b00100000; // add first run delay, no successive delays
	ADC0.CTRLE = 0; // no window comparator
	ADC0.SAMPCTRL = 2; // sample length before hold and conversion
	ADC0.MUXPOS = 6; // AIN6
	ADC0.CTRLA |= ADC_ENABLE_bm;   // start running ADC
	ADC0.COMMAND = 1;
	while (ADC0.COMMAND);
	ADC0.CTRLA &= ~ADC_ENABLE_bm;  // stop running ADC
	return ADC0.RES;
}


