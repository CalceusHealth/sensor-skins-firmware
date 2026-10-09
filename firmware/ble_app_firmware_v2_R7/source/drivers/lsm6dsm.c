/*===========================================
//
// lsm6dsm.c
// Written by Alex Gilmour
// 27/08/2019
// Copyright (c) 2019, Carbon Circuits.
// All rights reserved.
//
//=========================================*/

#include "drivers/lsm6dsm.h"
#include "drivers/board.h"
#include "drivers/system.h"

#include <stdbool.h>
#include "app_util_platform.h"
#include "nrf_drv_twi.h"

// ---- I2C bus ---------------------------------------------------------------
// The IMU is the only device on the bus, so the bus lives here. Transfers are
// blocking, but the wait calls system_sleep(), so commands are served meanwhile.

#define TWI_INSTANCE_ID     1
#define I2C_TIMEOUT		(2000000) // lots of 5ms

static const nrf_drv_twi_t m_twi = NRF_DRV_TWI_INSTANCE(TWI_INSTANCE_ID);
static volatile bool m_xfer_done = false;
static volatile bool i2c_busy = false;
static volatile bool was_error = false;

static const nrf_drv_twi_config_t twi_config = {
	.scl                = PIN_SCL,
	.sda                = PIN_SDA,
	.frequency          = NRF_DRV_TWI_FREQ_400K,
	.interrupt_priority = APP_IRQ_PRIORITY_MID,
	.clear_bus_init     = true,
	.hold_bus_uninit    = true
};

static void twi_handler(nrf_drv_twi_evt_t const * p_event, void * p_context)
{
	switch (p_event->type)
	{
		case NRF_DRV_TWI_EVT_DONE:
			m_xfer_done = true;
			was_error = false;
			break;
		case NRF_DRV_TWI_EVT_ADDRESS_NACK:
		case NRF_DRV_TWI_EVT_DATA_NACK:
			m_xfer_done = true;
			was_error = true;
			break;
		default:
			break;
	}
}

static void i2c_init(void)
{
	nrf_drv_twi_init(&m_twi, &twi_config, twi_handler, NULL);
	nrf_drv_twi_enable(&m_twi);
	i2c_busy = false;
}

static int8_t i2c_begin(void)
{
	if (i2c_busy == true) return -1;
	i2c_busy = true;
	m_xfer_done = false;
	was_error = false;
	return 0;
}

// Waits for the transfer just started. On a timeout or NACK the bus is
// re-initialised (clear_bus_init recovers a stuck slave).
static int8_t i2c_finish(void)
{
	int32_t timeout = I2C_TIMEOUT;
	while ((m_xfer_done == false)&&(--timeout > 0)) system_sleep();
	if ((timeout == 0) || was_error)
	{
		nrf_drv_twi_disable(&m_twi);
		nrf_drv_twi_uninit(&m_twi);
		i2c_init();
		i2c_busy = false;
		return -1;
	}
	i2c_busy = false;
	return 0;
}

// Both return 0 on success, -1 if the bus is busy, timed out or NACKed.
static int8_t i2c_write(uint8_t i2c_address, uint8_t data_len, uint8_t* p_data, bool no_stop)
{
	if (i2c_begin() != 0) return -1;
	nrf_drv_twi_tx(&m_twi, i2c_address, p_data, data_len, no_stop);
	return i2c_finish();
}

static int8_t i2c_read(uint8_t i2c_address, uint8_t data_len, uint8_t* p_data)
{
	if (i2c_begin() != 0) return -1;
	nrf_drv_twi_rx(&m_twi, i2c_address, p_data, data_len);
	return i2c_finish();
}

// ---- LSM6DSM ---------------------------------------------------------------

#define I2C_ADDRESS_LSM6DSM	(0b01101010)

#define LSM6DSM_ADDRESS_WHO_AM_I		(0x0F)
#define LSM6DSM_ADDRESS_CTRL1_XL		(0x10)
#define LSM6DSM_ADDRESS_WAKE_UP_SRC		(0x1B)
#define LSM6DSM_ADDRESS_OUT_TEMP_L		(0x20)
#define LSM6DSM_ADDRESS_TAP_CFG			(0x58)
#define LSM6DSM_ADDRESS_WAKE_UP_THS		(0x5B)
#define LSM6DSM_WAKE_UP_SRC_WU_IA		(0x08)

