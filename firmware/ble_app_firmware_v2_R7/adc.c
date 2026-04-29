/*===========================================
//
// adc.c
// Written by Alex Gilmour
// Copyright (c) 2024, CarbonCircuits
// All rights reserved.
//
//=========================================*/

#include "adc.h"
#include "configure_firmware.h"
#include "gpio.h"
#include "nrf_gpio.h"
#include "nrf_delay.h"
#include "system.h"

#define ADC_SCALING_FACTOR	6

#define ADC_CHANNEL_CONFIG_VDD						\
{                                                   \
    .resistor_p = NRF_SAADC_RESISTOR_DISABLED,      \
    .resistor_n = NRF_SAADC_RESISTOR_DISABLED,      \
    .gain       = NRF_SAADC_GAIN1_6,                \
    .reference  = NRF_SAADC_REFERENCE_INTERNAL,		\
    .acq_time   = NRF_SAADC_ACQTIME_40US,			\
    .mode       = NRF_SAADC_MODE_SINGLE_ENDED,      \
    .burst      = NRF_SAADC_BURST_DISABLED,         \
    .pin_p      = ADC_VDD,							\
    .pin_n      = NRF_SAADC_INPUT_DISABLED          \
}

#define ADC_CHANNEL_CONFIG_BANK1					\
{                                                   \
    .resistor_p = NRF_SAADC_RESISTOR_PULLUP,        \
    .resistor_n = NRF_SAADC_RESISTOR_DISABLED,      \
    .gain       = NRF_SAADC_GAIN1,                \
    .reference  = NRF_SAADC_REFERENCE_VDD4,         \
    .acq_time   = NRF_SAADC_ACQTIME_10US,           \
    .mode       = NRF_SAADC_MODE_DIFFERENTIAL,      \
    .burst      = NRF_SAADC_BURST_DISABLED,         \
    .pin_p      = NRF_SAADC_INPUT_VDD,				\
    .pin_n      = ADC_FSR_CH0						\
}

//.pin_p      = ADC_FSR_CH0,
//.pin_n      = NRF_SAADC_INPUT_DISABLED

#define ADC_CHANNEL_CONFIG_BANK2					\
{                                                   \
    .resistor_p = NRF_SAADC_RESISTOR_PULLUP,        \
    .resistor_n = NRF_SAADC_RESISTOR_DISABLED,      \
    .gain       = NRF_SAADC_GAIN1,                \
    .reference  = NRF_SAADC_REFERENCE_VDD4,         \
    .acq_time   = NRF_SAADC_ACQTIME_10US,           \
    .mode       = NRF_SAADC_MODE_DIFFERENTIAL,      \
    .burst      = NRF_SAADC_BURST_DISABLED,         \
    .pin_p      = NRF_SAADC_INPUT_VDD,				\
    .pin_n      = ADC_FSR_CH1						\
}

#define ADC_CHANNEL_CONFIG_BANK3					\
{                                                   \
    .resistor_p = NRF_SAADC_RESISTOR_PULLUP,        \
    .resistor_n = NRF_SAADC_RESISTOR_DISABLED,      \
    .gain       = NRF_SAADC_GAIN1,                \
    .reference  = NRF_SAADC_REFERENCE_VDD4,         \
    .acq_time   = NRF_SAADC_ACQTIME_10US,           \
    .mode       = NRF_SAADC_MODE_DIFFERENTIAL,      \
    .burst      = NRF_SAADC_BURST_DISABLED,         \
    .pin_p      = NRF_SAADC_INPUT_VDD,				\
    .pin_n      = ADC_FSR_CH2						\
}

