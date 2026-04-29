/*===========================================
//
// flash.h
// Written by Alex Gilmour
// Copyright (c) 2024, CarbonCircuits
// All rights reserved.
//
//=========================================*/

#ifndef FLASH_H_
#define FLASH_H_

#include <stdint.h>
#include "configure_firmware.h"
#include <nrfx.h>
#include "measure.h"

#define FLASH_DATA_SYNCED_MARKER (0)

typedef struct flash_sysdata_t
{
	uint32_t device_uid;
	uint32_t device_version;
} flash_sysdata_t;

extern volatile flash_sysdata_t flash_sysdata;

void flash_init(void);
void flash_deinit(void);

void flash_reset_all(void);
void flash_write_record(reid_ble_summary_packet_t* record);
reid_ble_summary_packet_t* flash_get_record(uint32_t req_index);

void flash_get_all_indices(uint32_t* low_index, uint32_t* high_index);
void flash_get_pending_indices(uint32_t* low_index, uint32_t* high_index);
void flash_mark_record_synced(uint32_t req_index);
void flash_mark_records_synced(uint32_t low_index, uint32_t high_index);

uint32_t flash_next_high_index(void);

#endif // FLASH_H_ 

