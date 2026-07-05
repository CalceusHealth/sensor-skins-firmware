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

#define I2C_ADDRESS_LSM6DSM	(0b01101010)

#define LSM6DSM_ADDRESS_WHO_AM_I		(0x0F)
#define LSM6DSM_WHO_AM_I_VALUE			(0x6A)
#define LSM6DSM_ADDRESS_CTRL1_XL		(0x10)
#define LSM6DSM_ADDRESS_WAKE_UP_SRC		(0x1B)
#define LSM6DSM_ADDRESS_OUT_TEMP_L		(0x20)
#define LSM6DSM_ADDRESS_TAP_CFG			(0x58)
#define LSM6DSM_ADDRESS_WAKE_UP_THS		(0x5B)
#define LSM6DSM_WAKE_UP_SRC_WU_IA		(0x08)

void lsm6dsm_init(void);
void lsm6dsm_deinit(void);

// SEN-95 sleep power management. enter_wom: gyro power-down + accel low-power
// 52 Hz with the wake-on-motion engine armed (~5 uA vs ~0.5 mA at 208 Hz A+G).
// exit_wom: restore the full-rate init config. motion_detected: poll the
// latched wake-on-motion flag (reading clears it); call from the sleep loop.
void lsm6dsm_enter_wom(void);
void lsm6dsm_exit_wom(void);
uint8_t lsm6dsm_motion_detected(void);

// Reads the WHO_AM_I identity register (expect LSM6DSM_WHO_AM_I_VALUE).
// Returns the register byte (0..255), or -1 on I2C bus error.
int16_t lsm6dsm_whoami(void);

void lsm6dsm_update(void);

int16_t lsm6dsm_read_temp(void);
int16_t lsm6dsm_read_ax(void);
int16_t lsm6dsm_read_ay(void);
int16_t lsm6dsm_read_az(void);
int16_t lsm6dsm_read_gx(void);
int16_t lsm6dsm_read_gy(void);
int16_t lsm6dsm_read_gz(void);


#endif // LSM6DSM_H_ 