#define ADC_CHANNEL_CONFIG_CAP1						\
{                                                   \
    .resistor_p = NRF_SAADC_RESISTOR_PULLUP,		\
    .resistor_n = NRF_SAADC_RESISTOR_DISABLED,      \
    .gain       = NRF_SAADC_GAIN1_4,                \
    .reference  = NRF_SAADC_REFERENCE_VDD4,         \
    .acq_time   = NRF_SAADC_ACQTIME_3US,			\
    .mode       = NRF_SAADC_MODE_SINGLE_ENDED,      \
    .burst      = NRF_SAADC_BURST_DISABLED,         \
    .pin_p      = ADC_CAP_CH0,						\
    .pin_n      = NRF_SAADC_INPUT_DISABLED          \
}

#define ADC_CHANNEL_CONFIG_CAP2						\
{                                                   \
    .resistor_p = NRF_SAADC_RESISTOR_PULLUP,		\
    .resistor_n = NRF_SAADC_RESISTOR_DISABLED,      \
    .gain       = NRF_SAADC_GAIN1_4,                \
    .reference  = NRF_SAADC_REFERENCE_VDD4,         \
    .acq_time   = NRF_SAADC_ACQTIME_3US,			\
    .mode       = NRF_SAADC_MODE_SINGLE_ENDED,      \
    .burst      = NRF_SAADC_BURST_DISABLED,         \
    .pin_p      = ADC_CAP_CH1,						\
    .pin_n      = NRF_SAADC_INPUT_DISABLED          \
}

#define ADC_CHANNEL_CONFIG_CAP1_LOW					\
{                                                   \
    .resistor_p = NRF_SAADC_RESISTOR_PULLDOWN,		\
    .resistor_n = NRF_SAADC_RESISTOR_DISABLED,      \
    .gain       = NRF_SAADC_GAIN1_4,                \
    .reference  = NRF_SAADC_REFERENCE_VDD4,         \
    .acq_time   = NRF_SAADC_ACQTIME_3US,			\
    .mode       = NRF_SAADC_MODE_SINGLE_ENDED,      \
    .burst      = NRF_SAADC_BURST_DISABLED,         \
    .pin_p      = ADC_CAP_CH0,						\
    .pin_n      = NRF_SAADC_INPUT_DISABLED          \
}

#define ADC_CHANNEL_CONFIG_CAP2_LOW					\
{                                                   \
    .resistor_p = NRF_SAADC_RESISTOR_PULLDOWN,		\
    .resistor_n = NRF_SAADC_RESISTOR_DISABLED,      \
    .gain       = NRF_SAADC_GAIN1_4,                \
    .reference  = NRF_SAADC_REFERENCE_VDD4,         \
    .acq_time   = NRF_SAADC_ACQTIME_3US,			\
    .mode       = NRF_SAADC_MODE_SINGLE_ENDED,      \
    .burst      = NRF_SAADC_BURST_DISABLED,         \
    .pin_p      = ADC_CAP_CH1,						\
    .pin_n      = NRF_SAADC_INPUT_DISABLED          \
}

#define ADC_CHANNEL_CONFIG_VBAT						\
{                                                   \
    .resistor_p = NRF_SAADC_RESISTOR_DISABLED,      \
    .resistor_n = NRF_SAADC_RESISTOR_DISABLED,      \
    .gain       = NRF_SAADC_GAIN1_6,                \
    .reference  = NRF_SAADC_REFERENCE_INTERNAL,		\
    .acq_time   = NRF_SAADC_ACQTIME_40US,			\
    .mode       = NRF_SAADC_MODE_SINGLE_ENDED,      \
    .burst      = NRF_SAADC_BURST_DISABLED,         \
    .pin_p      = ADC_VBAT,							\
    .pin_n      = NRF_SAADC_INPUT_DISABLED          \
}

#define ADC_DEFAULT_CONFIG								\
{														\
    .resolution         = NRF_SAADC_RESOLUTION_12BIT,	\
    .oversample         = NRF_SAADC_OVERSAMPLE_DISABLED,\
    .interrupt_priority = 2,							\
    .low_power_mode     = 0								\
}

static void adc_event_handler(nrfx_saadc_evt_t const *p_event);

