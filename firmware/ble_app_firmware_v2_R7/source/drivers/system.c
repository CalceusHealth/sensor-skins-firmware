/*===========================================
//
// system.c
// Written by Alex Gilmour
// Copyright (c) 2024, CarbonCircuits
// All rights reserved.
//
//=========================================*/

#include "drivers/system.h"
#include "drivers/board.h"
#include "configure_firmware.h"

#include "app_timer.h"
#include "nrf_delay.h"
#include "nrf_gpio.h"
#include "nrf_nvmc.h"
#include "nrf.h"
#include "nrf_pwr_mgmt.h"
#include "nrf_sdh.h"
#include "nrf_soc.h"

#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

// Written to GPREGRET before a reset to make the bootloader enter DFU mode
// (the buttonless-DFU magic pattern 0xB0 plus the "start DFU" bit).
#define BOOTLOADER_DFU_START				(0xB1)

#define DELAY_CYCLES_LOOP_TIMING			7
#define SYSTEM_TIME_TICKS_RESOLUTION		5	// ms between timebase interrupts

static volatile uint64_t system_time_ticks; // in 1/32768 s, advanced every SYSTEM_TIME_TICKS_RESOLUTION ms
static void (*system_idle_hook)(void);

APP_TIMER_DEF(system_tick_timer);

// ---- pins --------------------------------------------------------------------

// Side-detect strap (pulled to ground on RHS boards). Read at boot but not used
// yet: the side is still chosen at build time by REID_LHS / REID_RHS.
static uint8_t boards_is_lhs = 0;

static const uint8_t analog_pins[] = { PIN_FSR_CH0, PIN_FSR_CH1, PIN_FSR_CH2, PIN_CAP_CH0, PIN_CAP_CH1 };
static const uint8_t mux_select_pins[] = { PIN_FSR_S0, PIN_FSR_S1, PIN_FSR_S2, PIN_CAP_S0, PIN_CAP_S1, PIN_CAP_S2 };
static const uint8_t temp_power_pins[] = { PIN_TEMP_S0, PIN_TEMP_S1, PIN_TEMP_S2, PIN_TEMP_S3, PIN_TEMP_S4 };

static void cfg_analog_input(uint32_t pin, uint32_t drive)
{
	nrf_gpio_cfg(pin, GPIO_PIN_CNF_DIR_Input, GPIO_PIN_CNF_INPUT_Disconnect, GPIO_PIN_CNF_PULL_Disabled, drive, GPIO_PIN_CNF_SENSE_Disabled);
}

static void cfg_output(uint32_t pin, uint32_t pull, uint32_t drive)
{
	nrf_gpio_cfg(pin, GPIO_PIN_CNF_DIR_Output, GPIO_PIN_CNF_INPUT_Disconnect, pull, drive, GPIO_PIN_CNF_SENSE_Disabled);
}

static void pins_init(void)
{
	nrf_gpio_cfg_input(PIN_IS_LHS,	NRF_GPIO_PIN_PULLUP);

	for (uint8_t i = 0; i < sizeof(analog_pins); ++i) cfg_analog_input(analog_pins[i], GPIO_PIN_CNF_DRIVE_H0D1);

	// mux select lines: all low = input 0, which has no sensor
	for (uint8_t i = 0; i < sizeof(mux_select_pins); ++i) cfg_output(mux_select_pins[i], GPIO_PIN_CNF_PULL_Disabled, GPIO_PIN_CNF_DRIVE_H0H1);
	for (uint8_t i = 0; i < sizeof(mux_select_pins); ++i) nrf_gpio_pin_clear(mux_select_pins[i]);

	// each LMT01 is powered from its own pin; they share the TEMP_COM return
	for (uint8_t i = 0; i < sizeof(temp_power_pins); ++i) {
		cfg_output(temp_power_pins[i], GPIO_PIN_CNF_PULL_Disabled, GPIO_PIN_CNF_DRIVE_D0H1);
		nrf_gpio_pin_clear(temp_power_pins[i]);
	}
	cfg_analog_input(PIN_TEMP_COM, GPIO_PIN_CNF_DRIVE_D0S1);

	// battery divider, gated by VBAT_ON
	cfg_analog_input(PIN_VBAT, GPIO_PIN_CNF_DRIVE_H0D1);
	nrf_gpio_cfg_output(PIN_VBAT_ON);
	nrf_gpio_pin_clear(PIN_VBAT_ON);

	nrf_gpio_cfg_input(PIN_BQ_CHG,	NRF_GPIO_PIN_NOPULL);
	nrf_gpio_cfg_input(PIN_BQ_PG,	NRF_GPIO_PIN_NOPULL);

	nrf_gpio_cfg_input(PIN_SDA,	NRF_GPIO_PIN_PULLUP);
	nrf_gpio_cfg_input(PIN_SCL,	NRF_GPIO_PIN_PULLUP);

	// mux and FSR divider power, active low: off until a measurement
	cfg_output(PIN_MUX_ON, GPIO_PIN_CNF_PULL_Pullup, GPIO_PIN_CNF_DRIVE_H0H1);
	nrf_gpio_pin_set(PIN_MUX_ON);

	if (nrf_gpio_pin_read(PIN_IS_LHS)) boards_is_lhs = 1;
	nrf_gpio_cfg_input(PIN_IS_LHS,	NRF_GPIO_PIN_PULLDOWN);
}

