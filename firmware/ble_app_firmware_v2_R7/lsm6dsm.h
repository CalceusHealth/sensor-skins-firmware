/*===========================================
//
// lsm6dsm.h
// Written by Alex Gilmour
// 27/08/2019
// Copyright (c) 2019, Carbon Circuits.
// All rights reserved.
//
//=========================================*/

#ifndef LSM6DSM_H_
#define LSM6DSM_H_

#include <stdint.h>
#include "i2c.h"
#include "configure_firmware.h"

#define I2C_ADDRESS_LSM6DSM	(0b01101010)	// SA0 strapped low on Reid Orthotic v2 -> 0x6A

#define LSM6DSM_ADDRESS_WHO_AM_I		(0x0F)
#define LSM6DSM_WHO_AM_I_VALUE			(0x6A)
#define LSM6DSM_ADDRESS_CTRL1_XL		(0x10)
#define LSM6DSM_ADDRESS_OUT_TEMP_L		(0x20)

void lsm6dsm_init(void);
void lsm6dsm_deinit(void);

// Reads the WHO_AM_I register directly over I2C; returns LSM6DSM_WHO_AM_I_VALUE (0x6A) when the part is present and reachable.
uint8_t lsm6dsm_whoami(void);

void lsm6dsm_update(void);

int16_t lsm6dsm_read_temp(void);
int16_t lsm6dsm_read_ax(void);
int16_t lsm6dsm_read_ay(void);
int16_t lsm6dsm_read_az(void);
int16_t lsm6dsm_read_gx(void);
int16_t lsm6dsm_read_gy(void);
int16_t lsm6dsm_read_gz(void);


#endif // LSM6DSM_H_ 

