/*===========================================
//
// system.c
// Written by Alex Gilmour
// Copyright (c) 2024, CarbonCircuits
// All rights reserved.
//
//=========================================*/

#include "system.h"

#include <stddef.h>
#include <string.h>
#include "app_timer.h"
#include "nrf_delay.h"
#include "flash.h"
#include "nrf_nvmc.h"
#include "nrf.h"

#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

#define DELAY_CYCLES_LOOP_TIMING	7

static volatile uint64_t system_time_ticks; // incremented at SYSTEM_TIME_TICKS_RESOLUTION millisecond intervals
static void system_ticks_timer_handler(void * p_context);

// ---- SEN-182: reset reason + RAM black box -----------------------------------
// The record lives in .non_init (not zeroed by the startup code; see
// flash_placement.xml). RAM keeps its contents through a pin reset, a soft
// reset and the bootloader's DFU detour, and loses them when VDD actually
// collapses. Together with RESETREAS that separates the two field failure
// classes: RESETPIN with a valid record = the reset pin was pulled while power
// stayed up; no reason bits with an invalid record = the supply dropped out.
#define SYSTEM_BB_MAGIC		0x42424F58ul	// "BBOX"

typedef struct system_blackbox_t {
	uint32_t magic;
	uint32_t boot_count;
	uint32_t time_s;
	uint16_t vbat_mv;
	uint16_t vdd_mv;
	uint8_t  batt_state;
	uint8_t  event;
	uint16_t event_vbat_mv;
	uint32_t event_time_s;
	uint32_t check;
} system_blackbox_t;

static system_blackbox_t system_bb __attribute__((section(".non_init")));
static system_blackbox_prev_t system_bb_prev = {0};
static uint32_t system_resetreas_at_boot = 0;

static uint32_t system_bb_checksum(const system_blackbox_t* bb)
{
	// FNV-1a over everything before .check; small and dependency-free.
	const uint8_t* p = (const uint8_t*) bb;
	uint32_t h = 2166136261ul;
	for (size_t i = 0; i < offsetof(system_blackbox_t, check); ++i) { h ^= p[i]; h *= 16777619ul; }
	return h;
}

static void system_bb_commit(void)
{
	system_bb.check = system_bb_checksum(&system_bb);
}

static void system_blackbox_init(void)
{
	if ((system_bb.magic == SYSTEM_BB_MAGIC) && (system_bb.check == system_bb_checksum(&system_bb))) {
		system_bb_prev.valid = 1;
		system_bb_prev.boot_count = system_bb.boot_count;
		system_bb_prev.time_s = system_bb.time_s;
		system_bb_prev.vbat_mv = system_bb.vbat_mv;
		system_bb_prev.vdd_mv = system_bb.vdd_mv;
		system_bb_prev.batt_state = system_bb.batt_state;
		system_bb_prev.event = system_bb.event;
		system_bb_prev.event_vbat_mv = system_bb.event_vbat_mv;
		system_bb_prev.event_time_s = system_bb.event_time_s;
		system_bb.boot_count++;
	} else {
		memset(&system_bb_prev, 0, sizeof(system_bb_prev));
		memset(&system_bb, 0, sizeof(system_bb));
		system_bb.magic = SYSTEM_BB_MAGIC;
		system_bb.boot_count = 1;
	}
	system_bb.time_s = 0;
	system_bb.event = SYSTEM_BB_EVENT_NONE;
	system_bb.event_vbat_mv = 0;
	system_bb.event_time_s = 0;
	system_bb_commit();
}

uint32_t system_reset_reason(void) { return system_resetreas_at_boot; }
uint32_t system_boot_count(void) { return system_bb.boot_count; }
const system_blackbox_prev_t* system_blackbox_prev(void) { return &system_bb_prev; }

void system_blackbox_update(uint16_t vbat_mv, uint16_t vdd_mv, uint8_t batt_state)
{
	system_bb.time_s = system_time_sec();
	system_bb.vbat_mv = vbat_mv;
	if (vdd_mv) system_bb.vdd_mv = vdd_mv;
	system_bb.batt_state = batt_state;
	system_bb_commit();
}

