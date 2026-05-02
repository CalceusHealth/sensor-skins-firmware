/*===========================================
//
// main.c
// Written by Alex Gilmour
// Copyright (c) 2024, CarbonCircuits
// All rights reserved.
//
//=========================================*/

#include "main.h"
#include "coil.h"
#include "gpio.h"
#include "configure_firmware.h"

#include <avr/io.h>
#include <avr/cpufunc.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <util/atomic.h>
#include <avr/wdt.h>


static volatile uint8_t last_reset = 0;
static volatile int16_t isr_timer = 0;


void RTC_init(void) {
	/* Initialize RTC: */
	while (RTC.STATUS > 0);               // Wait for all register to be synchronized
	while (RTC.PITSTATUS > 0);            // Wait for everything to be synchronised
	
	RTC.CLKSEL = RTC_CLKSEL_INT32K_gc;    // 32.768kHz Internal Ultra-Low-Power Oscillator (OSCULP32K); other option is 1.024kHz
	RTC.PITINTFLAGS = RTC_PI_bm;          // Clear interrupt flag by writing '1' (required)
	RTC.PITINTCTRL = RTC_PI_bm;           // PIT Interrupt: enabled */
	//RTC.PITCTRLA = RTC_PERIOD_CYC16384_gc // RTC Clock Cycles 16384, resulting in 32.768kHz/16384 = 2Hz
	RTC.PITCTRLA = RTC_PERIOD_CYC256_gc
	| RTC_PITEN_bm;                       // Enable PIT counter: enabled
	#define MS_PER_TICK 8
	while (RTC.PITSTATUS > 0);            // Wait for everything to be synchronised
}

ISR(RTC_PIT_vect) {
	RTC.PITINTFLAGS = RTC_PI_bm;          // Clear interrupt flag by writing '1' (required)
	if (isr_timer >= 0) isr_timer -= MS_PER_TICK;
}

void wait_for_ms(int16_t ms) {
	isr_timer = ms;
	do {
	//sleep_cpu();
	} while (isr_timer > 0);
}

int main(void)
{
	last_reset = RSTCTRL.RSTFR;
	RSTCTRL.RSTFR = 0x3F;
	gpio_init();
	static volatile uint32_t timer=100000;
	while (--timer > 1);
	
	RTC.PITINTCTRL = 0x00; // disable RTC interrupt
	RTC.PITINTFLAGS = 0x01; // Clear interrupt flag
	set_sleep_mode(SLEEP_MODE_IDLE);
	CCP = 0xD8;
	CLKCTRL.MCLKCTRLA = CLKCTRL_CLKSEL_OSC20M_gc;
	while (CLKCTRL.MCLKSTATUS & CLKCTRL_SOSC_bm); // wait for oscillator switch
	CCP = 0xD8;
	CLKCTRL.MCLKCTRLB = 0b00000001; // divide by 2 for main clock of 10MHz
	
	cli();
	/*TCB0.CTRLA = 0;			// Use clk_per and no run on standby
	TCB0.CTRLB = 0;			// no waveform output
	TCB0.CCMP = 10000;		// set to 1ms interrupt
	TCB0.CNT = 0;			// reset count
	TCB0.EVCTRL = 0;		// no input capture
	TCB0.INTCTRL = 0x01;	// enable interrupt on capture
	TCB0.INTFLAGS = 0x01;	// reset interrupt flag
	TCB0.CTRLA |= 0x01;		// enable timer from CLK_PER*/
	gpio_init();
	RTC_init();
	set_sleep_mode(SLEEP_MODE_IDLE);
	sleep_enable();         // Enable sleep mode
	sei();					// enable interrupts
	
	coil_test_presence();
	static uint16_t min = 4000;
	static uint8_t power_on = 0;
	while(1) {
		coil_test_presence();
		wait_for_ms(20);
		while (power_on < 100) {
			wait_for_ms(3);
			uint16_t output = coil_read_fb();
			if ((output < min)&&(output>1800)) min = output;
			if (output > min+50) {
				++power_on;
			} else {
				LED1_R_PORT.OUTSET = (1 << LED1_R_PIN);
				LED1_G_PORT.OUTCLR = (1 << LED1_G_PIN);
				LED1_B_PORT.OUTCLR = (1 << LED1_B_PIN);
				if (power_on > 0) --power_on;
				//power_on = 0;
			}
		}
		LED1_R_PORT.OUTSET = (1 << LED1_R_PIN);
		LED1_G_PORT.OUTSET = (1 << LED1_G_PIN);
		LED1_B_PORT.OUTSET = (1 << LED1_B_PIN);
		coil_init();
		coil_set_power(75); // increased from 25 (~10.5% duty) to 75 (~31.6% duty) for faster charge; max is 100 (~43.9% duty)
		wait_for_ms(10000);
		power_on = 98;
	}

	/*coil_init();
	coil_set_power(25);
	LED1_R_PORT.OUTSET = (1 << LED1_R_PIN);
	LED1_G_PORT.OUTSET = (1 << LED1_G_PIN);
	LED1_B_PORT.OUTSET = (1 << LED1_B_PIN);
	while (1)
	{
	}*/
}

