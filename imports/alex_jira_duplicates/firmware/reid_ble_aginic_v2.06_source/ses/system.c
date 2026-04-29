/*===========================================
//
// system.c
// Written by Alex Gilmour
// Copyright (c) 2024, CarbonCircuits
// All rights reserved.
//
//=========================================*/

#include "system.h"

#include "app_timer.h"
#include "nrf_delay.h"
#include "flash.h"
#include "nrf_nvmc.h"

#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

#define DELAY_CYCLES_LOOP_TIMING	7

static volatile uint64_t system_time_ticks; // incremented at SYSTEM_TIME_TICKS_RESOLUTION millisecond intervals
static void system_ticks_timer_handler(void * p_context);

#define DELAY_CYCLES_LOOP_TIMING			7
//#define SYSTEM_TIME_TICKS_RESOLUTION		MAIN_LOOP_TIME_MS
#define SYSTEM_TIME_TICKS_RESOLUTION		5

APP_TIMER_DEF(system_tick_timer);

void system_init(void)
{
	system_time_ticks = 0;
	ret_code_t err_code;

    err_code = app_timer_init();
    //APP_ERROR_CHECK(err_code); // is always NRF_SUCCESS
	
	err_code = app_timer_create(&system_tick_timer, APP_TIMER_MODE_REPEATED, system_ticks_timer_handler);
	if (err_code == NRF_SUCCESS) err_code = app_timer_start(system_tick_timer, APP_TIMER_TICKS(SYSTEM_TIME_TICKS_RESOLUTION), NULL);

	err_code = nrf_pwr_mgmt_init();
	//APP_ERROR_CHECK(err_code); // is always NRF_SUCCESS
	
	// disable brownout
	((NRF_POWER_Type*) 0x40000000UL)->POFCON = (POWER_POFCON_THRESHOLD_V17<<POWER_POFCON_THRESHOLD_Pos)|(POWER_POFCON_POF_Disabled << POWER_POFCON_POF_Pos);
}

void system_deinit(void)
{
	system_time_ticks = 0;
	app_timer_stop(system_tick_timer);
}

uint64_t system_time_ms(void)
{
	return system_time_ticks*1000/32768ull;
}

uint32_t system_time_sec(void)
{
	return (uint32_t) (system_time_ticks / 32768ull);
}


void system_set_time(uint32_t time_s) {
	system_time_ticks = ((uint64_t) time_s) * 32768ull;
}

void system_wait_for_ms(uint32_t ms)
{
	if (nrf_sdh_is_enabled()) {
		uint64_t timer = system_time_ms() + ms;
		while (timer > system_time_ms()) system_sleep();
	} else {
		nrf_delay_ms(ms);
	}
}

void system_wait_for_ms_no_bg(uint32_t ms)
{
	if (nrf_sdh_is_enabled()) {
		uint64_t timer = system_time_ms() + ms;
		while (timer > system_time_ms()) {
            #ifdef ENABLE_DEBUG
			NRF_LOG_PROCESS();
			#endif
			#ifdef ENABLE_SLEEP
			nrf_pwr_mgmt_run();
			#endif
		}
	} else {
		nrf_delay_ms(ms);
	}
}

static void system_ticks_timer_handler(void * p_context)
{
	system_time_ticks  += APP_TIMER_TICKS(SYSTEM_TIME_TICKS_RESOLUTION);
}

void HardFault_Handler(void)
{
	__disable_irq();
	
	#ifdef ENABLE_HARDFAULT_RECOVERY
	NVIC_SystemReset();
	#endif // ENABLE_HARDFAULT_RECOVERY
	while (1)
	{
		//SCB->SCR |= (SCB_SCR_SEVONPEND_Msk | SCB_SCR_SLEEPDEEP_Msk | SCB_SCR_SLEEPONEXIT_Msk);
		//__WFI();	// Put CPU into suspend mode
	}
	
}

void SVC_Handler(void)
{
}

void PendSV_Handler(void)
{
}

void Default_Handler(void)
{
}

void system_delay_cycles(int32_t cycles)
{
	__IO int32_t temp = cycles - DELAY_CYCLES_LOOP_TIMING;
	while (temp > 0) temp = temp - DELAY_CYCLES_LOOP_TIMING;
}

void system_sleep(void)
{
	msg_process_packet();
    #ifdef ENABLE_DEBUG
	NRF_LOG_PROCESS();
	#endif
	#ifdef ENABLE_SLEEP
	nrf_pwr_mgmt_run();
	#endif
}

void system_shutdown(void)
{
	#ifdef ENABLE_SHUTDOWN
	sd_power_system_off();  // Go to system-off mode (this function will not return; wakeup will cause a reset).
	while(1);
	#else
	return;
	#endif
}

void system_reboot(void)
{
	//sd_card_force_write_data();
	//ble_reid_force_disconnect();
	//sd_power_gpregret_set(0,BOOTLOADER_DFU_START);
	NRF_POWER->GPREGRET = BOOTLOADER_DFU_START;
	NVIC_SystemReset();
}

void system_set_code_protection(void)
{
	#ifndef ENABLE_DEBUG
    #ifdef ENABLE_CODE_PROTECT // Set Level 1 code protection
	if (NRF_UICR->APPROTECT == 0xFFFFFFFF)
	{
		system_delay_cycles(100000);
		nrf_nvmc_write_word((uint32_t)&(NRF_UICR->APPROTECT), 0xFFFFFF00);
		NVIC_SystemReset();
	}
	#endif // ENABLE_CODE_PROTECT
    #endif // ENABLE_DEBUG
}


void app_error_fault_handler(uint32_t id, uint32_t pc, uint32_t info)
{
	#ifdef ENABLE_HARDFAULT_RECOVERY
	NVIC_SystemReset();
	#endif // ENABLE_HARDFAULT_RECOVERY

	//while(1);
	return;
}