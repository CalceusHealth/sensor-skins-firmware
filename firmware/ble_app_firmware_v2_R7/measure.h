/*===========================================
//
// measure.h
// Written by Alex Gilmour
// Copyright (c) 2024, CarbonCircuits
// All rights reserved.
//
//=========================================*/

#ifndef MEASURE_H_
#define MEASURE_H_

#include "configure_firmware.h"
#include <stdint.h>

// Last measure_sensors() duration in microseconds (DWT). Exposed for ;QI MEASUS=.
extern volatile uint32_t measure_last_us;
// Last CAP-section duration in us (DWT). Exposed for ;QI CAPUS=.
extern volatile uint32_t measure_cap_us;

#define REID_STREAM_BINARY_V2_MAGIC 0x5353u
// SEN-68: bumped 2 -> 3. Row layout changed: temp[5] moved OUT of the per-row
// payload into its own low-rate frame (frame_type 2), and IMU acc[3]+gyro[3]
// (int16) appended. Net row 74 -> 76 B, so 3 rows/frame still fit at MTU 247.
// Hosts must branch on this version byte (v2 = 74 B temp-in-row, no IMU).
#define REID_STREAM_BINARY_V2_VERSION 3u
#define REID_STREAM_BINARY_V2_FRAME_SENSOR_ROWS 1u
#define REID_STREAM_BINARY_V2_FRAME_TEMP 2u      // SEN-68: low-rate temp frame (temp[5])
#define REID_STREAM_BINARY_V2_FRAME_BATTERY 3u   // battery: low-rate vbat frame (drain metrics)
#define REID_STREAM_BINARY_V2_FLAG_FSR_X4 0x01u
#define REID_STREAM_BINARY_V2_FLAG_CAP_X3 0x02u
#define REID_STREAM_BINARY_V2_FLAG_IMU 0x04u  // SEN-68: rows carry acc[3]+gyro[3]

#pragma pack(push,1)
typedef struct reid_ble_packet_t {
	uint64_t time_ms;
	uint16_t vdd_mv;
	uint16_t fsr1;
	uint16_t fsr2;
	uint16_t fsr3;
	uint16_t fsr4;
	uint16_t fsr5;
	uint16_t fsr6;
	uint16_t fsr7;
	uint16_t fsr8;
	uint16_t fsr9;
	uint16_t fsr10;
	uint16_t fsr11;
	uint16_t fsr12;
	uint16_t fsr13;
	uint16_t fsr14;
	uint16_t fsr15;
	uint16_t fsr16;
	uint16_t fsr17;
	uint16_t fsr18;
	uint16_t fsr19;
	int16_t temp1;
	int16_t temp2;
	int16_t temp3;
	int16_t temp4;
	int16_t temp5;
	uint16_t cap1s;
	uint16_t cap1n;
	uint16_t cap2s;
	uint16_t cap2n;
	uint16_t cap3s;
	uint16_t cap3n;
	uint16_t cap4s;
	uint16_t cap4n;
	uint16_t cap5s;
	uint16_t cap5n;
	uint16_t cap6s;
	uint16_t cap6n;
} reid_ble_packet_t;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct reid_ble_stream_frame_v2_header_t {
	uint16_t magic;
	uint8_t version;
	uint8_t frame_type;
	uint8_t flags;
	uint8_t row_count;
	uint16_t sequence;
	uint64_t base_time_ms;
} reid_ble_stream_frame_v2_header_t;
#pragma pack(pop)

// SEN-68 (v3): 76 B. temp[5] removed (now its own frame_type 2); IMU appended.
// Layout: delta_time(2) + fsr[19](38) + cap[12](24) + acc[3](6) + gyro[3](6).
#pragma pack(push,1)
typedef struct reid_ble_stream_row_v2_t {
	uint16_t delta_time_ms;
	uint16_t fsr[19];
	uint16_t cap[12];
	int16_t acc[3];   // IMU accel x,y,z (raw LSB, +/-16g = 2048 LSB/g)
	int16_t gyro[3];  // IMU gyro  x,y,z (raw LSB, +/-2000dps = 16.4 LSB/dps)
} reid_ble_stream_row_v2_t;
#pragma pack(pop)

// SEN-68 (v3): low-rate temp payload, sent as frame_type 2 (~1 Hz). Same 16 B
// header, row_count=1, then this struct.
#pragma pack(push,1)
typedef struct reid_ble_stream_temp_v3_t {
	int16_t temp[5];
} reid_ble_stream_temp_v3_t;
#pragma pack(pop)

// Battery: low-rate frame (frame_type 3, ~1 Hz). Carries the AVERAGED vbat (mv)
// -- the value to use for in-session drain / battery-life metrics -- plus the
// fresh raw ADC (diagnostic), reported % (single-curve) and state char. Sent as
// a stream frame so it's captured during native recording (the QB query can't be
// polled while the native BLE owner is streaming, which is why per-row vbat was
// frozen in earlier session CSVs).
#pragma pack(push,1)
typedef struct reid_ble_stream_battery_v3_t {
	uint16_t vbat_mv;    // averaged pack voltage (mv) -- use this for drain
	int16_t  vbat_raw;   // fresh raw ADC sample (diagnostic; noisy)
	uint8_t  pct;        // reported charge % (single resting curve)
	uint8_t  state;      // battery_state_t char: 'K'/'C'/'D'/'L'/'U'
} reid_ble_stream_battery_v3_t;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct reid_ble_summary_packet_t {
	uint32_t time_s_start;
	uint32_t time_s_end;
	uint32_t index;
	uint16_t is_synced;
	uint16_t num_samples;
	uint16_t vdd_mv_min;
	uint16_t fsr1_max;
	uint16_t fsr2_max;
	uint16_t fsr3_max;
	uint16_t fsr4_max;
	uint16_t fsr5_max;
	uint16_t fsr6_max;
	uint16_t fsr7_max;
	uint16_t fsr8_max;
	uint16_t fsr9_max;
	uint16_t fsr10_max;
	uint16_t fsr11_max;
	uint16_t fsr12_max;
	uint16_t fsr13_max;
	uint16_t fsr14_max;
	uint16_t fsr15_max;
	uint16_t fsr16_max;
	uint16_t fsr17_max;
	uint16_t fsr18_max;
	uint16_t fsr19_max;
	int16_t temp1_max;
	int16_t temp2_max;
	int16_t temp3_max;
	int16_t temp4_max;
	int16_t temp5_max;
	int16_t cap1_delta_min;
	int16_t cap1_delta_max;
	int16_t cap2_delta_min;
	int16_t cap2_delta_max;
	int16_t cap3_delta_min;
	int16_t cap3_delta_max;
	int16_t cap4_delta_min;
	int16_t cap4_delta_max;
	int16_t cap5_delta_min;
	int16_t cap5_delta_max;
	int16_t cap6_delta_min;
	int16_t cap6_delta_max;
} reid_ble_summary_packet_t;
#pragma pack(pop)

void measure_sensors(reid_ble_packet_t* data, uint8_t force_temp);

void measure_update_summary(reid_ble_summary_packet_t* summary, reid_ble_packet_t* data);
void measure_reset_summary(reid_ble_summary_packet_t* summary);

#endif
