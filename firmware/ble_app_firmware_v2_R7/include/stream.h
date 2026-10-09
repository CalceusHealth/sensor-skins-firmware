/*===========================================
//
// stream.h
// Written by Alex Gilmour
// Copyright (c) 2024, CarbonCircuits
// All rights reserved.
//
//=========================================*/

// Binary measurement stream (BINARY_V2) sent as NUS notifications.

#ifndef STREAM_H_
#define STREAM_H_

#include <stdint.h>
#include "measure.h"

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

// Queues the frame just measured as a sensor row and, about once a second, sends
// the temperature and battery frames. Call once per measured frame.
void stream_push_frame(const reid_ble_packet_t* data);
// Sends queued rows. With force == 0 it waits for a full notification or
// STREAM_BINARY_V2_MAX_LATENCY_MS, whichever comes first.
void stream_flush(uint8_t force);

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

// SEN-68 (v3): 76 B. temp[5] removed (now its own frame_type 2); IMU appended.
// Layout: delta_time(2) + fsr[19](38) + cap[12](24) + acc[3](6) + gyro[3](6).
typedef struct reid_ble_stream_row_v2_t {
	uint16_t delta_time_ms;
	uint16_t fsr[NUM_FSR];
	uint16_t cap[NUM_CAP];
	int16_t acc[3];   // IMU accel x,y,z (raw LSB, +/-16g = 2048 LSB/g)
	int16_t gyro[3];  // IMU gyro  x,y,z (raw LSB, +/-2000dps = 16.4 LSB/dps)
} reid_ble_stream_row_v2_t;

// SEN-68 (v3): low-rate temp payload, sent as frame_type 2 (~1 Hz). Same 16 B
// header, row_count=1, then this struct.
typedef struct reid_ble_stream_temp_v3_t {
	int16_t temp[NUM_TEMP];
} reid_ble_stream_temp_v3_t;

// Battery: low-rate frame (frame_type 3, ~1 Hz). Carries the AVERAGED vbat (mv)
// -- the value to use for in-session drain / battery-life metrics -- plus the
// fresh raw ADC (diagnostic), reported % (single-curve) and state char. Sent as
// a stream frame so it's captured during native recording (the QB query can't be
// polled while the native BLE owner is streaming, which is why per-row vbat was
// frozen in earlier session CSVs).
typedef struct reid_ble_stream_battery_v3_t {
	uint16_t vbat_mv;    // averaged pack voltage (mv) -- use this for drain
	int16_t  vbat_raw;   // fresh raw ADC sample (diagnostic; noisy)
	uint8_t  pct;        // reported charge % (single resting curve)
	uint8_t  state;      // battery_state_t char: 'K'/'C'/'D'/'L'/'U'
} reid_ble_stream_battery_v3_t;
#pragma pack(pop)

_Static_assert(sizeof(reid_ble_stream_frame_v2_header_t) == 16, "stream header is a wire format");
_Static_assert(sizeof(reid_ble_stream_row_v2_t) == 76, "stream row is a wire format");

#endif // STREAM_H_
