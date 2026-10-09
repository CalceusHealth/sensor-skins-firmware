/*===========================================
//
// stream.c
// Written by Alex Gilmour
// Copyright (c) 2024, CarbonCircuits
// All rights reserved.
//
//=========================================*/

#include "stream.h"
#include <string.h>
#include "configure_firmware.h"
#include "battery.h"
#include "drivers/ble_reid.h"
#include "drivers/lsm6dsm.h"

// Rows waiting to go out in the next sensor frame.
static struct {
	reid_ble_stream_row_v2_t rows[STREAM_BINARY_V2_MAX_ROWS];
	uint64_t row_time_ms[STREAM_BINARY_V2_MAX_ROWS];
	uint8_t row_count;
	uint16_t sequence;
} pending = {0};

static void stream_reset(void)
{
	memset((void*) &pending, 0, sizeof(pending));
}

void stream_flush(uint8_t force)
{
	if (pending.row_count == 0) return;
	if (!ble_is_connected()) {
		stream_reset();
		return;
	}

	const uint16_t tx_max_len = ble_reid_max_tx_len();
	const uint16_t header_len = sizeof(reid_ble_stream_frame_v2_header_t);
	const uint16_t row_len = sizeof(reid_ble_stream_row_v2_t);
	if (tx_max_len <= header_len || (tx_max_len - header_len) < row_len) return;

	uint8_t max_rows = (uint8_t)((tx_max_len - header_len) / row_len);
	if (max_rows > STREAM_BINARY_V2_MAX_ROWS) max_rows = STREAM_BINARY_V2_MAX_ROWS;

	const uint64_t first_time_ms = pending.row_time_ms[0];
	const uint64_t last_time_ms = pending.row_time_ms[pending.row_count - 1];
	if (!force &&
		pending.row_count < max_rows &&
		(last_time_ms - first_time_ms) < STREAM_BINARY_V2_MAX_LATENCY_MS) {
		return;
	}

	const uint8_t rows_to_send = (pending.row_count < max_rows) ? pending.row_count : max_rows;
	uint8_t frame_buffer[sizeof(reid_ble_stream_frame_v2_header_t) + (STREAM_BINARY_V2_MAX_ROWS * sizeof(reid_ble_stream_row_v2_t))] = {0};
	const uint16_t frame_len = header_len + (rows_to_send * row_len);
	reid_ble_stream_frame_v2_header_t header = {
		.magic = REID_STREAM_BINARY_V2_MAGIC,
		.version = REID_STREAM_BINARY_V2_VERSION,
		.frame_type = REID_STREAM_BINARY_V2_FRAME_SENSOR_ROWS,
		.flags = REID_STREAM_BINARY_V2_FLAG_FSR_X4 | REID_STREAM_BINARY_V2_FLAG_CAP_X3 | REID_STREAM_BINARY_V2_FLAG_IMU,
		.row_count = rows_to_send,
		.sequence = pending.sequence++,
		.base_time_ms = first_time_ms,
	};

	memcpy(frame_buffer, &header, sizeof(header));
	for (uint8_t i = 0; i < rows_to_send; ++i) {
		reid_ble_stream_row_v2_t row = pending.rows[i];
		row.delta_time_ms = (uint16_t)(pending.row_time_ms[i] - first_time_ms);
		memcpy(frame_buffer + header_len + (i * row_len), &row, row_len);
	}
	ble_reid_tx_stream(frame_buffer, frame_len); // SEN-59: non-blocking, don't stall the measurement loop

	// Keep whatever did not fit for the next frame.
	const uint8_t remaining = pending.row_count - rows_to_send;
	memmove((void*) &pending.rows[0], (const void*) &pending.rows[rows_to_send], remaining * sizeof(pending.rows[0]));
	memmove((void*) &pending.row_time_ms[0], (const void*) &pending.row_time_ms[rows_to_send], remaining * sizeof(pending.row_time_ms[0]));
	pending.row_count = remaining;
}

