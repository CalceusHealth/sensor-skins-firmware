/*===========================================
//
// system.h
// Written by Alex Gilmour
// Copyright (c) 2024, CarbonCircuits
// All rights reserved.
//
//=========================================*/

#ifndef SYSTEM_H_
#define SYSTEM_H_

#include <stdint.h>
#include "gpio.h"
#include "configure_firmware.h"
#include "messaging.h"
#include "nrf_pwr_mgmt.h"

#define SYSTEM_AHB_FREQ		(64000000U)
#define SYSTEM_APB_FREQ		(64000000U)

void system_init(void);
void system_deinit(void);

//#define system_reset()	NVIC_SystemReset()

#define safe_disable_interrupt()	CRITICAL_REGION_ENTER()
#define safe_enable_interrupt()		CRITICAL_REGION_EXIT()
#define interrupts_enabled()		(__get_PRIMASK() == 0)

uint64_t system_time_ms(void);
uint32_t system_time_sec(void);


void system_set_time(uint32_t time_s);

void system_wait_for_ms(uint32_t ms);
void system_wait_for_ms_no_bg(uint32_t ms);
void system_delay_cycles(int32_t cycles);

void system_sleep(void);
void system_shutdown(void);
void system_reboot(void);

void system_set_code_protection(void);

void app_error_fault_handler(uint32_t id, uint32_t pc, uint32_t info);

//#define system_delay_x(a)			{__NOP();__NOP();__NOP();}
#define system_delay_x(a)			{if (a >1) {int32_t __system_delay_value = a; while (--__system_delay_value > 0) {__NOP();};}}

#endif // SYSTEM_H_ 