void system_blackbox_event(uint8_t event)
{
	system_bb.event = event;
	system_bb.event_vbat_mv = system_bb.vbat_mv;
	system_bb.event_time_s = system_time_sec();
	system_bb_commit();
}
// ------------------------------------------------------------------------------

#define DELAY_CYCLES_LOOP_TIMING			7
//#define SYSTEM_TIME_TICKS_RESOLUTION		MAIN_LOOP_TIME_MS
#define SYSTEM_TIME_TICKS_RESOLUTION		5

APP_TIMER_DEF(system_tick_timer);

void system_wdt_init(void)
{
	NRF_WDT->CONFIG = (WDT_CONFIG_HALT_Pause << WDT_CONFIG_HALT_Pos) |
	                  (WDT_CONFIG_SLEEP_Run  << WDT_CONFIG_SLEEP_Pos);
	NRF_WDT->CRV    = 60 * 32768 - 1;
	NRF_WDT->RREN   = WDT_RREN_RR0_Enabled << WDT_RREN_RR0_Pos;
	NRF_WDT->TASKS_START = 1;
}

void system_wdt_kick(void)
{
	NRF_WDT->RR[0] = WDT_RR_RR_Reload;
}

void system_cycle_counter_init(void)
{
	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
	DWT->CYCCNT = 0;
	DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

uint32_t system_cycles(void)
{
	return DWT->CYCCNT;
}

void system_init(void)
{
	// SEN-182: latch the reset cause before anything else touches it, then clear
	// the (sticky, write-1-to-clear) register so the next boot sees only its own
	// cause. SystemInit's errata-136 workaround has already run and leaves
	// RESETPIN intact. SoftDevice is not enabled yet, so direct access is legal.
	system_resetreas_at_boot = NRF_POWER->RESETREAS;
	NRF_POWER->RESETREAS = 0xFFFFFFFFul;
	system_blackbox_init();

	system_time_ticks = 0;
	system_cycle_counter_init();
	ret_code_t err_code;

    err_code = app_timer_init();
    //APP_ERROR_CHECK(err_code); // is always NRF_SUCCESS

	err_code = app_timer_create(&system_tick_timer, APP_TIMER_MODE_REPEATED, system_ticks_timer_handler);
	if (err_code == NRF_SUCCESS) err_code = app_timer_start(system_tick_timer, APP_TIMER_TICKS(SYSTEM_TIME_TICKS_RESOLUTION), NULL);

	err_code = nrf_pwr_mgmt_init();
	//APP_ERROR_CHECK(err_code); // is always NRF_SUCCESS

	// disable brownout
	((NRF_POWER_Type*) 0x40000000UL)->POFCON = (POWER_POFCON_THRESHOLD_V17<<POWER_POFCON_THRESHOLD_Pos)|(POWER_POFCON_POF_Disabled << POWER_POFCON_POF_Pos);

	system_wdt_init();
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
	system_wdt_kick();
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

// SEN-106: firmware UVLO -- the last barrier before unrecoverable deep
// discharge (bare cell with no PCM, board UVLO provision R58 unfitted, POF
// disabled above). Arms the charger PG line (active low, externally pulled up
// to VBAT) as the System OFF wake source, then powers everything down
// (~0.3 uA; no BLE at all). Wake = puck contact or SWD reset. System OFF wake
// is a full reset, so boot lands in the normal protection logic: recovery
// sleep on the charger until the debounced 3450 mV exit.
void system_enter_deep_shutdown(void)
{
	system_blackbox_event(SYSTEM_BB_EVENT_SYSTEM_OFF); // SEN-182: visible via ;QI after the PG wake
	nrf_gpio_cfg_sense_input(PIN_BQ_PG, NRF_GPIO_PIN_NOPULL, NRF_GPIO_PIN_SENSE_LOW);
	if (nrf_sdh_is_enabled()) {
		(void) sd_power_system_off();
	} else {
		NRF_POWER->SYSTEMOFF = 1;
	}
	while (1) { __WFE(); } // not reached; debugger-emulated System OFF backstop
}

void system_reboot(void)
{
	//sd_card_force_write_data();
	//ble_reid_force_disconnect();
	//sd_power_gpregret_set(0,BOOTLOADER_DFU_START);
	system_blackbox_event(SYSTEM_BB_EVENT_REBOOT_CMD); // SEN-182
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