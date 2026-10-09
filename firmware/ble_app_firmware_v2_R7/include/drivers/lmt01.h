/*===========================================
//
// lmt01.h
// Written by Alex Gilmour
// 02/11/2019
// Copyright (c) 2019, CarbonCircuits
// All rights reserved.
//
//=========================================*/

// LMT01 pulse-count temperature sensors.

#ifndef LMT01_H_
#define LMT01_H_

#include <stdint.h>

#define LMT01_NUM_SENSORS	5

void lmt01_init(void);

// Temperature of sensor 0..4 (TMP1..TMP5), in 0.01 degC. Blocks ~90 ms.
// +/-32767 means the pulse count was out of range (e.g. no sensor fitted).
int16_t lmt01_get_temp(uint8_t sensor);

#endif // LMT01_H_
