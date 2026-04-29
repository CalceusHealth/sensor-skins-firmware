/*===========================================
//
// lmt01.h
// Written by Alex Gilmour
// 02/11/2019
// Copyright (c) 2019, CarbonCircuits
// All rights reserved.
//
//=========================================*/

#ifndef LMT01_H_
#define LMT01_H_

#include <stdint.h>
#include "configure_firmware.h"

int8_t lmt01_init(void);
int8_t lmt01_deinit(void);

int16_t lmt01_get_temp(uint32_t pin);


#endif // LMT01_H_

