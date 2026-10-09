/*===========================================
//
// measure.c
// Written by Alex Gilmour
// Copyright (c) 2024, CarbonCircuits
// All rights reserved.
//
//=========================================*/

#include "measure.h"
#include "configure_firmware.h"
#include "battery.h"
#include "drivers/adc.h"
#include "drivers/lmt01.h"
#include "drivers/lsm6dsm.h"
#include "drivers/system.h"
#include "nrf_log.h"

// Last measure_sensors() duration in microseconds (DWT). Read by ;QI (MEASUS=).
volatile uint32_t measure_last_us = 0;
// Last CAP-section duration in us (DWT). Read by ;QI (CAPUS=) to split the
// per-frame budget (caps are the dominant cost after the bank scan -- SEN-58).
volatile uint32_t measure_cap_us = 0;

#define FSR_OFFSET		50	// subtracted from every FSR reading, clamped at 0
#define CAP_AVERAGE_N	5	// cap readings are a moving average over this many frames

_Static_assert(NUM_TEMP == LMT01_NUM_SENSORS, "one frame slot per temperature sensor");

// ---- sensor wiring -----------------------------------------------------------
// Which sensor sits on each mux input. Row = mux input 1..7 (input 0 of every
// mux is unused), column = ADC bank. The two sides use one PCB but route the
// sensors differently.
#define NC		0xFF			// nothing on this input
#define F(n)	((n) - 1)		// FSRn -> index into reid_ble_packet_t.fsr[]

#ifdef REID_LHS
static const uint8_t fsr_map[ADC_MUX_INPUTS - 1][ADC_FSR_BANKS] = {
	{ F(12), F(14), F(17) },
	{ F(9),  F(11), F(18) },
	{ F(1),  F(10), NC    },
	{ F(13), F(5),  F(19) },
	{ F(6),  F(4),  NC    },
	{ F(2),  F(8),  F(15) },
	{ F(7),  F(3),  F(16) },
};
static const uint8_t cap_map[ADC_MUX_INPUTS - 1][ADC_CAP_BANKS] = {
	{ CAP_N(1), CAP_N(6) },
	{ CAP_N(5), CAP_S(6) },
	{ NC,       NC       },
	{ CAP_S(5), CAP_S(3) },
	{ CAP_S(2), CAP_N(3) },
	{ CAP_S(1), CAP_S(4) },
	{ CAP_N(2), CAP_N(4) },
};
#else
static const uint8_t fsr_map[ADC_MUX_INPUTS - 1][ADC_FSR_BANKS] = {
	{ F(7),  F(8),  F(16) },
	{ F(2),  F(3),  F(15) },
	{ F(6),  F(4),  NC    },
	{ F(13), F(5),  F(19) },
	{ F(1),  F(10), NC    },
	{ F(9),  F(14), F(17) },
	{ F(12), F(11), F(18) },
};
static const uint8_t cap_map[ADC_MUX_INPUTS - 1][ADC_CAP_BANKS] = {
	{ CAP_S(2), CAP_N(4) },
	{ CAP_N(2), CAP_S(4) },
	{ NC,       NC       },
	{ CAP_S(1), CAP_S(3) },
	{ CAP_N(5), CAP_N(3) },
	{ CAP_N(1), CAP_S(6) },
	{ CAP_S(5), CAP_N(6) },
};
#endif

static reid_ble_packet_t frame;

// Time-based temp cadence (SEN-58): LMT01 is a ~90ms blocking read, so gate on
// elapsed time (not frame count) -> stays slow regardless of stream rate.
// One sensor per TEMP_SAMPLE_PERIOD_MS; each of the 5 sensors every 5x that.
static void sample_temp(void)
{
	static uint64_t last_temp_ms = 0;
	static uint8_t temp_sensor = 0;

	if ((last_temp_ms == 0) || (frame.time_ms - last_temp_ms >= TEMP_SAMPLE_PERIOD_MS)) {
		last_temp_ms = frame.time_ms;
		if (++temp_sensor >= NUM_TEMP) temp_sensor = 0;
		frame.temp[temp_sensor] = lmt01_get_temp(temp_sensor);
	}
}

static void sample_fsrs(void)
{
	int32_t bank[ADC_FSR_BANKS];

	adc_fsr_begin();
	for (uint8_t input = 1; input < ADC_MUX_INPUTS; ++input) {
		adc_fsr_read(input, bank);
		for (uint8_t b = 0; b < ADC_FSR_BANKS; ++b) {
			uint8_t fsr = fsr_map[input - 1][b];
			if (fsr != NC) frame.fsr[fsr] = (uint16_t) bank[b];
		}
	}
	adc_fsr_end();

	for (uint8_t i = 0; i < NUM_FSR; ++i) {
		if (frame.fsr[i] > FSR_OFFSET) frame.fsr[i] -= FSR_OFFSET; else frame.fsr[i] = 0;
	}
}

static void sample_caps(void)
{
	static int32_t cap_history[NUM_CAP][CAP_AVERAGE_N];
	static uint8_t cap_history_pos = 0;

	if (++cap_history_pos >= CAP_AVERAGE_N) cap_history_pos = 0;

	adc_cap_select(0);
	for (uint8_t input = 1; input < ADC_MUX_INPUTS; ++input) {
		adc_cap_select(input);
		for (uint8_t b = 0; b < ADC_CAP_BANKS; ++b) {
			uint8_t cap = cap_map[input - 1][b];
			int32_t sum = 0;
			if (cap == NC) continue;
			cap_history[cap][cap_history_pos] = adc_cap_read(b);
			for (uint8_t i = 0; i < CAP_AVERAGE_N; ++i) sum += cap_history[cap][i];
			frame.cap[cap] = (uint16_t) (sum / CAP_AVERAGE_N);
		}
	}
}

const reid_ble_packet_t* measure_sensors(void)
{
	uint32_t meas_t0 = system_cycles(); // profile per-frame measurement cost (SEN-53)

	adc_sensors_on();

	frame.time_ms = system_time_ms();
	frame.vdd_mv = adc_read_vdd_mv();
	// vbat is sampled here, in sequence, because the SAADC is known to be idle
	// between the vdd read and the FSR scan.
	battery_sample(frame.time_ms);
	sample_temp();

	// SEN-68: refresh the IMU every frame (I2C burst ~0.5 ms). The buffered
	// accel/gyro are read straight into the stream row by the stream module.
	// At 208 Hz ODR each 10 ms read gets a fresh sample.
	lsm6dsm_update();

	sample_fsrs();

	uint32_t cap_t0 = system_cycles(); // SEN-58: profile the CAP-section cost
	sample_caps();
	adc_sensors_off();
	measure_cap_us = (system_cycles() - cap_t0) / SYSTEM_CYCLES_PER_US; // SEN-58 CAP-section cost

	// Per-frame measurement cost in us (DWT), stored every frame so ;QI can
	// report it (MEASUS=) without RTT -- the binding-constraint input for the
	// binary-stream rate ceiling (SEN-53/58). ~1-in-N frames also includes the
	// vbat read above, so expect periodic higher samples.
	measure_last_us = (system_cycles() - meas_t0) / SYSTEM_CYCLES_PER_US;
#ifdef ENABLE_DEBUG
	{
		static uint16_t meas_log_div = 0;
		if (++meas_log_div >= 40) {
			meas_log_div = 0;
			NRF_LOG_INFO("measure_sensors: %u us", (unsigned)measure_last_us);
		}
	}
#endif
	return &frame;
}

const reid_ble_packet_t* measure_latest(void)
{
	return &frame;
}