static const nrf_saadc_channel_config_t adc_channel_vbat		= ADC_CHANNEL_CONFIG_VBAT;
static const nrf_saadc_channel_config_t adc_channel_vdd			= ADC_CHANNEL_CONFIG_VDD;
static const nrf_saadc_channel_config_t adc_channel_bank1		= ADC_CHANNEL_CONFIG_BANK1;
static const nrf_saadc_channel_config_t adc_channel_bank2		= ADC_CHANNEL_CONFIG_BANK2;
static const nrf_saadc_channel_config_t adc_channel_bank3		= ADC_CHANNEL_CONFIG_BANK3;
static const nrf_saadc_channel_config_t adc_channel_cap1		= ADC_CHANNEL_CONFIG_CAP1;
static const nrf_saadc_channel_config_t adc_channel_cap2		= ADC_CHANNEL_CONFIG_CAP2;
static const nrf_saadc_channel_config_t adc_channel_cap1_low	= ADC_CHANNEL_CONFIG_CAP1_LOW;
static const nrf_saadc_channel_config_t adc_channel_cap2_low	= ADC_CHANNEL_CONFIG_CAP2_LOW;

static const nrfx_saadc_config_t adc_config = ADC_DEFAULT_CONFIG;

#define VDIV_VBAT_R1	(21)
#define VDIV_VBAT_R2	(10)

typedef enum adc_current_state_t
{
	ADC_INIT = 0,
	ADC_CAL,
	ADC_IDLE,
	ADC_ONESHOT,
	ADC_RUNNING,
	ADC_DONE
} adc_current_state_t;

static volatile adc_current_state_t adc_current_state;

void adc_init(void)
{
	nrfx_err_t ret_err;
	adc_current_state = ADC_INIT;
	do {
		nrfx_saadc_abort();
		nrfx_saadc_uninit();
		ret_err = nrfx_saadc_init(&adc_config, adc_event_handler);
		if (ret_err == NRFX_SUCCESS) ret_err = nrfx_saadc_channel_uninit(1);
		if (ret_err == NRFX_SUCCESS) ret_err = nrfx_saadc_channel_uninit(2);
		if (ret_err == NRFX_SUCCESS) ret_err = nrfx_saadc_channel_uninit(3);
		if (ret_err == NRFX_SUCCESS) ret_err = nrfx_saadc_channel_uninit(4);
		if (ret_err == NRFX_SUCCESS) ret_err = nrfx_saadc_channel_uninit(5);
		if (ret_err == NRFX_SUCCESS) ret_err = nrfx_saadc_channel_uninit(6);
		if (ret_err == NRFX_SUCCESS) ret_err = nrfx_saadc_channel_uninit(7);
		if (ret_err == NRFX_SUCCESS) ret_err = nrfx_saadc_channel_uninit(0);
		if (ret_err == NRFX_SUCCESS) ret_err = nrfx_saadc_channel_init(0,&adc_channel_vdd);
		/*if (ret_err == NRFX_SUCCESS)
		{
			adc_current_state = ADC_CAL;
			ret_err = nrfx_saadc_calibrate_offset();
			while ((adc_current_state == ADC_CAL) && (ret_err == NRFX_SUCCESS));
		}*/
		if (ret_err != NRFX_SUCCESS) adc_current_state = ADC_INIT;
		else adc_current_state = ADC_IDLE;
	} while (adc_current_state == ADC_INIT);
    adc_current_state = ADC_IDLE;
}

void adc_deinit(void)
{
	nrfx_saadc_abort();
	nrfx_saadc_channel_uninit(0);
	nrfx_saadc_channel_uninit(1);
	nrfx_saadc_channel_uninit(2);
	nrfx_saadc_channel_uninit(3);
	nrfx_saadc_channel_uninit(4);
	nrfx_saadc_channel_uninit(5);
	nrfx_saadc_channel_uninit(6);
	nrfx_saadc_channel_uninit(7);
	nrfx_saadc_uninit();
}