// ODR field [7:4]: 0011=52, 0100=104, 0101=208 Hz. SEN-68: 208 Hz so each 10 ms
// stream read gets a fresh sample (~4.8 ms sample period).
#define CTRL1_XL_STREAMING	0b01010100	// ACC: 208 Hz, 16g full scale, no BW filter
#define CTRL2_G_STREAMING	0b01011100	// GYRO: 208 Hz, 2000dps full scale, no BW filter

// Last sample, in the order the device outputs it from OUT_TEMP_L.
enum { BUF_TEMP, BUF_GX, BUF_GY, BUF_GZ, BUF_AX, BUF_AY, BUF_AZ, BUF_N };
static volatile int16_t lsm6dsm_buffer[BUF_N];
static volatile lsm6dsm_mode_t lsm6dsm_mode = LSM6DSM_STREAMING;

static void clear_buffer(void)
{
	for (uint8_t i = 0; i < BUF_N; ++i) lsm6dsm_buffer[i] = 0;
}

// Writes consecutive registers starting at data[0] (the device auto-increments).
static void write_registers(uint8_t* data, uint8_t length)
{
	i2c_write(I2C_ADDRESS_LSM6DSM,length,data,false);
}

// Repeated-start register read: point at the register (no stop), then read one
// byte. Returns the byte (0..255), or -1 on I2C bus error.
static int16_t read_register(uint8_t reg)
{
	uint8_t val = 0;
	if (i2c_write(I2C_ADDRESS_LSM6DSM,1,&reg,true) != 0) return -1;
	system_delay_cycles(10000);
	if (i2c_read(I2C_ADDRESS_LSM6DSM,1,&val) != 0) return -1;
	return (int16_t)val;
}

// The complete control block, CTRL1_XL .. MASTER_CONFIG.
static void write_full_config(void)
{
	uint8_t data[12] = {
		LSM6DSM_ADDRESS_CTRL1_XL,
		CTRL1_XL_STREAMING,
		CTRL2_G_STREAMING,
		0b01000100, // CTRL3_C: no resets, buffer values (BDU), autoinc r/w, LSB in lower address
		0b00000000,
		0b00000000, // no selftests
		0b00010000, // accelerometer high performance disabled (XL_HM_MODE), no filters
		0b10000000, // gyro high performance disabled (G_HM_MODE), no filter
		0b00000000, // no filter
		0b00000000, // no DEN value storage
		0b00000000, // no pedometer, tilt or significant motion algorithms
		0b00000000, // MASTER_CONFIG no corrections, default config
	};
	write_registers(data, sizeof(data));
	clear_buffer();
}

void lsm6dsm_init(void)
{
	i2c_init();
	write_full_config();
	lsm6dsm_mode = LSM6DSM_STREAMING;
}

