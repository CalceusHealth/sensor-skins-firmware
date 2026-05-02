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

void system_sleep(void);
void system_shutdown(void);
void system_reboot(void);

void system_set_code_protection(void);

void app_error_fault_handler(uint32_t id, uint32_t pc, uint32_t info);

//#define system_delay_x(a)			{__NOP();__NOP();__NOP();}
#define system_delay_x(a)			{if (a >1) {int32_t __system_delay_value = a; while (--__system_delay_value > 0) {__NOP();};}}

#endif // SYSTEM_H_ 