static void adc_event_handler(nrfx_saadc_evt_t const *p_event)
{
	switch (adc_current_state)
	{
		case ADC_IDLE:
		case ADC_INIT:
		default:
			break;

		case ADC_CAL:
			if ((p_event->type == NRFX_SAADC_EVT_CALIBRATEDONE) || (p_event->type == NRFX_SAADC_EVT_DONE)) adc_current_state = ADC_IDLE;
			break;

		case ADC_ONESHOT:
			if (p_event->type == NRFX_SAADC_EVT_DONE) adc_current_state = ADC_IDLE;
			break;
		
		case ADC_RUNNING:
			if (p_event->type == NRFX_SAADC_EVT_DONE) {
				adc_current_state = ADC_DONE;
				NRF_SAADC->SAMPLERATE = 0;
				//nrfx_ppi_channel_disable(m_ppi_channel);
			}
			break;
	}
}

int32_t adc_read_vbat_raw(void)
{
	int32_t reading = 0;
	nrfx_err_t ret_err;
    nrf_saadc_value_t adc_result = 0;

	if (adc_current_state == ADC_DONE) adc_current_state = ADC_IDLE;

	for (uint8_t wait_count = 0; wait_count < 10; ++wait_count)
	{
		if ((adc_current_state == ADC_IDLE) && (!nrfx_saadc_is_busy())) break;
		nrf_delay_ms(1);
		if (adc_current_state == ADC_DONE) adc_current_state = ADC_IDLE;
	}

	if ((adc_current_state != ADC_IDLE)||(nrfx_saadc_is_busy())) return 0;
	
	adc_current_state = ADC_ONESHOT;
    nrf_gpio_cfg_output(PIN_VBAT_ON);
    nrf_gpio_pin_set(PIN_VBAT_ON);
	nrf_delay_ms(5);
    ret_err = nrfx_saadc_channel_uninit(0);
	if (ret_err == NRFX_SUCCESS) ret_err = nrfx_saadc_channel_init(0,&adc_channel_vbat);
	for (uint8_t i=0; i<ADC_AVG_SAMPLES; ++i)
	{
		if (ret_err == NRFX_SUCCESS) ret_err = nrfx_saadc_sample_convert(0,&adc_result);
		reading += adc_result;
    }
    adc_current_state = ADC_IDLE;
    nrf_gpio_pin_clear(PIN_VBAT_ON);
	if (reading<0)reading=0;
    if (ret_err == NRFX_SUCCESS) return reading;
	else return 0;
}

uint16_t adc_read_vbat_mv(void)
{
	int32_t reading = adc_read_vbat_raw();
    return adc_vbat_raw_to_mv(reading);
}

int32_t adc_read_vbat_raw_fresh(void)
{
	int32_t reading = 0;
	nrfx_err_t ret_err;
    nrf_saadc_value_t adc_result = 0;

	nrfx_saadc_abort();
	nrfx_saadc_uninit();
	ret_err = nrfx_saadc_init(&adc_config, adc_event_handler);
	if (ret_err == NRFX_SUCCESS) ret_err = nrfx_saadc_channel_init(0,&adc_channel_vbat);

    nrf_gpio_cfg_output(PIN_VBAT_ON);
    nrf_gpio_pin_set(PIN_VBAT_ON);
	nrf_delay_ms(5);

	for (uint8_t i=0; i<ADC_AVG_SAMPLES; ++i)
	{
		if (ret_err == NRFX_SUCCESS) ret_err = nrfx_saadc_sample_convert(0,&adc_result);
		reading += adc_result;
	}

    nrf_gpio_pin_clear(PIN_VBAT_ON);
	nrfx_saadc_abort();
	nrfx_saadc_uninit();
	adc_init();

	if (reading < 0) reading = 0;
    if (ret_err == NRFX_SUCCESS) return reading;
	return 0;
}

uint16_t adc_read_vbat_mv_fresh(void)
{
	int32_t reading = adc_read_vbat_raw_fresh();
    return adc_vbat_raw_to_mv(reading);
}

