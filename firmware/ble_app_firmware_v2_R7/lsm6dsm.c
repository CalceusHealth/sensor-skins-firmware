/*===========================================
//
// lsm6dsm.c
// Written by Alex Gilmour
// 27/08/2019
// Copyright (c) 2019, Carbon Circuits.
// All rights reserved.
//
//=========================================*/

#include "lsm6dsm.h"

static volatile int16_t lsm6dsm_buffered_temp;
static volatile int16_t lsm6dsm_buffered_ax;
static volatile int16_t lsm6dsm_buffered_ay;
static volatile int16_t lsm6dsm_buffered_az;
static volatile int16_t lsm6dsm_buffered_gx;
static volatile int16_t lsm6dsm_buffered_gy;
static volatile int16_t lsm6dsm_buffered_gz;



void lsm6dsm_init(void)
{
	// normal mode, ODR at 208
	// G_HM_MODE = 1
	// XL_HM_MODE = 1
	// FIFO_MODE = 000 (bypass)
	uint8_t data[12];
	data[0] = LSM6DSM_ADDRESS_CTRL1_XL;
	data[1] = 0b00110100; // ACC: 208 Hz, 16g full scale, no BW filter; first 4 bits: 0010 = 26, 11=52,100=104,101=208
	data[2] = 0b00111100; // GYRO: 208 Hz, 2000dps full scale, no BW filter
	data[3] = 0b01000100; // no resets, buffer values, autoinc r/w, LSB in lower address
	// data[3] = 0b10000001; // does reset of memory and software
	data[4] = 0b00000000;
	data[5] = 0b00000000; // no selftests
	data[6] = 0b00010000; // accelerometer high performance disabled, no filters
	data[7] = 0b10000000; // gyro high performance disabled, no filter
	data[8] = 0b00000000; // no filter
	data[9] = 0b00000000; // no DEN value storage
	data[10] = 0b00000000; // no pedometer, tilt or significant motion algorithms
	data[11] = 0b00000000; // MASTER_CONFIG no corrections, default config
    
	i2c_write(I2C_ADDRESS_LSM6DSM,12,data,false);

	lsm6dsm_buffered_temp = 0;
	lsm6dsm_buffered_ax = 0;
	lsm6dsm_buffered_ay = 0;
	lsm6dsm_buffered_az = 0;
	lsm6dsm_buffered_gx = 0;
	lsm6dsm_buffered_gy = 0;
	lsm6dsm_buffered_gz = 0;
}

void lsm6dsm_deinit(void)
{
	lsm6dsm_buffered_temp = 0;
	lsm6dsm_buffered_ax = 0;
	lsm6dsm_buffered_ay = 0;
	lsm6dsm_buffered_az = 0;
	lsm6dsm_buffered_gx = 0;
	lsm6dsm_buffered_gy = 0;
	lsm6dsm_buffered_gz = 0;

	uint8_t data[4];
	data[0] = LSM6DSM_ADDRESS_CTRL1_XL;
	data[1] = 0b00000100; // ACC: power down
	data[2] = 0b00001100; // GYRO: power down
	data[3] = 0b11000101; // does reset of memory and software
    
	//i2c_write(I2C_ADDRESS_LSM6DSM,4,data,true);
}

void lsm6dsm_update(void)
{
	uint8_t data[14];
	data[0] = LSM6DSM_ADDRESS_OUT_TEMP_L;
	i2c_write(I2C_ADDRESS_LSM6DSM,1,data,true);
	for (uint8_t i=0; i<14; ++i) data[i] = 0;
	system_delay_cycles(10000);
	i2c_read(I2C_ADDRESS_LSM6DSM,14,data);

	lsm6dsm_buffered_temp	= data[0]  + (((int16_t)data[1]) <<8);
	lsm6dsm_buffered_gx		= data[2]  + (((int16_t)data[3]) <<8);
	lsm6dsm_buffered_gy		= data[4]  + (((int16_t)data[5]) <<8);
	lsm6dsm_buffered_gz		= data[6]  + (((int16_t)data[7]) <<8);
	lsm6dsm_buffered_ax		= data[8]  + (((int16_t)data[9]) <<8);
	lsm6dsm_buffered_ay		= data[10] + (((int16_t)data[11])<<8);
	lsm6dsm_buffered_az		= data[12] + (((int16_t)data[13])<<8);
}

int16_t lsm6dsm_read_temp(void)	{return lsm6dsm_buffered_temp;}
int16_t lsm6dsm_read_ax(void)	{return lsm6dsm_buffered_ax;}
int16_t lsm6dsm_read_ay(void)	{return lsm6dsm_buffered_ay;}
int16_t lsm6dsm_read_az(void)	{return lsm6dsm_buffered_az;}
int16_t lsm6dsm_read_gx(void)	{return lsm6dsm_buffered_gx;}
int16_t lsm6dsm_read_gy(void)	{return lsm6dsm_buffered_gy;}
int16_t lsm6dsm_read_gz(void)	{return lsm6dsm_buffered_gz;}


