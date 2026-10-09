/*===========================================
//
// flash.c
// Written by Alex Gilmour
// Copyright (c) 2024, CarbonCircuits
// All rights reserved.
//
//=========================================*/

#include "drivers/flash.h"
#include <string.h>
#include "configure_firmware.h"
#include "drivers/system.h"
#include "drivers/ble_reid.h"
#include "nrf.h"

// Records live in FLASH_NUM_PAGES pages reserved inside the application image
// (flash_measurements below), so a DFU also wipes them. A record with index n
// is stored in slot n modulo the store size; writes go through raw NVMC with
// interrupts off, which is why callers drop the BLE link first.
#define FLASH_PAGE_SIZE		(0x1000)
#define FLASH_NUM_PAGES		(10)
#define FLASH_PAGE_RECORDS	(FLASH_PAGE_SIZE / sizeof(reid_ble_summary_packet_t))

#define RECORD_UNUSED		(0xFFFFFFFF)	// time/index fields of an erased slot

// One erased page: FLASH_PAGE_SIZE / 2 words of 0xFFFF.
#define FF8		0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF
#define FF64	FF8, FF8, FF8, FF8, FF8, FF8, FF8, FF8
#define FF512	FF64, FF64, FF64, FF64, FF64, FF64, FF64, FF64
#define BLANK_PAGE	{{ FF512, FF512, FF512, FF512 }}
_Static_assert(FLASH_PAGE_SIZE / 2 == 4 * 512, "BLANK_PAGE must fill exactly one page");

volatile flash_sysdata_t flash_sysdata __attribute__((used)) =
{
	.device_id				= 0xFFFFFFFFFFFFFFFFull,
	.device_version			= DEVICE_FW_VERSION
};

typedef union flash_records_page_t
{
	uint16_t		data[FLASH_PAGE_SIZE/2];
	reid_ble_summary_packet_t	records[FLASH_PAGE_RECORDS];
} flash_records_page_t;

const __attribute__((used)) uint8_t copyright_message[] /*__attribute__((used))*/ = "   Written by Alex Gilmour. Copyright (c) 2025 Reid Technologies. All rights reserved.   ";

const volatile __attribute__((aligned(FLASH_PAGE_SIZE),used)) flash_records_page_t flash_measurements[FLASH_NUM_PAGES] = \
{BLANK_PAGE,BLANK_PAGE,BLANK_PAGE,BLANK_PAGE,BLANK_PAGE,\
BLANK_PAGE,BLANK_PAGE,BLANK_PAGE,BLANK_PAGE,BLANK_PAGE};

static void flash_erase_generic(uint16_t* flash);	// erases a single page of flash
static void flash_write_generic(uint16_t* flash, const uint16_t* data, uint16_t length_bytes);
static void flash_write_16bit(uint32_t flash, uint16_t data);

static inline void wait_for_flash_ready(void)
{
	while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {;}
}

static inline uint8_t record_exists(const volatile reid_ble_summary_packet_t* record)
{
	return (record->time_s_start != RECORD_UNUSED) && (record->time_s_end != RECORD_UNUSED);
}

// The slot a record with this index would occupy.
static inline reid_ble_summary_packet_t* record_slot(uint32_t index)
{
	return (reid_ble_summary_packet_t*) &flash_measurements[(index/FLASH_PAGE_RECORDS)%FLASH_NUM_PAGES].records[index%FLASH_PAGE_RECORDS];
}

void flash_init(void)
{
	flash_sysdata.device_id = (((uint64_t)NRF_FICR->DEVICEID[1]) << 32) | NRF_FICR->DEVICEID[0];
	flash_sysdata.device_version = DEVICE_FW_VERSION;

	// Resume the device clock just after the newest stored record (5 s after
	// zero when the store is empty). The first byte of the notice is a space,
	// so it contributes 0.
	uint32_t max_time_s = (*((volatile uint8_t*)copyright_message))-0x20;
	uint32_t next_index = 0;

	for (uint32_t i=0; i<FLASH_NUM_PAGES; ++i) {
		for (uint32_t j=0; j<FLASH_PAGE_RECORDS; ++j) {
			const volatile reid_ble_summary_packet_t* record = &flash_measurements[i].records[j];
			if (record_exists(record) && (record->index != RECORD_UNUSED) && (record->index >= next_index))
			{
				next_index = record->index+1;
				max_time_s = record->time_s_end;
			}
		}
	}
	system_set_time(max_time_s+5);
}

void flash_reset_all(void)
{
	ble_reid_force_disconnect();

	for (uint32_t i=0; i < FLASH_NUM_PAGES; ++i)
	{
		flash_erase_generic((uint16_t*) &(flash_measurements[i].records[0]));
	}

	ble_advertise_again();
}