uint16_t adc_vbat_raw_to_mv(int32_t reading)
{
	if (reading < 0) reading = 0;
	reading /= ADC_AVG_SAMPLES;
    return (uint16_t) (((reading) * ADC_SCALING_FACTOR * (600/8) * (VDIV_VBAT_R1 + VDIV_VBAT_R2))/(ADC_TOP / 8)/VDIV_VBAT_R2);
}

uint16_t adc_read_bank1_mv(void)
{
	int32_t reading = adc_read_bank1();
	return (uint16_t) (((reading) * ADC_SCALING_FACTOR * (600/8))/(ADC_TOP / 8));
}

uint16_t adc_read_bank2_mv(void)
{
	int32_t reading = adc_read_bank2();
	return (uint16_t) (((reading) * ADC_SCALING_FACTOR * (600/8))/(ADC_TOP / 8));
}
uint16_t adc_read_bank3_mv(void)
{
	int32_t reading = adc_read_bank3();
	return (uint16_t) (((reading) * ADC_SCALING_FACTOR * (600/8))/(ADC_TOP / 8));
}

uint16_t adc_read_vdd_mv(void)
{
	int32_t reading = 0;
	nrfx_err_t ret_err;
    nrf_saadc_value_t adc_result = 0;

	if ((adc_current_state != ADC_IDLE)||(nrfx_saadc_is_busy())) return 0;
	
	adc_current_state = ADC_ONESHOT;
    ret_err = nrfx_saadc_channel_uninit(0);
	if (ret_err == NRFX_SUCCESS) ret_err = nrfx_saadc_channel_init(0,&adc_channel_vdd);
	if (ret_err == NRFX_SUCCESS) ret_err = nrfx_saadc_sample_convert(0,&adc_result);
	reading = adc_result;
    adc_current_state = ADC_IDLE;
	if (reading<0)reading=0;
    if (ret_err == NRFX_SUCCESS) return (uint16_t) (((reading) * ADC_SCALING_FACTOR * (600/8))/(ADC_TOP / 8));
	else return 0;
}

int32_t adc_read_bank1(void)
{
	int32_t reading = 0;
    nrf_saadc_value_t adc_result = 0;

	if ((adc_current_state != ADC_IDLE)||(nrfx_saadc_is_busy())) return 0;
    nrf_gpio_cfg(PIN_FSR_CH0, GPIO_PIN_CNF_DIR_Input, GPIO_PIN_CNF_INPUT_Disconnect, GPIO_PIN_CNF_PULL_Disabled, GPIO_PIN_CNF_DRIVE_H0D1, GPIO_PIN_CNF_SENSE_Disabled);
	
	adc_current_state = ADC_ONESHOT;
    nrfx_saadc_channel_uninit(0);
	nrfx_saadc_channel_init(0,&adc_channel_bank1);
	nrfx_saadc_sample_convert(0,&adc_result);
    for (uint8_t i=0; i<ADC_AVG_SAMPLES; ++i) {
		nrfx_saadc_sample_convert(0,&adc_result);
		reading += adc_result;
	}
    adc_current_state = ADC_IDLE;
	if (reading<0)reading=0;
    return reading;
}

int32_t adc_read_bank2(void)
{
	int32_t reading = 0;
    nrf_saadc_value_t adc_result = 0;

	if ((adc_current_state != ADC_IDLE)||(nrfx_saadc_is_busy())) return 0;
    nrf_gpio_cfg(PIN_FSR_CH1, GPIO_PIN_CNF_DIR_Input, GPIO_PIN_CNF_INPUT_Disconnect, GPIO_PIN_CNF_PULL_Disabled, GPIO_PIN_CNF_DRIVE_H0D1, GPIO_PIN_CNF_SENSE_Disabled);
	
	adc_current_state = ADC_ONESHOT;
    nrfx_saadc_channel_uninit(0);
	nrfx_saadc_channel_init(0,&adc_channel_bank2);
	nrfx_saadc_sample_convert(0,&adc_result);
    for (uint8_t i=0; i<ADC_AVG_SAMPLES; ++i) {
		nrfx_saadc_sample_convert(0,&adc_result);
		reading += adc_result;
	}
    adc_current_state = ADC_IDLE;
	if (reading<0)reading=0;
    return reading;
}

