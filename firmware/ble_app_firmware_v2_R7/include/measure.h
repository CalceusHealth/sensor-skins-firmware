/*===========================================
//
// measure.h
// Written by Alex Gilmour
// Copyright (c) 2024, CarbonCircuits
// All rights reserved.
//
//=========================================*/

// One frame of every sensor: which sensor sits where, and in what order they
// are read.

#ifndef MEASURE_H_
#define MEASURE_H_

#include <stdint.h>

#define NUM_FSR		19
#define NUM_TEMP	5
#define NUM_CAP		12	// 6 pads, each with a south and a north electrode

// Index of a cap electrode in reid_ble_packet_t.cap[] (pad is 1..6).
// "S" (south) is the electrode furthest from the big toe.
#define CAP_S(pad)	(((pad) - 1) * 2)
#define CAP_N(pad)	(((pad) - 1) * 2 + 1)

// Last measure_sensors() duration in microseconds (DWT). Exposed for ;QI MEASUS=.
extern volatile uint32_t measure_last_us;
// Last CAP-section duration in us (DWT). Exposed for ;QI CAPUS=.
extern volatile uint32_t measure_cap_us;

// One measurement frame. Also the binary blob returned by ;QL and ;QF, so the
// packed layout is part of the protocol.
#pragma pack(push,1)
typedef struct reid_ble_packet_t {
	uint64_t time_ms;
	uint16_t vdd_mv;
	uint16_t fsr[NUM_FSR];		// fsr[0] = FSR1 ... fsr[18] = FSR19
	int16_t temp[NUM_TEMP];		// 0.01 degC; temp[0] = TMP1
	uint16_t cap[NUM_CAP];		// cap1s, cap1n, cap2s, cap2n ... cap6s, cap6n
} reid_ble_packet_t;
#pragma pack(pop)

_Static_assert(sizeof(reid_ble_packet_t) == 82, "reid_ble_packet_t is a wire format");

// Measures a new frame and returns it. The frame stays valid, and is what
// measure_latest() returns, until the next call. Temperatures are refreshed one
// sensor per TEMP_SAMPLE_PERIOD_MS, so most frames carry the previous values.
const reid_ble_packet_t* measure_sensors(void);
const reid_ble_packet_t* measure_latest(void);

#endif
