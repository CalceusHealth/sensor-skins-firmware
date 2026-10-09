/*===========================================
//
// lsm6dsm.h
// Written by Alex Gilmour
// 27/08/2019
// Copyright (c) 2019, Carbon Circuits.
// All rights reserved.
//
//=========================================*/

// LSM6DSM accelerometer + gyroscope, on its own I2C bus.

#ifndef LSM6DSM_H_
#define LSM6DSM_H_

#include <stdint.h>

#define LSM6DSM_WHO_AM_I_VALUE			(0x6A)

typedef enum lsm6dsm_mode_t
{
	// Accel +/-16 g and gyro +/-2000 dps at 208 Hz (~0.5 mA).
	LSM6DSM_STREAMING,
	// SEN-95: gyro off, accel low-power 52 Hz at +/-2 g with the wake-on-motion
	// engine armed (~5 uA). The sample buffer reads zero in this mode.
	LSM6DSM_WAKE_ON_MOTION,
	// Both blocks powered down. The sample buffer reads zero.
	LSM6DSM_OFF
} lsm6dsm_mode_t;

// Starts the I2C bus and puts the sensor in LSM6DSM_STREAMING.
void lsm6dsm_init(void);
void lsm6dsm_set_mode(lsm6dsm_mode_t mode);

// Polls the latched wake-on-motion flag (reading clears it): nonzero if motion
// was seen since the last poll. Only meaningful in LSM6DSM_WAKE_ON_MOTION.
uint8_t lsm6dsm_motion_detected(void);

// Reads the WHO_AM_I identity register (expect LSM6DSM_WHO_AM_I_VALUE).
// Returns the register byte (0..255), or -1 on I2C bus error.
int16_t lsm6dsm_whoami(void);

// Reads a fresh sample into the buffer the getters below return (raw LSBs).
// Does nothing unless the sensor is streaming.
void lsm6dsm_update(void);

int16_t lsm6dsm_read_temp(void);
int16_t lsm6dsm_read_ax(void);
int16_t lsm6dsm_read_ay(void);
int16_t lsm6dsm_read_az(void);
int16_t lsm6dsm_read_gx(void);
int16_t lsm6dsm_read_gy(void);
int16_t lsm6dsm_read_gz(void);

#endif // LSM6DSM_H_
