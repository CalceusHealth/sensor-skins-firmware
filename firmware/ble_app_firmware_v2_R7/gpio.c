/*===========================================
//
// gpio.c
// Written by Alex Gilmour
// Copyright (c) 2024, CarbonCircuits
// All rights reserved.
//
//=========================================*/

#include "gpio.h"
#include "configure_firmware.h"

static uint8_t boards_is_lhs = 0;

void gpio_init(void)
{
	// nrf_gpio_cfg(PIN_LED1, GPIO_PIN_CNF_DIR_Output, GPIO_PIN_CNF_INPUT_Disconnect, GPIO_PIN_CNF_PULL_Disabled, GPIO_PIN_CNF_DRIVE_H0D1, GPIO_PIN_CNF_SENSE_Disabled);

	nrf_gpio_cfg_input(PIN_IS_LHS,	NRF_GPIO_PIN_PULLUP);
	
	nrf_gpio_cfg(PIN_FSR_CH0, GPIO_PIN_CNF_DIR_Input, GPIO_PIN_CNF_INPUT_Disconnect, GPIO_PIN_CNF_PULL_Disabled, GPIO_PIN_CNF_DRIVE_H0D1, GPIO_PIN_CNF_SENSE_Disabled);
	nrf_gpio_cfg(PIN_FSR_CH1, GPIO_PIN_CNF_DIR_Input, GPIO_PIN_CNF_INPUT_Disconnect, GPIO_PIN_CNF_PULL_Disabled, GPIO_PIN_CNF_DRIVE_H0D1, GPIO_PIN_CNF_SENSE_Disabled);
	nrf_gpio_cfg(PIN_FSR_CH2, GPIO_PIN_CNF_DIR_Input, GPIO_PIN_CNF_INPUT_Disconnect, GPIO_PIN_CNF_PULL_Disabled, GPIO_PIN_CNF_DRIVE_H0D1, GPIO_PIN_CNF_SENSE_Disabled);
	nrf_gpio_cfg(PIN_CAP_CH0, GPIO_PIN_CNF_DIR_Input, GPIO_PIN_CNF_INPUT_Disconnect, GPIO_PIN_CNF_PULL_Disabled, GPIO_PIN_CNF_DRIVE_H0D1, GPIO_PIN_CNF_SENSE_Disabled);
	nrf_gpio_cfg(PIN_CAP_CH1, GPIO_PIN_CNF_DIR_Input, GPIO_PIN_CNF_INPUT_Disconnect, GPIO_PIN_CNF_PULL_Disabled, GPIO_PIN_CNF_DRIVE_H0D1, GPIO_PIN_CNF_SENSE_Disabled);
	//GPIO_PIN_CNF_DRIVE_H0H1
	nrf_gpio_cfg(PIN_FSR_S0, GPIO_PIN_CNF_DIR_Output, GPIO_PIN_CNF_INPUT_Disconnect, GPIO_PIN_CNF_PULL_Disabled, GPIO_PIN_CNF_DRIVE_H0H1, GPIO_PIN_CNF_SENSE_Disabled);
	nrf_gpio_cfg(PIN_FSR_S1, GPIO_PIN_CNF_DIR_Output, GPIO_PIN_CNF_INPUT_Disconnect, GPIO_PIN_CNF_PULL_Disabled, GPIO_PIN_CNF_DRIVE_H0H1, GPIO_PIN_CNF_SENSE_Disabled);
	nrf_gpio_cfg(PIN_FSR_S2, GPIO_PIN_CNF_DIR_Output, GPIO_PIN_CNF_INPUT_Disconnect, GPIO_PIN_CNF_PULL_Disabled, GPIO_PIN_CNF_DRIVE_H0H1, GPIO_PIN_CNF_SENSE_Disabled);
	nrf_gpio_cfg(PIN_CAP_S0, GPIO_PIN_CNF_DIR_Output, GPIO_PIN_CNF_INPUT_Disconnect, GPIO_PIN_CNF_PULL_Disabled, GPIO_PIN_CNF_DRIVE_H0H1, GPIO_PIN_CNF_SENSE_Disabled);
	nrf_gpio_cfg(PIN_CAP_S1, GPIO_PIN_CNF_DIR_Output, GPIO_PIN_CNF_INPUT_Disconnect, GPIO_PIN_CNF_PULL_Disabled, GPIO_PIN_CNF_DRIVE_H0H1, GPIO_PIN_CNF_SENSE_Disabled);
	nrf_gpio_cfg(PIN_CAP_S2, GPIO_PIN_CNF_DIR_Output, GPIO_PIN_CNF_INPUT_Disconnect, GPIO_PIN_CNF_PULL_Disabled, GPIO_PIN_CNF_DRIVE_H0H1, GPIO_PIN_CNF_SENSE_Disabled);
	
	nrf_gpio_pin_clear(PIN_FSR_S0);
	nrf_gpio_pin_clear(PIN_FSR_S1);
	nrf_gpio_pin_clear(PIN_FSR_S2);
	nrf_gpio_pin_clear(PIN_CAP_S0);
	nrf_gpio_pin_clear(PIN_CAP_S1);
	nrf_gpio_pin_clear(PIN_CAP_S2);
	
	nrf_gpio_cfg(PIN_TEMP_S0, GPIO_PIN_CNF_DIR_Output, GPIO_PIN_CNF_INPUT_Disconnect, GPIO_PIN_CNF_PULL_Disabled, GPIO_PIN_CNF_DRIVE_D0H1, GPIO_PIN_CNF_SENSE_Disabled);
	nrf_gpio_pin_clear(PIN_TEMP_S0);
	nrf_gpio_cfg(PIN_TEMP_S1, GPIO_PIN_CNF_DIR_Output, GPIO_PIN_CNF_INPUT_Disconnect, GPIO_PIN_CNF_PULL_Disabled, GPIO_PIN_CNF_DRIVE_D0H1, GPIO_PIN_CNF_SENSE_Disabled);
	nrf_gpio_pin_clear(PIN_TEMP_S1);
	nrf_gpio_cfg(PIN_TEMP_S2, GPIO_PIN_CNF_DIR_Output, GPIO_PIN_CNF_INPUT_Disconnect, GPIO_PIN_CNF_PULL_Disabled, GPIO_PIN_CNF_DRIVE_D0H1, GPIO_PIN_CNF_SENSE_Disabled);
	nrf_gpio_pin_clear(PIN_TEMP_S2);
	nrf_gpio_cfg(PIN_TEMP_S3, GPIO_PIN_CNF_DIR_Output, GPIO_PIN_CNF_INPUT_Disconnect, GPIO_PIN_CNF_PULL_Disabled, GPIO_PIN_CNF_DRIVE_D0H1, GPIO_PIN_CNF_SENSE_Disabled);
	nrf_gpio_pin_clear(PIN_TEMP_S3);
	nrf_gpio_cfg(PIN_TEMP_S4, GPIO_PIN_CNF_DIR_Output, GPIO_PIN_CNF_INPUT_Disconnect, GPIO_PIN_CNF_PULL_Disabled, GPIO_PIN_CNF_DRIVE_D0H1, GPIO_PIN_CNF_SENSE_Disabled);
	nrf_gpio_pin_clear(PIN_TEMP_S4);
	nrf_gpio_cfg(PIN_TEMP_COM, GPIO_PIN_CNF_DIR_Input, GPIO_PIN_CNF_INPUT_Disconnect, GPIO_PIN_CNF_PULL_Disabled, GPIO_PIN_CNF_DRIVE_D0S1, GPIO_PIN_CNF_SENSE_Disabled);
	//nrf_gpio_cfg(PIN_TEMPREF, GPIO_PIN_CNF_DIR_Input, GPIO_PIN_CNF_INPUT_Disconnect, GPIO_PIN_CNF_PULL_Disabled, GPIO_PIN_CNF_DRIVE_D0S1, GPIO_PIN_CNF_SENSE_Disabled);
	
	nrf_gpio_cfg(PIN_VBAT, GPIO_PIN_CNF_DIR_Input, GPIO_PIN_CNF_INPUT_Disconnect, GPIO_PIN_CNF_PULL_Disabled, GPIO_PIN_CNF_DRIVE_H0D1, GPIO_PIN_CNF_SENSE_Disabled);
	nrf_gpio_cfg_output(PIN_VBAT_ON);
	nrf_gpio_pin_clear(PIN_VBAT_ON);
	
	nrf_gpio_cfg_input(PIN_BQ_CHG,	NRF_GPIO_PIN_NOPULL);
	nrf_gpio_cfg_input(PIN_BQ_PG,	NRF_GPIO_PIN_NOPULL);
	
	
	nrf_gpio_cfg_input(PIN_SDA,	NRF_GPIO_PIN_PULLUP);
	nrf_gpio_cfg_input(PIN_SCL,	NRF_GPIO_PIN_PULLUP);
	//nrf_gpio_cfg_input(PIN_6DOF_INT1,	NRF_GPIO_PIN_PULLUP);
	
	nrf_gpio_cfg(PIN_MUX_ON, GPIO_PIN_CNF_DIR_Output, GPIO_PIN_CNF_INPUT_Disconnect, GPIO_PIN_CNF_PULL_Pullup, GPIO_PIN_CNF_DRIVE_H0H1, GPIO_PIN_CNF_SENSE_Disabled);
	nrf_gpio_pin_set(PIN_MUX_ON);
	
	if (nrf_gpio_pin_read(PIN_IS_LHS)) boards_is_lhs = 1;
	nrf_gpio_cfg_input(PIN_IS_LHS,	NRF_GPIO_PIN_PULLDOWN);
}

