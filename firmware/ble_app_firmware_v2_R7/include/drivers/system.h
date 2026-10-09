/*===========================================
//
// system.h
// Written by Alex Gilmour
// Copyright (c) 2024, CarbonCircuits
// All rights reserved.
//
//=========================================*/

// MCU-level services: pin setup, the device clock, watchdog, sleeping, reset
// and power-off.

#ifndef SYSTEM_H_
#define SYSTEM_H_

#include <stdint.h>
#include "app_util_platform.h"

#define SYSTEM_AHB_FREQ		(64000000U)

// Sets code protection (if enabled), then starts the clock tick and watchdog
// and configures every pin.
void system_init(void);

// ---- device clock ------------------------------------------------------------
// Starts near zero at boot (flash_init resumes it from the newest stored record,
// if any) and can be set with ;CT. 5 ms resolution. Every timeout in the
// firmware runs off this same clock.
uint64_t system_time_ms(void);
uint32_t system_time_sec(void);
void system_set_time(uint32_t time_s);

// ---- waiting -----------------------------------------------------------------
// The idle step every wait loop calls: kicks the watchdog, runs the idle hook,
// flushes logs, then sleeps the CPU until the next interrupt.
void system_sleep(void);
// The hook is how received commands get served while the firmware waits; main
// registers msg_process_packet.
void system_set_idle_hook(void (*hook)(void));
// Wait while calling system_sleep() (so commands are served during the wait).
void system_wait_for_ms(uint32_t ms);
// Wait without running the idle hook.
void system_wait_for_ms_no_bg(uint32_t ms);
// Busy-wait for roughly this many CPU cycles.
void system_delay_cycles(int32_t cycles);

// Free-running CPU cycle counter (DWT CYCCNT) for microsecond-resolution
// profiling. Wraps every ~67s; take uint32 deltas.
uint32_t system_cycles(void);
#define SYSTEM_CYCLES_PER_US	(SYSTEM_AHB_FREQ / 1000000U)

#define safe_disable_interrupt()	CRITICAL_REGION_ENTER()
#define safe_enable_interrupt()		CRITICAL_REGION_EXIT()

// ---- charger (BQ24210) status lines, nonzero when asserted -------------------
uint8_t system_charger_power_good(void);	// PG: sitting on the charging puck
uint8_t system_charger_charging(void);		// CHG: charge in progress

// ---- reset and power-off -----------------------------------------------------
// Resets into the bootloader's DFU mode.
void system_reboot(void);
// SEN-106: firmware UVLO. System OFF (~0.3 uA), wake only via the charger PG
// line (GPIO sense) or reset. Does not return.
void system_enter_deep_shutdown(void);

void app_error_fault_handler(uint32_t id, uint32_t pc, uint32_t info);

#endif // SYSTEM_H_