reid_ble_summary_packet_t* flash_get_record(uint32_t req_index)
{
	reid_ble_summary_packet_t* record = record_slot(req_index);
	if (record_exists(record) && (record->index == req_index)) {
		return record;
	} else {
		return NULL;
	}
}

// Lowest and highest index among the stored records (optionally only those not
// yet marked synced). An empty result is low = 0xFFFFFFFF, high = 0.
static void flash_get_index_range(uint8_t pending_only, uint32_t* low_index, uint32_t* high_index)
{
	uint32_t ret_high = 0;
	uint32_t ret_low = 0xFFFFFFFFul;

	for (uint32_t i=0; i<FLASH_NUM_PAGES; ++i)
	{
		for (uint32_t j=0; j<FLASH_PAGE_RECORDS; ++j)
		{
			const volatile reid_ble_summary_packet_t* record = &flash_measurements[i].records[j];
			if (!record_exists(record)) continue;
			if (pending_only && (record->is_synced == FLASH_DATA_SYNCED_MARKER)) continue;
			if (record->index > ret_high) ret_high = record->index;
			if (record->index < ret_low) ret_low = record->index;
		}
	}

	if (low_index != NULL) *low_index = ret_low;
	if (high_index != NULL) *high_index = ret_high;
}

void flash_get_all_indices(uint32_t* low_index, uint32_t* high_index)
{
	flash_get_index_range(0, low_index, high_index);
}

void flash_get_pending_indices(uint32_t* low_index, uint32_t* high_index)
{
	flash_get_index_range(1, low_index, high_index);
}

void flash_mark_record_synced(uint32_t req_index)
{
	__attribute__((aligned(4))) uint16_t mark_synced = FLASH_DATA_SYNCED_MARKER;

	reid_ble_summary_packet_t* to_mark = record_slot(req_index);

	if (record_exists(to_mark)
	 && (to_mark->is_synced != FLASH_DATA_SYNCED_MARKER)
	 && (to_mark->index == req_index))
	{
		flash_write_generic((uint16_t*) &(to_mark->is_synced), (uint16_t*) &mark_synced, 2);
	}
}

void flash_mark_records_synced(uint32_t low_index, uint32_t high_index)
{
	__attribute__((aligned(4))) uint16_t mark_synced = FLASH_DATA_SYNCED_MARKER;

	for (uint32_t i=0; i<FLASH_NUM_PAGES; ++i)
	{
		for (uint32_t j=0; j<FLASH_PAGE_RECORDS; ++j)
		{
			const volatile reid_ble_summary_packet_t* record = &flash_measurements[i].records[j];
			if (record_exists(record)
			 && (record->is_synced != FLASH_DATA_SYNCED_MARKER)
			 && (record->index <= high_index)
			 && (record->index >= low_index))
			{
				flash_write_generic((uint16_t*) &flash_measurements[i].records[j].is_synced, (uint16_t*) &mark_synced, 2);
			}
		}
	}
}

static void flash_erase_generic(uint16_t* flash)
{
	safe_disable_interrupt();
	// Enable erase.
	NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Een;
	__ISB();
	__DSB();

	// Erase the page
	NRF_NVMC->ERASEPAGE = (uint32_t) flash;
	wait_for_flash_ready();

	NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren;
	__ISB();
	__DSB();

	safe_enable_interrupt();
}

static void flash_write_generic(uint16_t* flash, const uint16_t* data, uint16_t length_bytes)
{
	for (uint16_t i = 0; i<(length_bytes>>1); ++i)
	{
		flash_write_16bit(((uint32_t) flash) + (i*2), *(data+i));
	}
}

static void flash_write_16bit(uint32_t flash, uint16_t data)
{
	// Load the 4-byte aligned memory address, and the existing data at that location
	__attribute__((aligned(4))) uint32_t address = (flash >> 2)<<2;
	__attribute__((aligned(4))) uint32_t data_to_write = *((uint32_t*)address);

	// Mask existing data, and add the data to be written into it
	if (address == flash) data_to_write = (data_to_write & 0xFFFF0000) | ((uint32_t)data);
	else data_to_write = (data_to_write & 0x0000FFFF) | (((uint32_t)data) << 16);

	safe_disable_interrupt();
	// Enable write.
	NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen;
	__ISB();
	__DSB();

	*((uint32_t*)address) = data_to_write;
	wait_for_flash_ready();

	NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren;
	__ISB();
	__DSB();

	safe_enable_interrupt();
}