void gpio_deinit(void)
{
	nrf_gpio_cfg(PIN_FSR_CH0, GPIO_PIN_CNF_DIR_Input, GPIO_PIN_CNF_INPUT_Disconnect, GPIO_PIN_CNF_PULL_Disabled, GPIO_PIN_CNF_DRIVE_H0D1, GPIO_PIN_CNF_SENSE_Disabled);
	nrf_gpio_cfg(PIN_FSR_CH1, GPIO_PIN_CNF_DIR_Input, GPIO_PIN_CNF_INPUT_Disconnect, GPIO_PIN_CNF_PULL_Disabled, GPIO_PIN_CNF_DRIVE_H0D1, GPIO_PIN_CNF_SENSE_Disabled);
	nrf_gpio_cfg(PIN_FSR_CH2, GPIO_PIN_CNF_DIR_Input, GPIO_PIN_CNF_INPUT_Disconnect, GPIO_PIN_CNF_PULL_Disabled, GPIO_PIN_CNF_DRIVE_H0D1, GPIO_PIN_CNF_SENSE_Disabled);
	nrf_gpio_cfg(PIN_CAP_CH0, GPIO_PIN_CNF_DIR_Input, GPIO_PIN_CNF_INPUT_Disconnect, GPIO_PIN_CNF_PULL_Disabled, GPIO_PIN_CNF_DRIVE_H0D1, GPIO_PIN_CNF_SENSE_Disabled);
	nrf_gpio_cfg(PIN_CAP_CH1, GPIO_PIN_CNF_DIR_Input, GPIO_PIN_CNF_INPUT_Disconnect, GPIO_PIN_CNF_PULL_Disabled, GPIO_PIN_CNF_DRIVE_H0D1, GPIO_PIN_CNF_SENSE_Disabled);
	nrf_gpio_cfg(PIN_TEMP_COM, GPIO_PIN_CNF_DIR_Input, GPIO_PIN_CNF_INPUT_Disconnect, GPIO_PIN_CNF_PULL_Disabled, GPIO_PIN_CNF_DRIVE_H0D1, GPIO_PIN_CNF_SENSE_Disabled);
	//nrf_gpio_cfg(PIN_TEMPREF, GPIO_PIN_CNF_DIR_Input, GPIO_PIN_CNF_INPUT_Disconnect, GPIO_PIN_CNF_PULL_Disabled, GPIO_PIN_CNF_DRIVE_H0D1, GPIO_PIN_CNF_SENSE_Disabled);
	nrf_gpio_cfg(PIN_VBAT, GPIO_PIN_CNF_DIR_Input, GPIO_PIN_CNF_INPUT_Disconnect, GPIO_PIN_CNF_PULL_Disabled, GPIO_PIN_CNF_DRIVE_H0D1, GPIO_PIN_CNF_SENSE_Disabled);
	
	nrf_gpio_cfg_input(PIN_FSR_S0,NRF_GPIO_PIN_PULLDOWN);
	nrf_gpio_cfg_input(PIN_FSR_S1,NRF_GPIO_PIN_PULLDOWN);
	nrf_gpio_cfg_input(PIN_FSR_S2,NRF_GPIO_PIN_PULLDOWN);
	nrf_gpio_cfg_input(PIN_CAP_S0,NRF_GPIO_PIN_PULLDOWN);
	nrf_gpio_cfg_input(PIN_CAP_S1,NRF_GPIO_PIN_PULLDOWN);
	nrf_gpio_cfg_input(PIN_CAP_S2,NRF_GPIO_PIN_PULLDOWN);
	nrf_gpio_cfg_input(PIN_TEMP_S0,NRF_GPIO_PIN_PULLDOWN);
	nrf_gpio_cfg_input(PIN_TEMP_S1,NRF_GPIO_PIN_PULLDOWN);
	nrf_gpio_cfg_input(PIN_TEMP_S2,NRF_GPIO_PIN_PULLDOWN);
	nrf_gpio_cfg_input(PIN_TEMP_S3,NRF_GPIO_PIN_PULLDOWN);
	nrf_gpio_cfg_input(PIN_TEMP_S4,NRF_GPIO_PIN_PULLDOWN);
	nrf_gpio_cfg_input(PIN_MUX_ON,NRF_GPIO_PIN_NOPULL);
	
	nrf_gpio_cfg_input(PIN_VBAT_ON,NRF_GPIO_PIN_PULLDOWN);
	nrf_gpio_cfg_input(PIN_BQ_PG,NRF_GPIO_PIN_NOPULL);
	nrf_gpio_cfg_input(PIN_BQ_CHG,NRF_GPIO_PIN_NOPULL);
	nrf_gpio_cfg_input(PIN_SDA,NRF_GPIO_PIN_NOPULL);
	nrf_gpio_cfg_input(PIN_SCL,NRF_GPIO_PIN_NOPULL);
	nrf_gpio_cfg_input(PIN_IS_LHS,NRF_GPIO_PIN_PULLDOWN);
}


uint8_t gpio_bq_chg_asserted(void)
{
	if (nrf_gpio_pin_read(PIN_BQ_CHG)) return 0;
	else return 0xFF;
}

uint8_t gpio_bq_pg_asserted(void)
{
	if (nrf_gpio_pin_read(PIN_BQ_PG)) return 0;
	else return 0xFF;
}

