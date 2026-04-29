/*===========================================
//
// gpio.c
// Written by Alex Gilmour
// Copyright (c) 2023, CarbonCircuits
// All rights reserved.
//
//=========================================*/

#include "gpio.h"
#include "configure_firmware.h"

#include <avr/io.h>

void gpio_init(void) { // reset with clamp on output
	// ADC PINS
	// Disable output driver.
	VCOIL_PORT  .DIRCLR = (1 << VCOIL_PIN);
	VBUS_PORT   .DIRCLR = (1 << VBUS_PIN);

	// Set pins: Interrupt disabled. Input buffer disabled. Pull-up Disabled.
	VCOIL_PORT  .VCOIL_CTRL   = (INPUT_DISABLE << ISC);
	VBUS_PORT   .VBUS_CTRL    = (INPUT_DISABLE << ISC);
	
	// LEDS
	// Enable output driver, set to 1 (red LED by default)
	LED1_R_PORT.DIRSET = (1 << LED1_R_PIN);
	LED1_G_PORT.DIRSET = (1 << LED1_G_PIN);
	LED1_B_PORT.DIRSET = (1 << LED1_B_PIN);
	LED1_R_PORT.OUTSET = (1 << LED1_R_PIN);
	LED1_G_PORT.OUTCLR = (1 << LED1_G_PIN);
	LED1_B_PORT.OUTCLR = (1 << LED1_B_PIN);
	
	// Set FETs to off for now.
	DRV1_PORT.DIRSET = (1 << DRV1_PIN);
	DRV2_PORT.DIRSET = (1 << DRV2_PIN);
	DRV1_PORT.OUTSET = (1 << DRV1_PIN);
	DRV2_PORT.OUTCLR = (1 << DRV2_PIN);
}



