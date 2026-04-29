/*===========================================
//
// gpio.h
// Written by Alex Gilmour
// Copyright (c) 2024, CarbonCircuits
// All rights reserved.
//
//=========================================*/

#ifndef GPIO_H_
#define GPIO_H_

#include <stdint.h>
#include "nrf_gpio.h"
#include "configure_firmware.h"
#include "system.h"
#include "nordic_common.h"
#include "nrf.h"

// adc pins
#define ADC_VDD			(NRF_SAADC_INPUT_VDD)

#ifdef REID_LHS
//#define PIN_MUX_ON		(25)
#define PIN_MUX_ON		(31)

#define PIN_FSR_CH0		(4)
#define PIN_FSR_CH1		(3)
#define PIN_FSR_CH2		(2)
#define ADC_FSR_CH0		(NRF_SAADC_INPUT_AIN2)
#define ADC_FSR_CH1		(NRF_SAADC_INPUT_AIN1)
#define ADC_FSR_CH2		(NRF_SAADC_INPUT_AIN0)
#define PIN_FSR_S0		(9)
#define PIN_FSR_S1		(8)
#define PIN_FSR_S2		(7)

#define PIN_CAP_CH0		(30)
#define PIN_CAP_CH1		(28)
#define ADC_CAP_CH0		(NRF_SAADC_INPUT_AIN6)
#define ADC_CAP_CH1		(NRF_SAADC_INPUT_AIN4)
#define PIN_CAP_S0		(13)
#define PIN_CAP_S1		(14)
#define PIN_CAP_S2		(15)

#define PIN_TEMP_S0		(18)
#define PIN_TEMP_S1		(19)
#define PIN_TEMP_S2		(17)
#define PIN_TEMP_S3		(16)
#define PIN_TEMP_S4		(20)
#define PIN_TEMP_COM	(29)
#define ADC_TEMP_COM	(NRF_SAADC_INPUT_AIN5)
//#define PIN_TEMPREF		(31)
//#define ADC_TEMPREF		(NRF_SAADC_INPUT_AIN7)

#define PIN_VBAT		(5)
#define ADC_VBAT		(NRF_SAADC_INPUT_AIN3)
#define PIN_VBAT_ON		(6)

#define PIN_BQ_PG		(10)
#define PIN_BQ_CHG		(12)

#define PIN_SDA			(26)
#define PIN_SCL			(27)
//#define PIN_6DOF_INT1	(25)

#define PIN_IS_LHS		(22)

#else
#ifdef REID_RHS
#define PIN_MUX_ON		(31)

#define PIN_FSR_CH0		(2)
#define PIN_FSR_CH1		(3)
#define PIN_FSR_CH2		(4)
#define ADC_FSR_CH0		(NRF_SAADC_INPUT_AIN0)
#define ADC_FSR_CH1		(NRF_SAADC_INPUT_AIN1)
#define ADC_FSR_CH2		(NRF_SAADC_INPUT_AIN2)
#define PIN_FSR_S0		(27)
#define PIN_FSR_S1		(26)
#define PIN_FSR_S2		(25)

#define PIN_CAP_CH0		(30)
#define PIN_CAP_CH1		(28)
#define ADC_CAP_CH0		(NRF_SAADC_INPUT_AIN6)
#define ADC_CAP_CH1		(NRF_SAADC_INPUT_AIN4)
#define PIN_CAP_S0		(15)
#define PIN_CAP_S1		(14)
#define PIN_CAP_S2		(13)

#define PIN_TEMP_S0		(17)
#define PIN_TEMP_S1		(16)
#define PIN_TEMP_S2		(18)
#define PIN_TEMP_S3		(20)
#define PIN_TEMP_S4		(19)
#define PIN_TEMP_COM	(29)
#define ADC_TEMP_COM	(NRF_SAADC_INPUT_AIN5)
//#define PIN_TEMPREF		()
//#define ADC_TEMPREF		(NRF_SAADC_INPUT_AIN)

#define PIN_VBAT		(5)
#define ADC_VBAT		(NRF_SAADC_INPUT_AIN3)
#define PIN_VBAT_ON		(6)

#define PIN_BQ_PG		(10)
#define PIN_BQ_CHG		(11)

#define PIN_SDA			(8)
#define PIN_SCL			(9)
//#define PIN_6DOF_INT1	(11)

#define PIN_IS_LHS		(22)

#else
#error No side defined for pinout
#endif
#endif

#define PIN_RESET		(21)

void gpio_init(void);
void gpio_deinit(void);

uint8_t gpio_bq_chg_asserted(void);
uint8_t gpio_bq_pg_asserted(void);


#endif // GPIO_H_ 

