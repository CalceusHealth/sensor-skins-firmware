/*===========================================
//
// lmt01.c
// Written by Alex Gilmour
// 02/11/2019
// Copyright (c) 2019, CarbonCircuits
// All rights reserved.
//
//=========================================*/

// LMT01 temperature sensors. A sensor is powered from its own GPIO and reports
// the temperature as a train of current pulses on the shared return line. The
// low-power comparator turns those into CROSS events, which PPI routes to
// TIMER1 in counter mode, so the pulses are counted without CPU involvement.

#include "drivers/lmt01.h"
#include "drivers/board.h"
#include "drivers/system.h"
#include "nrf_gpio.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"

#include "nrf_drv_ppi.h"
#include "nrf_drv_timer.h"

#define LINEARISATION_TABLE_N	(21)
static const int32_t lmt01_count_table[LINEARISATION_TABLE_N] = {   29,  181,  338,  494,  651,  808, 966,1125,1284,1443,1602,1762,1923,2084,2245, 2407, 2569, 2731, 2893, 3057, 3218};
static const int32_t lmt01_temp_table[LINEARISATION_TABLE_N] =  {-5000,-4000,-3000,-2000,-1000,    0,1000,2000,3000,4000,5000,6000,7000,8000,9000,10000,11000,12000,13000,14000,15000};

#define LMT01_MEAS_CHANNEL		LPCOMP_PSEL_PSEL_AnalogInput5

static const nrf_drv_timer_t m_timer_count = NRF_DRV_TIMER_INSTANCE(1);
static nrf_ppi_channel_t ppi_channel_1;
static const uint8_t power_pins[LMT01_NUM_SENSORS] = { PIN_TEMP_S0, PIN_TEMP_S1, PIN_TEMP_S2, PIN_TEMP_S3, PIN_TEMP_S4 };

static void timer_handler_count(nrf_timer_event_t event_type, void * p_context) {
}

static void lpcomp_init(void) {
	NRF_LPCOMP->PSEL = (LMT01_MEAS_CHANNEL << LPCOMP_PSEL_PSEL_Pos);
	NRF_LPCOMP->REFSEL = (LPCOMP_REFSEL_REFSEL_Ref1_8Vdd<< LPCOMP_REFSEL_REFSEL_Pos);
	NRF_LPCOMP->ENABLE = (LPCOMP_ENABLE_ENABLE_Enabled << LPCOMP_ENABLE_ENABLE_Pos);
	NRF_LPCOMP->HYST = 0;
	//Start the comparator
	NRF_LPCOMP->TASKS_START=1;
	while(NRF_LPCOMP->EVENTS_READY==0);
}

static void timer_init(void) {
	ret_code_t err_code;

	// Configure TIMER1 for counting of low to high events on GPIO
	nrf_drv_timer_config_t timer_cfg = NRF_DRV_TIMER_DEFAULT_CONFIG;
	timer_cfg.bit_width = NRF_TIMER_BIT_WIDTH_32;
	timer_cfg.mode = NRF_TIMER_MODE_LOW_POWER_COUNTER;
	err_code = nrf_drv_timer_init(&m_timer_count, &timer_cfg, timer_handler_count);
	APP_ERROR_CHECK(err_code);
}

static void ppi_init(void) {
	ret_code_t err_code;

	err_code = nrf_drv_ppi_init();
	APP_ERROR_CHECK(err_code);

	err_code = nrf_drv_ppi_channel_alloc(&ppi_channel_1);
	APP_ERROR_CHECK(err_code);

	// Trigger the timer count task on every LPCOMP crossing.
	err_code = nrf_drv_ppi_channel_assign(ppi_channel_1,
										  (uint32_t)&NRF_LPCOMP->EVENTS_CROSS,
										  nrf_drv_timer_task_address_get(&m_timer_count, NRF_TIMER_TASK_COUNT));
	APP_ERROR_CHECK(err_code);

	err_code = nrf_drv_ppi_channel_enable(ppi_channel_1);
	APP_ERROR_CHECK(err_code);
}

void lmt01_init(void) {
	lpcomp_init();
	timer_init();
	ppi_init();
}

// Linear interpolation of pulse count to 0.01 degC. Out-of-range counts return
// +32767 (too few pulses: no sensor) or -32767 (too many).
static int16_t lmt01_count_to_temp(uint16_t count) {
	int32_t linear_value;

	if (count <= lmt01_count_table[0]) {
		return 32767;
	} else if (count > lmt01_count_table[LINEARISATION_TABLE_N-1]) {
		return -32767;
	}

	linear_value = 0xFFFFFFFF;
	for (int32_t i=0; i<(LINEARISATION_TABLE_N-1); ++i)
	{
		if ((count>=lmt01_count_table[i]) && (count<=lmt01_count_table[i+1]))
		{
			linear_value = lmt01_temp_table[i] \
					   + (((count					- lmt01_count_table[i])		// partial difference xs from x1
					   *  (lmt01_temp_table[i+1]	- lmt01_temp_table[i]))	// y2-y1
					   /  (lmt01_count_table[i+1]	- lmt01_count_table[i]));	// x2-x1
			break;
		}
	}

	if (linear_value > 32767) linear_value = 32767;
	else if (linear_value < -32767) linear_value = -32767;
	return linear_value;
}

// Powers one sensor and counts its pulse train. Blocks for ~90 ms.
int16_t lmt01_get_temp(uint8_t sensor)
{
	uint32_t pin = power_pins[sensor];

	nrf_gpio_pin_clear(pin);
	nrf_drv_timer_clear(&m_timer_count);
	system_wait_for_ms(10);
	nrf_gpio_pin_set(pin);
	system_wait_for_ms(20);
	nrf_drv_timer_clear(&m_timer_count);
	system_wait_for_ms(60);
	uint32_t count = nrf_drv_timer_capture(&m_timer_count, NRF_TIMER_CC_CHANNEL0);
	nrf_gpio_pin_clear(pin);
	NRF_LOG_INFO("LMT01 count %u",count);
	NRF_LOG_FLUSH();
	return lmt01_count_to_temp(count/2); // the comparator fires on both edges of a pulse
}
