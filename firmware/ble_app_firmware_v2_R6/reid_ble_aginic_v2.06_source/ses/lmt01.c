/*===========================================
//
// lmt01.c
// Written by Alex Gilmour
// 02/11/2019
// Copyright (c) 2019, CarbonCircuits
// All rights reserved.
//
//=========================================*/

#include "lmt01.h"
#include "i2c.h"
#include "system.h"
#include "gpio.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
#include "app_pwm.h"

#include "nrf_drv_ppi.h"
#include "nrf_drv_timer.h"

#define LINEARISATION_TABLE_N	(21)
const int32_t lmt01_count_table[21] = {   29,  181,  338,  494,  651,  808, 966,1125,1284,1443,1602,1762,1923,2084,2245, 2407, 2569, 2731, 2893, 3057, 3218};
const int32_t lmt01_temp_table[21] =  {-5000,-4000,-3000,-2000,-1000,    0,1000,2000,3000,4000,5000,6000,7000,8000,9000,10000,11000,12000,13000,14000,15000};

int16_t lmt01_count_to_temp(uint16_t count);

#define LMT01_MEAS_CHANNEL		LPCOMP_PSEL_PSEL_AnalogInput5

static volatile uint16_t drive_voltage = 0;

APP_PWM_INSTANCE(PWM1,1);
static volatile bool pwm_ready_flag;

void pwm_ready_callback(uint32_t pwm_id)    // PWM callback function
{
	pwm_ready_flag = true;
}
/*
void lmt01_drive_start(uint8_t percent) {
	app_pwm_config_t pwm1_cfg =     {                                                   \
		.pins            = {PIN_LED, APP_PWM_NOPIN},                                    \
		.pin_polarity    = {APP_PWM_POLARITY_ACTIVE_HIGH, APP_PWM_POLARITY_ACTIVE_HIGH},\
		.num_of_channels = 1,                                                           \
		.period_us       = 100                                                          \
	};
	app_pwm_init(&PWM1,&pwm1_cfg,pwm_ready_callback);
	app_pwm_enable(&PWM1);
	while (app_pwm_channel_duty_set(&PWM1, 0, percent) == NRF_ERROR_BUSY);
}

void lmt01_drive_end(void) {
	while (app_pwm_channel_duty_set(&PWM1, 0, 0) == NRF_ERROR_BUSY);
	app_pwm_disable(&PWM1);
	app_pwm_uninit(&PWM1);
	nrf_gpio_cfg_output(PIN_LED);
    nrf_gpio_pin_clear(PIN_LED);
}*/

static const nrf_drv_timer_t m_timer_count = NRF_DRV_TIMER_INSTANCE(1);
nrf_ppi_channel_t ppi_channel_1;

void timer_handler_count(nrf_timer_event_t event_type, void * p_context) {
}

void timer_init(void) {
	ret_code_t err_code;

	// Configure TIMER1 for counting of low to high events on GPIO
	nrf_drv_timer_config_t timer_cfg = NRF_DRV_TIMER_DEFAULT_CONFIG;
	timer_cfg.bit_width = NRF_TIMER_BIT_WIDTH_32;
	timer_cfg.mode = NRF_TIMER_MODE_LOW_POWER_COUNTER;
	//timer_cfg.mode = NRF_TIMER_MODE_COUNTER;
	err_code = nrf_drv_timer_init(&m_timer_count, &timer_cfg, timer_handler_count);
	APP_ERROR_CHECK(err_code);
}

void ppi_init(void) {
	ret_code_t err_code;

	err_code = nrf_drv_ppi_init();
	APP_ERROR_CHECK(err_code);
	
	err_code = nrf_drv_ppi_channel_alloc(&ppi_channel_1);
	APP_ERROR_CHECK(err_code);
	
	uint32_t lpcomp_evt_addr_up				 = (uint32_t)&NRF_LPCOMP->EVENTS_CROSS;
	
	uint32_t timer_count_count_task_addr	= nrf_drv_timer_task_address_get(&m_timer_count, NRF_TIMER_TASK_COUNT);
	uint32_t timer_count_capture_task_addr	= nrf_drv_timer_task_address_get(&m_timer_count, NRF_TIMER_TASK_CAPTURE0);
	uint32_t timer_count_clear_task_addr	= nrf_drv_timer_task_address_get(&m_timer_count, NRF_TIMER_TASK_CLEAR);
	
	
	err_code = nrf_drv_ppi_channel_assign(ppi_channel_1,
										  lpcomp_evt_addr_up,
										  timer_count_count_task_addr); // Trigger timer count task when LPCOMP UP event is generated.

	APP_ERROR_CHECK(err_code);
	
	err_code = nrf_drv_ppi_channel_enable(ppi_channel_1);
	APP_ERROR_CHECK(err_code);
}

void lpcomp_init(void) {
	NRF_LPCOMP->PSEL = (LMT01_MEAS_CHANNEL << LPCOMP_PSEL_PSEL_Pos);
    NRF_LPCOMP->REFSEL = (LPCOMP_REFSEL_REFSEL_Ref1_8Vdd<< LPCOMP_REFSEL_REFSEL_Pos);
    //NRF_LPCOMP->REFSEL = (LPCOMP_REFSEL_REFSEL_ARef<< LPCOMP_REFSEL_REFSEL_Pos);
    //NRF_LPCOMP->EXTREFSEL = (LPCOMP_EXTREFSEL_EXTREFSEL_AnalogReference0<< LPCOMP_EXTREFSEL_EXTREFSEL_Pos);
    NRF_LPCOMP->ENABLE = (LPCOMP_ENABLE_ENABLE_Enabled << LPCOMP_ENABLE_ENABLE_Pos);
    NRF_LPCOMP->HYST = 0;
    //Start the comparator
    NRF_LPCOMP->TASKS_START=1;
    while(NRF_LPCOMP->EVENTS_READY==0); 
}

int8_t lmt01_init(void) {
	lpcomp_init();
	timer_init();
	ppi_init();
	//nrf_drv_ppi_channel_disable(ppi_channel_1);
	//NRF_COMP->TASKS_STOP=1;
    //NRF_COMP->ENABLE=0;
}

int8_t lmt01_deinit(void) {
	
}

int16_t lmt01_get_temp(uint32_t pin)
{
	//NRF_LPCOMP->PSEL = (LMT01_MEAS_CHANNEL << LPCOMP_PSEL_PSEL_Pos);
	/*NRF_COMP->MODE = 0x0000102;
	NRF_COMP->EXTREFSEL = 0;
	NRF_COMP->TH = 0x00000303;
	NRF_COMP->ENABLE = 2;
	NRF_COMP->TASKS_START=1;
	while(NRF_COMP->EVENTS_READY==0);
	nrf_drv_ppi_channel_enable(ppi_channel_1);*/
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
	//nrf_drv_timer_clear(&m_timer_count);
    
	//nrf_drv_ppi_channel_disable(ppi_channel_1);
	/*NRF_COMP->TASKS_STOP=1;
    NRF_COMP->ENABLE=0;*/
	return lmt01_count_to_temp(count/2);
}



int16_t lmt01_count_to_temp(uint16_t count) {
	static int32_t linear_value;
	
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
    //linear_value+=10;
	
	if (linear_value > 32767) linear_value = 32767;
	else if (linear_value < -32767) linear_value = -32767;
	return linear_value;
}


