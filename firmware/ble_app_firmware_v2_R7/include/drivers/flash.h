/*===========================================
//
// flash.h
// Written by Alex Gilmour
// Copyright (c) 2024, CarbonCircuits
// All rights reserved.
//
//=========================================*/

// Legacy summary-record store in internal flash, served by the ;QA ;QP ;QR ;QM
// ;CS ;CM ;CE commands. Nothing in the current firmware writes records (the
// 5-minute summary writer was removed with ENABLE_FLASH_SUMMARY), so on a
// freshly flashed device every query reports an empty store.

#ifndef FLASH_H_
#define FLASH_H_

#include <stdint.h>

#define FLASH_DATA_SYNCED_MARKER (0)

typedef struct flash_sysdata_t
{
	uint64_t device_id;
	uint32_t device_version;
} flash_sysdata_t;

extern volatile flash_sysdata_t flash_sysdata;

// One stored record, and the binary blob returned by ;QR / ;QM.
#pragma pack(push,1)
typedef struct reid_ble_summary_packet_t {
	uint32_t time_s_start;
	uint32_t time_s_end;
	uint32_t index;
	uint16_t is_synced;
	uint16_t num_samples;
	uint16_t vdd_mv_min;
	uint16_t fsr_max[19];
	int16_t temp_max[5];
	struct { int16_t min; int16_t max; } cap_delta[6];	// north minus south, per pad
} reid_ble_summary_packet_t;
#pragma pack(pop)

_Static_assert(sizeof(reid_ble_summary_packet_t) == 90, "reid_ble_summary_packet_t is a wire and flash format");

void flash_init(void);

void flash_reset_all(void);
reid_ble_summary_packet_t* flash_get_record(uint32_t req_index);

void flash_get_all_indices(uint32_t* low_index, uint32_t* high_index);
void flash_get_pending_indices(uint32_t* low_index, uint32_t* high_index);
void flash_mark_record_synced(uint32_t req_index);
void flash_mark_records_synced(uint32_t low_index, uint32_t high_index);

#endif // FLASH_H_
