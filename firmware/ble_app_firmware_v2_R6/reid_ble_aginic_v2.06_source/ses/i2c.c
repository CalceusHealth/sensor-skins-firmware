/*===========================================
//
// i2c.c
// Written by Alex Gilmour
// 27/08/2019
// Copyright (c) 2019, Carbon Circuits.
// All rights reserved.
//
//=========================================*/


#include "i2c.h"
#include "system.h"
#include "app_util_platform.h"
#include "app_error.h"
#include "nrf_drv_twi.h"
#include "nrf_delay.h"
#include "gpio.h"

// TWI instance ID
#define TWI_INSTANCE_ID     1
#define I2C_TIMEOUT		(2000000) // lots of 5ms

uint8_t current_i2c_instance = 0;

static const nrf_drv_twi_t m_twi = NRF_DRV_TWI_INSTANCE(TWI_INSTANCE_ID);
static volatile bool m_xfer_done = false;
static volatile bool i2c_busy = false;
static volatile bool was_error = false;
void twi_handler(nrf_drv_twi_evt_t const * p_event, void * p_context);

const nrf_drv_twi_config_t twi_config = {
	.scl                = PIN_SCL,
	.sda                = PIN_SDA,
	.frequency          = NRF_DRV_TWI_FREQ_400K,
	.interrupt_priority = APP_IRQ_PRIORITY_MID,
	.clear_bus_init     = true,
	.hold_bus_uninit    = true
};

void i2c_init(void)
{
	nrf_drv_twi_init(&m_twi, &twi_config, twi_handler, NULL);
	nrf_drv_twi_enable(&m_twi);
    i2c_busy = false;
}

void i2c_deinit(void)
{
	nrf_drv_twi_disable(&m_twi);
    nrf_drv_twi_uninit(&m_twi);
	return;
}

int8_t i2c_write(uint8_t i2c_address, uint8_t data_len, uint8_t* p_data, bool no_stop)
{
	if (i2c_busy == true) return -1;
	i2c_busy = true;
	m_xfer_done = false;
    was_error = false;

	nrf_drv_twi_tx(&m_twi, i2c_address, p_data, data_len, no_stop);
    int32_t timeout = I2C_TIMEOUT;
	while ((m_xfer_done == false)&&(--timeout > 0)) system_sleep();
	if ((timeout == 0) || was_error)
	{
		i2c_deinit();
		i2c_init();
		i2c_busy = false;
		return -1;
	}
	i2c_busy = false;
	return 0;
}

int8_t i2c_read(uint8_t i2c_address, uint8_t data_len, uint8_t* p_data)
{
	if (i2c_busy == true) return -1;
	i2c_busy = true;
	m_xfer_done = false;
    was_error = false;

	nrf_drv_twi_rx(&m_twi, i2c_address, p_data, data_len);
    int32_t timeout = I2C_TIMEOUT;
	while ((m_xfer_done == false)&&(--timeout > 0)) system_sleep();
	if ((timeout == 0) || was_error)
	{
		i2c_deinit();
		i2c_init();
		i2c_busy = false;
		return -1;
	}
    i2c_busy = false;
	return 0;
}

void twi_handler(nrf_drv_twi_evt_t const * p_event, void * p_context)
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

uint8_t i2c_is_busy(void)
{
	if (i2c_busy == true) return 0xAA;
	else return 0x00;
}