static void stream_queue_row(const reid_ble_packet_t* data)
{
	if (!ble_is_connected()) {
		stream_reset();
		return;
	}

	if (pending.row_count >= STREAM_BINARY_V2_MAX_ROWS) {
		stream_flush(1);
		// Still full: the link cannot carry even one row yet (MTU not negotiated).
		// Drop the backlog instead of writing past the row buffer.
		if (pending.row_count >= STREAM_BINARY_V2_MAX_ROWS) pending.row_count = 0;
	}

	reid_ble_stream_row_v2_t* row = &pending.rows[pending.row_count];
	memset((void*) row, 0, sizeof(*row));

	for (uint8_t i = 0; i < NUM_FSR; ++i) row->fsr[i] = data->fsr[i] * 4;
	for (uint8_t i = 0; i < NUM_CAP; ++i) row->cap[i] = data->cap[i] * 3;

	// SEN-68 (v3): IMU accel/gyro, refreshed by lsm6dsm_update() in measure_sensors.
	row->acc[0] = lsm6dsm_read_ax();
	row->acc[1] = lsm6dsm_read_ay();
	row->acc[2] = lsm6dsm_read_az();
	row->gyro[0] = lsm6dsm_read_gx();
	row->gyro[1] = lsm6dsm_read_gy();
	row->gyro[2] = lsm6dsm_read_gz();

	pending.row_time_ms[pending.row_count] = data->time_ms;
	++pending.row_count;
	stream_flush(0);
}

// A single-payload frame (temperature or battery): 16 B header, row_count = 1.
static void stream_send_single(uint8_t frame_type, uint16_t sequence, uint64_t time_ms, const void* payload, uint8_t payload_len)
{
	uint8_t buf[sizeof(reid_ble_stream_frame_v2_header_t) + sizeof(reid_ble_stream_temp_v3_t)] = {0};
	reid_ble_stream_frame_v2_header_t header = {
		.magic = REID_STREAM_BINARY_V2_MAGIC,
		.version = REID_STREAM_BINARY_V2_VERSION,
		.frame_type = frame_type,
		.flags = 0,
		.row_count = 1,
		.sequence = sequence,
		.base_time_ms = time_ms,
	};
	memcpy(buf, &header, sizeof(header));
	memcpy(buf + sizeof(header), payload, payload_len);
	ble_reid_tx_stream(buf, sizeof(header) + payload_len);
}
_Static_assert(sizeof(reid_ble_stream_battery_v3_t) <= sizeof(reid_ble_stream_temp_v3_t), "stream_send_single buffer is sized for the temp payload");

// SEN-68 (v3): temp moved out of the sensor row into its own low-rate frame
// (frame_type 2). Sent ~1 Hz; temp only changes every 60 s/sensor so this is
// cheap (26 B) and keeps the sensor row at 76 B / 3-per-frame. Non-blocking TX.
static void stream_send_temp(const reid_ble_packet_t* data)
{
	static uint16_t temp_sequence = 0;
	reid_ble_stream_temp_v3_t temp;

	if (!ble_is_connected()) return;
	memcpy(temp.temp, data->temp, sizeof(temp.temp));
	stream_send_single(REID_STREAM_BINARY_V2_FRAME_TEMP, temp_sequence++, data->time_ms, &temp, sizeof(temp));
}

// Battery frame (frame_type 3, ~1 Hz). Streams the AVERAGED vbat so in-session
// drain / battery-life metrics have a real time series (previously vbat was
// frozen per session because QB can't be polled during native streaming).
static void stream_send_battery(const reid_ble_packet_t* data)
{
	static uint16_t batt_sequence = 0;

	if (!ble_is_connected()) return;
	reid_ble_stream_battery_v3_t batt = {
		.vbat_mv = battery_pack_voltage_mv(),
		.vbat_raw = (int16_t) battery_pack_voltage_raw(),
		.pct = battery_pack_charge(),
		.state = (uint8_t) battery_current_state(),
	};
	stream_send_single(REID_STREAM_BINARY_V2_FRAME_BATTERY, batt_sequence++, data->time_ms, &batt, sizeof(batt));
}

void stream_push_frame(const reid_ble_packet_t* data)
{
	static uint64_t last_temp_frame_ms = 0;
	static uint64_t last_batt_frame_ms = 0;

	stream_queue_row(data);
	// SEN-68 (v3): emit the low-rate temp frame ~1 Hz (temp is no longer in the row).
	if ((last_temp_frame_ms == 0) || (data->time_ms - last_temp_frame_ms >= 1000)) {
		last_temp_frame_ms = data->time_ms;
		stream_send_temp(data);
	}
	// Battery: low-rate vbat frame ~1 Hz for in-session drain metrics.
	if ((last_batt_frame_ms == 0) || (data->time_ms - last_batt_frame_ms >= 1000)) {
		last_batt_frame_ms = data->time_ms;
		stream_send_battery(data);
	}
}
