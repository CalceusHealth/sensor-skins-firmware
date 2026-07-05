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
	// ODR field [7:4]: 0011=52, 0100=104, 0101=208 Hz. SEN-68: was 0011 (52 Hz) --
	// too slow for a 100 Hz read (samples repeat). Set 0101 = 208 Hz so each 10 ms
	// stream read gets a fresh sample (~4.8 ms sample period). FS unchanged.
	data[1] = 0b01010100; // ACC: 208 Hz, 16g full scale, no BW filter
	data[2] = 0b01011100; // GYRO: 208 Hz, 2000dps full scale, no BW filter
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

	// SEN-95: full power-down (ODR=0 both blocks). No SW_RESET here -- a reset
	// would drop IF_INC/BDU and require a full re-init to talk to it again.
	uint8_t data[3];
	data[0] = LSM6DSM_ADDRESS_CTRL1_XL;
	data[1] = 0b00000000; // ACC: ODR=0 power down
	data[2] = 0b00000000; // GYRO: ODR=0 power down

	i2c_write(I2C_ADDRESS_LSM6DSM,3,data,false);
}

// SEN-95: sleep-mode IMU config. Gyro fully off; accel at 52 Hz in low-power
// mode (XL_HM_MODE=1 is already set by lsm6dsm_init and 52 Hz qualifies), FS
// dropped to 2 g so the wake-on-motion threshold LSB is 31.25 mg (at 16 g the
// LSB is 250 mg -- far too coarse). WAKE_UP_THS=2 -> ~62.5 mg, DUR=0 -> one
// sample above threshold wakes. Latched (LIR) so a 5 s poll cannot miss a
// short jolt; reading WAKE_UP_SRC clears the latch.
void lsm6dsm_enter_wom(void)
{
	uint8_t data[3];

	data[0] = LSM6DSM_ADDRESS_CTRL1_XL;
	data[1] = 0b00110000; // ACC: 52 Hz, 2g full scale
	data[2] = 0b00000000; // GYRO: ODR=0 power down
	i2c_write(I2C_ADDRESS_LSM6DSM,3,data,false);

	data[0] = LSM6DSM_ADDRESS_WAKE_UP_THS;
	data[1] = 0x02; // threshold: 2 * 31.25 mg = 62.5 mg (FS 2g)
	data[2] = 0x00; // WAKE_UP_DUR: wake on first sample above threshold
	i2c_write(I2C_ADDRESS_LSM6DSM,3,data,false);

	data[0] = LSM6DSM_ADDRESS_TAP_CFG;
	data[1] = 0b10000001; // INTERRUPTS_ENABLE | LIR (latched wake flag)
	i2c_write(I2C_ADDRESS_LSM6DSM,2,data,false);

	// The slope filter re-settles on the mode change and can latch a spurious
	// wake; give it a couple of 52 Hz samples then clear the flag so the sleep
	// loop starts from a clean state.
	system_delay_cycles(3000000); // ~46 ms at 64 MHz
	(void) lsm6dsm_motion_detected();
}

// SEN-95: restore the full-rate config (identical to lsm6dsm_init: 208 Hz,
// 16g / 2000 dps) and disarm the wake engine. Gyro turn-on is ~70 ms, so the
// first frames after wake carry stale gyro values -- reconnect takes longer.
void lsm6dsm_exit_wom(void)
{
	uint8_t data[3];

	data[0] = LSM6DSM_ADDRESS_TAP_CFG;
	data[1] = 0b00000000; // interrupts off, latch off
	i2c_write(I2C_ADDRESS_LSM6DSM,2,data,false);

	data[0] = LSM6DSM_ADDRESS_CTRL1_XL;
	data[1] = 0b01010100; // ACC: 208 Hz, 16g full scale (as lsm6dsm_init)
	data[2] = 0b01011100; // GYRO: 208 Hz, 2000dps full scale (as lsm6dsm_init)
	i2c_write(I2C_ADDRESS_LSM6DSM,3,data,false);
}

// SEN-95: poll the latched wake-on-motion flag. Repeated-start register read
// (same pattern as lsm6dsm_whoami). Returns nonzero if motion was seen since
// the last poll. On I2C error returns 0 -- BLE connection remains a wake
// source, so a broken bus degrades to connect-to-wake rather than stuck-awake.
uint8_t lsm6dsm_motion_detected(void)
{
	uint8_t reg = LSM6DSM_ADDRESS_WAKE_UP_SRC;
	uint8_t val = 0;
	if (i2c_write(I2C_ADDRESS_LSM6DSM,1,&reg,true) != 0) return 0;
	system_delay_cycles(10000);
	if (i2c_read(I2C_ADDRESS_LSM6DSM,1,&val) != 0) return 0;
	return (val & LSM6DSM_WAKE_UP_SRC_WU_IA) ? 1 : 0;
}

int16_t lsm6dsm_whoami(void)
{
	uint8_t reg = LSM6DSM_ADDRESS_WHO_AM_I;
	uint8_t val = 0;
	// repeated-start register read: point at WHO_AM_I (no stop), then read 1 byte
	if (i2c_write(I2C_ADDRESS_LSM6DSM,1,&reg,true) != 0) return -1;
	system_delay_cycles(10000);
	if (i2c_read(I2C_ADDRESS_LSM6DSM,1,&val) != 0) return -1;
	return (int16_t)val;
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