int32_t adc_read_bank3(void)
{
	int32_t reading = 0;
	nrfx_err_t ret_err;
    nrf_saadc_value_t adc_result = 0;

	if ((adc_current_state != ADC_IDLE)||(nrfx_saadc_is_busy())) return 0;
    nrf_gpio_cfg(PIN_FSR_CH2, GPIO_PIN_CNF_DIR_Input, GPIO_PIN_CNF_INPUT_Disconnect, GPIO_PIN_CNF_PULL_Disabled, GPIO_PIN_CNF_DRIVE_H0D1, GPIO_PIN_CNF_SENSE_Disabled);
	
	adc_current_state = ADC_ONESHOT;
    nrfx_saadc_channel_uninit(0);
	nrfx_saadc_channel_init(0,&adc_channel_bank3);
	nrfx_saadc_sample_convert(0,&adc_result);
    for (uint8_t i=0; i<ADC_AVG_SAMPLES; ++i) {
		nrfx_saadc_sample_convert(0,&adc_result);
		reading += adc_result;
	}
    adc_current_state = ADC_IDLE;
	if (reading<0)reading=0;
    return reading;
}

uint32_t adc_generate_random(void)
{
	uint32_t return_val = 0;
	nrfx_err_t ret_err;
    nrf_saadc_value_t adc_result = 0;

	if ((adc_current_state != ADC_IDLE)||(nrfx_saadc_is_busy())) return 0;
	
	adc_current_state = ADC_ONESHOT;
    ret_err = nrfx_saadc_channel_uninit(0);
	if (ret_err == NRFX_SUCCESS) ret_err = nrfx_saadc_channel_init(0,&adc_channel_vdd);
	for (uint8_t i=0; i<32; ++i)
	{
		if (ret_err == NRFX_SUCCESS) ret_err = nrfx_saadc_sample_convert(0,&adc_result);
		return_val = (return_val<<1)|(adc_result & 0x0001);
	}
    adc_current_state = ADC_IDLE;
	return return_val;
}


int32_t adc_read_cap1(void)
{
	nrfx_err_t ret_err;
    nrf_saadc_value_t adc_result;
    nrf_saadc_value_t adc_result_low;
	int32_t count = 0;

	if ((adc_current_state != ADC_IDLE)||(nrfx_saadc_is_busy())) return 0;
    adc_current_state = ADC_ONESHOT;
    nrfx_saadc_channel_uninit(0);
    nrfx_saadc_channel_uninit(1);
	nrfx_saadc_channel_init(0,&adc_channel_cap1);
	nrfx_saadc_channel_init(1,&adc_channel_cap1_low);
	nrfx_saadc_sample_convert(1,&adc_result_low);
	nrfx_saadc_sample_convert(1,&adc_result_low);
	nrfx_saadc_sample_convert(1,&adc_result_low);
	//count += (adc_result-adc_result_low);
	nrfx_saadc_sample_convert(0,&adc_result);
	nrfx_saadc_sample_convert(0,&adc_result);
	nrfx_saadc_sample_convert(0,&adc_result);
	count += (adc_result-adc_result_low);
	nrfx_saadc_sample_convert(1,&adc_result_low);
	nrfx_saadc_sample_convert(1,&adc_result_low);
	nrfx_saadc_sample_convert(1,&adc_result_low);
	count += (adc_result-adc_result_low);
	nrfx_saadc_sample_convert(0,&adc_result);
	nrfx_saadc_sample_convert(0,&adc_result);
	nrfx_saadc_sample_convert(0,&adc_result);
	count += (adc_result-adc_result_low);
	nrfx_saadc_sample_convert(1,&adc_result_low);
	nrfx_saadc_sample_convert(1,&adc_result_low);
	nrfx_saadc_sample_convert(1,&adc_result_low);
	count += (adc_result-adc_result_low);
	/*nrfx_saadc_sample_convert(0,&adc_result);
	nrfx_saadc_sample_convert(0,&adc_result);
	nrfx_saadc_sample_convert(0,&adc_result);
	count += (adc_result-adc_result_low);
	nrfx_saadc_sample_convert(1,&adc_result_low);
	nrfx_saadc_sample_convert(1,&adc_result_low);
	nrfx_saadc_sample_convert(1,&adc_result_low);
	count += (adc_result-adc_result_low);
	nrfx_saadc_sample_convert(0,&adc_result);
	nrfx_saadc_sample_convert(0,&adc_result);
	nrfx_saadc_sample_convert(0,&adc_result);
	count += (adc_result-adc_result_low);
	nrfx_saadc_sample_convert(1,&adc_result_low);
	nrfx_saadc_sample_convert(1,&adc_result_low);
	nrfx_saadc_sample_convert(1,&adc_result_low);
	count += (adc_result-adc_result_low);*/
    adc_current_state = ADC_IDLE;
	return count;
}

