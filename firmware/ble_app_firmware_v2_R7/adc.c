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
    .gain       = FSR_ADC_GAIN,                   \
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
    .gain       = FSR_ADC_GAIN,                   \
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
    .gain       = FSR_ADC_GAIN,                   \
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

// ---- SEN-58 step 2: EasyDMA bank scan -------------------------------------
// The 3 FSR bank channels are read in ONE DMA scan per call instead of 3
// separate blocking single-channel reads, eliminating the per-conversion driver
// overhead. measure_sensors() brackets its FSR mux loop with adc_banks_begin()/
// adc_banks_end(); within that window adc_read_bank1() runs the scan + caches
// all 3 results, and adc_read_bank2()/bank3() return the cache (they are always
// called immediately after bank1 in measure.c). Scale is preserved: the scan is
// summed ADC_AVG_SAMPLES times, matching the old "sum of N samples" per bank.
#define ADC_BANK_SCAN_TIMEOUT 200000u
static nrf_saadc_value_t bank_scan_buf[3];
static int32_t bank_cache[3];

void adc_banks_begin(void)
{
	// Configure channels 0/1/2 = bank1/2/3 for scan mode (active_channels == 3).
	nrf_gpio_cfg(PIN_FSR_CH0, GPIO_PIN_CNF_DIR_Input, GPIO_PIN_CNF_INPUT_Disconnect, GPIO_PIN_CNF_PULL_Disabled, GPIO_PIN_CNF_DRIVE_H0D1, GPIO_PIN_CNF_SENSE_Disabled);
	nrf_gpio_cfg(PIN_FSR_CH1, GPIO_PIN_CNF_DIR_Input, GPIO_PIN_CNF_INPUT_Disconnect, GPIO_PIN_CNF_PULL_Disabled, GPIO_PIN_CNF_DRIVE_H0D1, GPIO_PIN_CNF_SENSE_Disabled);
	nrf_gpio_cfg(PIN_FSR_CH2, GPIO_PIN_CNF_DIR_Input, GPIO_PIN_CNF_INPUT_Disconnect, GPIO_PIN_CNF_PULL_Disabled, GPIO_PIN_CNF_DRIVE_H0D1, GPIO_PIN_CNF_SENSE_Disabled);
	nrfx_saadc_channel_uninit(0);
	nrfx_saadc_channel_uninit(1);
	nrfx_saadc_channel_uninit(2);
	nrfx_saadc_channel_init(0,&adc_channel_bank1);
	nrfx_saadc_channel_init(1,&adc_channel_bank2);
	nrfx_saadc_channel_init(2,&adc_channel_bank3);
	adc_current_state = ADC_IDLE;
}

void adc_banks_end(void)
{
	// Release the scan channels so the cap/vbat single-channel reads behave as
	// before (channel 0 is left for the next read to reconfigure).
	nrfx_saadc_channel_uninit(1);
	nrfx_saadc_channel_uninit(2);
	adc_current_state = ADC_IDLE;
}

static void adc_banks_scan(void)
{
	bank_cache[0] = 0; bank_cache[1] = 0; bank_cache[2] = 0;
	for (uint8_t s=0; s<ADC_AVG_SAMPLES; ++s) {
		uint32_t to;
		adc_current_state = ADC_RUNNING;
		if (nrfx_saadc_buffer_convert(bank_scan_buf, 3) != NRFX_SUCCESS) { adc_current_state = ADC_IDLE; return; }
		// buffer_convert triggered TASKS_START; wait STARTED, then SAMPLE the scan.
		to = 0; while ((nrf_saadc_event_check(NRF_SAADC_EVENT_STARTED) == 0) && (++to < ADC_BANK_SCAN_TIMEOUT)) {}
		if (nrfx_saadc_sample() != NRFX_SUCCESS) { nrfx_saadc_abort(); adc_current_state = ADC_IDLE; return; }
		// handler sets ADC_DONE on END (buffer of 3 filled = one scan of 3 channels).
		to = 0; while ((adc_current_state != ADC_DONE) && (++to < ADC_BANK_SCAN_TIMEOUT)) {}
		if (adc_current_state != ADC_DONE) { nrfx_saadc_abort(); adc_current_state = ADC_IDLE; return; }
		adc_current_state = ADC_IDLE;
		bank_cache[0] += bank_scan_buf[0];
		bank_cache[1] += bank_scan_buf[1];
		bank_cache[2] += bank_scan_buf[2];
	}
	if (bank_cache[0]<0) bank_cache[0]=0;
	if (bank_cache[1]<0) bank_cache[1]=0;
	if (bank_cache[2]<0) bank_cache[2]=0;
}

int32_t adc_read_bank1(void)
{
	if ((adc_current_state != ADC_IDLE)||(nrfx_saadc_is_busy())) return 0;
	adc_banks_scan();           // one DMA scan of all 3 banks -> bank_cache[0..2]
	return bank_cache[0];
}

// bank2/bank3 return the cache filled by the preceding bank1 scan (measure.c
// always calls bank1 first in each trio). No extra conversion.
int32_t adc_read_bank2(void) { return bank_cache[1]; }
int32_t adc_read_bank3(void) { return bank_cache[2]; }

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
