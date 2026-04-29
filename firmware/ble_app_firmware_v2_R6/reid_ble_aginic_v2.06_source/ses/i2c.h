/*===========================================
//
// i2c.h
// Written by Alex Gilmour
// 27/08/2019
// Copyright (c) 2019, Carbon Circuits.
// All rights reserved.
//
//=========================================*/


#ifndef _I2C_H
#define _I2C_H

#include "system.h"


void i2c_init(void);
void i2c_deinit(void);

int8_t i2c_read(uint8_t i2c_address, uint8_t data_len, uint8_t* p_data);
int8_t i2c_write(uint8_t i2c_address, uint8_t data_len, uint8_t* p_data, bool no_stop);

uint8_t i2c_is_busy(void);

#endif // _I2C_H