void lsm6dsm_set_mode(lsm6dsm_mode_t mode)
{
	uint8_t data[3];
	lsm6dsm_mode_t previous = lsm6dsm_mode;

	// Recorded first, as the transfers below run the idle hook while they wait.
	lsm6dsm_mode = mode;

	switch (mode)
	{
		case LSM6DSM_STREAMING:
			// Coming back from a power-down the whole control block is rewritten.
			// The two writes below are sent even if the sensor is already
			// streaming; they are idempotent.
			if (previous == LSM6DSM_OFF) write_full_config();

			data[0] = LSM6DSM_ADDRESS_TAP_CFG;
			data[1] = 0b00000000; // interrupts off, latch off: wake engine disarmed
			write_registers(data, 2);

			// Gyro turn-on is ~70 ms, so the first frames after this carry stale
			// gyro values.
			data[0] = LSM6DSM_ADDRESS_CTRL1_XL;
			data[1] = CTRL1_XL_STREAMING;
			data[2] = CTRL2_G_STREAMING;
			write_registers(data, 3);
			break;

		case LSM6DSM_WAKE_ON_MOTION:
			// 52 Hz qualifies for accel low-power mode (XL_HM_MODE is already set
			// by the full config). FS drops to 2 g so the wake threshold LSB is
			// 31.25 mg (at 16 g it is 250 mg -- far too coarse).
			clear_buffer();

			data[0] = LSM6DSM_ADDRESS_CTRL1_XL;
			data[1] = 0b00110000; // ACC: 52 Hz, 2g full scale
			data[2] = 0b00000000; // GYRO: ODR=0 power down
			write_registers(data, 3);

			data[0] = LSM6DSM_ADDRESS_WAKE_UP_THS;
			data[1] = 0x02; // threshold: 2 * 31.25 mg = 62.5 mg (FS 2g)
			data[2] = 0x00; // WAKE_UP_DUR: wake on first sample above threshold
			write_registers(data, 3);

			// Latched (LIR) so a 5 s poll cannot miss a short jolt; reading
			// WAKE_UP_SRC clears the latch.
			data[0] = LSM6DSM_ADDRESS_TAP_CFG;
			data[1] = 0b10000001; // INTERRUPTS_ENABLE | LIR (latched wake flag)
			write_registers(data, 2);

			// The slope filter re-settles on the mode change and can latch a
			// spurious wake; give it a couple of 52 Hz samples then clear the flag.
			system_delay_cycles(3000000); // ~46 ms at 64 MHz
			(void) lsm6dsm_motion_detected();
			break;

		case LSM6DSM_OFF:
			// ODR=0 on both blocks. No SW_RESET -- a reset would drop IF_INC/BDU
			// and require a full re-init to talk to it again.
			clear_buffer();

			data[0] = LSM6DSM_ADDRESS_CTRL1_XL;
			data[1] = 0b00000000; // ACC: ODR=0 power down
			data[2] = 0b00000000; // GYRO: ODR=0 power down
			write_registers(data, 3);
			break;
	}
}

// On I2C error returns 0 -- BLE connection remains a wake source, so a broken
// bus degrades to connect-to-wake rather than stuck-awake.
uint8_t lsm6dsm_motion_detected(void)
{
	int16_t wake_up_src = read_register(LSM6DSM_ADDRESS_WAKE_UP_SRC);
	if (wake_up_src < 0) return 0;
	return (wake_up_src & LSM6DSM_WAKE_UP_SRC_WU_IA) ? 1 : 0;
}

int16_t lsm6dsm_whoami(void)
{
	return read_register(LSM6DSM_ADDRESS_WHO_AM_I);
}

void lsm6dsm_update(void)
{
	// SEN-95/98: outside streaming mode the buffer stays zeroed, so stream rows
	// sent at reduced cadence carry an unambiguous "IMU off" instead of
	// 2g-scaled or stale samples.
	if (lsm6dsm_mode != LSM6DSM_STREAMING) return;

	uint8_t data[2 * BUF_N];
	data[0] = LSM6DSM_ADDRESS_OUT_TEMP_L;
	i2c_write(I2C_ADDRESS_LSM6DSM,1,data,true);
	for (uint8_t i=0; i<sizeof(data); ++i) data[i] = 0;
	system_delay_cycles(10000);
	i2c_read(I2C_ADDRESS_LSM6DSM,sizeof(data),data);

	for (uint8_t i = 0; i < BUF_N; ++i) lsm6dsm_buffer[i] = data[2 * i] + (((int16_t)data[2 * i + 1]) <<8);
}

int16_t lsm6dsm_read_temp(void)	{return lsm6dsm_buffer[BUF_TEMP];}
int16_t lsm6dsm_read_ax(void)	{return lsm6dsm_buffer[BUF_AX];}
int16_t lsm6dsm_read_ay(void)	{return lsm6dsm_buffer[BUF_AY];}
int16_t lsm6dsm_read_az(void)	{return lsm6dsm_buffer[BUF_AZ];}
int16_t lsm6dsm_read_gx(void)	{return lsm6dsm_buffer[BUF_GX];}
int16_t lsm6dsm_read_gy(void)	{return lsm6dsm_buffer[BUF_GY];}
int16_t lsm6dsm_read_gz(void)	{return lsm6dsm_buffer[BUF_GZ];}
