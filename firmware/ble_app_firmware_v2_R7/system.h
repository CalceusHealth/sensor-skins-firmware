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

#define BOOTLOADER_DFU_GPREGRET_MASK            (0xF8)      /**< Mask for GPGPREGRET bits used for the magic pattern written to GPREGRET register to signal between main app and DFU. */
#define BOOTLOADER_DFU_GPREGRET                 (0xB0)      /**< Magic pattern written to GPREGRET register to signal between main app and DFU. The 3 lower bits are assumed to be used for signalling purposes.*/
#define BOOTLOADER_DFU_START_BIT_MASK           (0x01)      /**< Bit mask to signal from main application to enter DFU mode using a buttonless service. */

#define BOOTLOADER_DFU_GPREGRET2_MASK           (0xF8)      /**< Mask for GPGPREGRET2 bits used for the magic pattern written to GPREGRET2 register to signal between main app and DFU. */
#define BOOTLOADER_DFU_GPREGRET2                (0xA8)      /**< Magic pattern written to GPREGRET2 register to signal between main app and DFU. The 3 lower bits are assumed to be used for signalling purposes.*/
#define BOOTLOADER_DFU_SKIP_CRC_BIT_MASK        (0x01)      /**< Bit mask to signal from main application that CRC-check is not needed for image verification. */

#define BOOTLOADER_DFU_START    (BOOTLOADER_DFU_GPREGRET | BOOTLOADER_DFU_START_BIT_MASK)      /**< Magic number to signal that bootloader should enter DFU mode because of signal from Buttonless DFU in main app.*/
#define BOOTLOADER_DFU_SKIP_CRC (BOOTLOADER_DFU_GPREGRET2 | BOOTLOADER_DFU_SKIP_CRC_BIT_MASK)  /**< Magic number to signal that CRC can be skipped due to low power modes.*/

#define SYSTEM_AHB_FREQ		(64000000U)
#define SYSTEM_APB_FREQ		(64000000U)

void system_init(void);
void system_deinit(void);

// SEN-106: firmware UVLO. System OFF (~0.3 uA), wake only via the charger PG
// line (GPIO sense) or reset. Does not return.
void system_enter_deep_shutdown(void);

// SEN-182: reset-reason capture + RAM "black box". RESETREAS is latched at
// system_init() (then cleared so each boot reports only its own cause) and a
// small record in .non_init RAM survives pin/soft resets but not a power loss,
// so the app can tell "reset pin pulled" from "supply dropped" after a field
// drop-out. Reported by ;QI (RST=/BOOT=/PREV*=).
#define SYSTEM_BB_EVENT_NONE				0
#define SYSTEM_BB_EVENT_PROTECTION_SLEEP	1	// low-battery recovery sleep entered
#define SYSTEM_BB_EVENT_SYSTEM_OFF			2	// firmware UVLO -> sd_power_system_off
#define SYSTEM_BB_EVENT_REBOOT_CMD			3	// ;CR

typedef struct system_blackbox_prev_t {
	uint8_t  valid;			// 1 = record below survived the last reset (magic + checksum OK)
	uint32_t boot_count;	// boots since the record was last lost
	uint32_t time_s;		// device clock (s) at the last update before the reset
	uint16_t vbat_mv;		// last averaged vbat
	uint16_t vdd_mv;		// last nRF rail
	uint8_t  batt_state;	// 'C' charging confirmed, 'K' otherwise
	uint8_t  event;			// last SYSTEM_BB_EVENT_* before the reset
	uint16_t event_vbat_mv;
	uint32_t event_time_s;
} system_blackbox_prev_t;

uint32_t system_reset_reason(void);					// NRF_POWER->RESETREAS as found at boot
uint32_t system_boot_count(void);
const system_blackbox_prev_t* system_blackbox_prev(void);
void system_blackbox_update(uint16_t vbat_mv, uint16_t vdd_mv, uint8_t batt_state);
void system_blackbox_event(uint8_t event);

void system_wdt_init(void);
void system_wdt_kick(void);

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

// Free-running CPU cycle counter (DWT CYCCNT) for microsecond-resolution
// profiling (system_time_ms only has ~5ms granularity). Wraps every ~67s;
// take uint32 deltas. Used to characterise measure_sensors() cost for the
// binary-stream sample-rate ceiling (SEN-53).
void system_cycle_counter_init(void);
uint32_t system_cycles(void);
#define SYSTEM_CYCLES_PER_US	(SYSTEM_AHB_FREQ / 1000000U)

void system_sleep(void);
void system_shutdown(void);
void system_reboot(void);

void system_set_code_protection(void);

void app_error_fault_handler(uint32_t id, uint32_t pc, uint32_t info);

//#define system_delay_x(a)			{__NOP();__NOP();__NOP();}
#define system_delay_x(a)			{if (a >1) {int32_t __system_delay_value = a; while (--__system_delay_value > 0) {__NOP();};}}

#endif // SYSTEM_H_ 

