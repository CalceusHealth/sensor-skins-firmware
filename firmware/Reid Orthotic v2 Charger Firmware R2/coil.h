/*===========================================
//
// coil.h
// Written by Alex Gilmour
// Copyright (c) 2024, CarbonCircuits
// All rights reserved.
//
//=========================================*/

#ifndef __COIL_H
#define __COIL_H

#include <stdint.h>

int8_t coil_test_presence(void);
int8_t coil_init(void);
int8_t coil_set_power(uint8_t power);

uint16_t coil_read_fb(void);

#endif // __COIL_H