int32_t adc_read_cap2(void)
{
	nrfx_err_t ret_err;
    nrf_saadc_value_t adc_result;
    nrf_saadc_value_t adc_result_low;
	int32_t count = 0;

	if ((adc_current_state != ADC_IDLE)||(nrfx_saadc_is_busy())) return 0;
    adc_current_state = ADC_ONESHOT;
    nrfx_saadc_channel_uninit(0);
    nrfx_saadc_channel_uninit(1);
	nrfx_saadc_channel_init(0,&adc_channel_cap2);
	nrfx_saadc_channel_init(1,&adc_channel_cap2_low);
	nrfx_saadc_sample_convert(1,&adc_result_low);
	nrfx_saadc_sample_convert(1,&adc_result_low);
	nrfx_saadc_sample_convert(1,&adc_result_low);
	//count += (adc_result-adc_result_low);
	nrfx_saadc_sample_convert(0,&adc_result);
	nrfx_saadc_sample_convert(0,&adc_result);
	nrfx_saadc_sample_convert(0,&adc_result);
	count += (adc_result-adc_result_low);
	nrfx_saadc_sample_convert(1,&adc_result_low);
	nrfx_saadc_sample_convert(1,&adc_result_low);
	nrfx_saadc_sample_convert(1,&adc_result_low);
	count += (adc_result-adc_result_low);
	nrfx_saadc_sample_convert(0,&adc_result);
	nrfx_saadc_sample_convert(0,&adc_result);
	nrfx_saadc_sample_convert(0,&adc_result);
	count += (adc_result-adc_result_low);
	nrfx_saadc_sample_convert(1,&adc_result_low);
	nrfx_saadc_sample_convert(1,&adc_result_low);
	nrfx_saadc_sample_convert(1,&adc_result_low);
	count += (adc_result-adc_result_low);
	/*nrfx_saadc_sample_convert(0,&adc_result);
	nrfx_saadc_sample_convert(0,&adc_result);
	nrfx_saadc_sample_convert(0,&adc_result);
	count += (adc_result-adc_result_low);
	nrfx_saadc_sample_convert(1,&adc_result_low);
	nrfx_saadc_sample_convert(1,&adc_result_low);
	nrfx_saadc_sample_convert(1,&adc_result_low);
	count += (adc_result-adc_result_low);
	nrfx_saadc_sample_convert(0,&adc_result);
	nrfx_saadc_sample_convert(0,&adc_result);
	nrfx_saadc_sample_convert(0,&adc_result);
	count += (adc_result-adc_result_low);
	nrfx_saadc_sample_convert(1,&adc_result_low);
	nrfx_saadc_sample_convert(1,&adc_result_low);
	nrfx_saadc_sample_convert(1,&adc_result_low);
	count += (adc_result-adc_result_low);*/
    adc_current_state = ADC_IDLE;
	return count;
}
