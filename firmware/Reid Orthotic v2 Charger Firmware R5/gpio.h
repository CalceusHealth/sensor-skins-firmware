/*===========================================
//
// gpio.h
// Written by Alex Gilmour
// Copyright (c) 2023, CarbonCircuits
// All rights reserved.
//
//=========================================*/

#ifndef __GPIO_H
#define __GPIO_H

#include <stdint.h>

// Input Pin Configuration

#define ISC                 0 // Input/Sense Configuration.
#define PULLUPEN            3 // Pull-up enable.
#define INVEN               7 // Invert input and output.

#define INTDISABLE          0x0 // Interrupt disabled. Input buffer enabled.
#define BOTHEDGES           0x1 // Interrupt enabled.  Sense on both edges.
#define RISING              0x2 // Interrupt enabled.  Sense on rising edge.
#define FALLING             0x3 // Interrupt enabled.  Sense on falling edge.
#define INPUT_DISABLE       0x4 // Interrupt disabled. Input buffer disabled.
#define LEVEL               0x5 // Interrupt enabled.  Sense on low level.
// Note: Input buffer required to use PORTx.IN for reading value.

// SCH: LED1_R
#define LED1_R_PORT		PORTC
#define LED1_R_PIN		1
#define LED1_R_CTRL		PIN1CTRL

// SCH: LED1_G
#define LED1_G_PORT		PORTC
#define LED1_G_PIN		2
#define LED1_G_CTRL		PIN2CTRL

// SCH: LED1_B
#define LED1_B_PORT		PORTC
#define LED1_B_PIN		0
#define LED1_B_CTRL		PIN0CTRL

// SCH: DRV1
#define DRV1_PORT		PORTA
#define DRV1_PIN		4
#define DRV1_CTRL		PIN4CTRL

// SCH: DRV2
#define DRV2_PORT		PORTA
#define DRV2_PIN		5
#define DRV2_CTRL		PIN5CTRL

// SCH: ADC_VCOIL
#define VCOIL_PORT		PORTA
#define VCOIL_PIN		6
#define VCOIL_CTRL		PIN6CTRL
#define VCOIL_ADC		ADC0
#define VCOIL_AIN		ADC_MUXPOS_AIN6_gc

// SCH: ADC_VBUS
#define VBUS_PORT		PORTA
#define VBUS_PIN		7
#define VBUS_CTRL		PIN7CTRL
#define VBUS_ADC		ADC0
#define VBUS_AIN		ADC_MUXPOS_AIN7_gc


void gpio_init(void); // reset with clamp on output


#endif // __GPIO_H