// The charger's status outputs are open-drain, active low.
uint8_t system_charger_charging(void)
{
	return nrf_gpio_pin_read(PIN_BQ_CHG) ? 0 : 0xFF;
}

uint8_t system_charger_power_good(void)
{
	return nrf_gpio_pin_read(PIN_BQ_PG) ? 0 : 0xFF;
}

// ---- start-up ----------------------------------------------------------------

// Sets level 1 readback protection the first time the device boots. Compiled
// to nothing while ENABLE_DEBUG is defined, so that debug builds stay readable.
static void set_code_protection(void)
{
	#if defined(ENABLE_CODE_PROTECT) && !defined(ENABLE_DEBUG)
	if (NRF_UICR->APPROTECT == 0xFFFFFFFF)
	{
		system_delay_cycles(100000);
		nrf_nvmc_write_word((uint32_t)&(NRF_UICR->APPROTECT), 0xFFFFFF00);
		NVIC_SystemReset();
	}
	#endif
}

// 60 s watchdog; keeps running while the CPU sleeps, pauses under the debugger.
static void wdt_init(void)
{
	NRF_WDT->CONFIG = (WDT_CONFIG_HALT_Pause << WDT_CONFIG_HALT_Pos) |
	                  (WDT_CONFIG_SLEEP_Run  << WDT_CONFIG_SLEEP_Pos);
	NRF_WDT->CRV    = 60 * 32768 - 1;
	NRF_WDT->RREN   = WDT_RREN_RR0_Enabled << WDT_RREN_RR0_Pos;
	NRF_WDT->TASKS_START = 1;
}

static void system_ticks_timer_handler(void * p_context)
{
	system_time_ticks  += APP_TIMER_TICKS(SYSTEM_TIME_TICKS_RESOLUTION);
}

void system_init(void)
{
	set_code_protection();

	system_time_ticks = 0;

	// cycle counter for system_cycles()
	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
	DWT->CYCCNT = 0;
	DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

	ret_code_t err_code;

	err_code = app_timer_init();
	//APP_ERROR_CHECK(err_code); // is always NRF_SUCCESS

	err_code = app_timer_create(&system_tick_timer, APP_TIMER_MODE_REPEATED, system_ticks_timer_handler);
	if (err_code == NRF_SUCCESS) err_code = app_timer_start(system_tick_timer, APP_TIMER_TICKS(SYSTEM_TIME_TICKS_RESOLUTION), NULL);

	err_code = nrf_pwr_mgmt_init();
	//APP_ERROR_CHECK(err_code); // is always NRF_SUCCESS

	// disable brownout
	NRF_POWER->POFCON = (POWER_POFCON_THRESHOLD_V17<<POWER_POFCON_THRESHOLD_Pos)|(POWER_POFCON_POF_Disabled << POWER_POFCON_POF_Pos);

	wdt_init();
	pins_init();
}

// ---- device clock ------------------------------------------------------------

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

uint32_t system_cycles(void)
{
	return DWT->CYCCNT;
}

// ---- waiting -----------------------------------------------------------------

void system_set_idle_hook(void (*hook)(void))
{
	system_idle_hook = hook;
}

void system_sleep(void)
{
	NRF_WDT->RR[0] = WDT_RR_RR_Reload;
	if (system_idle_hook) system_idle_hook();
	#ifdef ENABLE_DEBUG
	NRF_LOG_PROCESS();
	#endif
	#ifdef ENABLE_SLEEP
	nrf_pwr_mgmt_run();
	#endif
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

void system_delay_cycles(int32_t cycles)
{
	__IO int32_t temp = cycles - DELAY_CYCLES_LOOP_TIMING;
	while (temp > 0) temp = temp - DELAY_CYCLES_LOOP_TIMING;
}

// ---- reset and power-off -----------------------------------------------------

void system_reboot(void)
{
	NRF_POWER->GPREGRET = BOOTLOADER_DFU_START;
	NVIC_SystemReset();
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
	nrf_gpio_cfg_sense_input(PIN_BQ_PG, NRF_GPIO_PIN_NOPULL, NRF_GPIO_PIN_SENSE_LOW);
	if (nrf_sdh_is_enabled()) {
		(void) sd_power_system_off();
	} else {
		NRF_POWER->SYSTEMOFF = 1;
	}
	while (1) { __WFE(); } // not reached; debugger-emulated System OFF backstop
}

// ---- fault handling ----------------------------------------------------------

void HardFault_Handler(void)
{
	__disable_irq();

	#ifdef ENABLE_HARDFAULT_RECOVERY
	NVIC_SystemReset();
	#endif // ENABLE_HARDFAULT_RECOVERY
	while (1)
	{
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

void app_error_fault_handler(uint32_t id, uint32_t pc, uint32_t info)
{
	#ifdef ENABLE_HARDFAULT_RECOVERY
	NVIC_SystemReset();
	#endif // ENABLE_HARDFAULT_RECOVERY
}
