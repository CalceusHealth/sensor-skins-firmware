/*===========================================
//
// main.c
// Written by Alex Gilmour
// Copyright (c) 2024, CarbonCircuits
// All rights reserved.
//
//=========================================*/

#include <stdint.h>

#include "configure_firmware.h"
#include "battery.h"
#include "measure.h"
#include "messaging.h"
#include "power.h"
#include "stream.h"
#include "drivers/adc.h"
#include "drivers/ble_reid.h"
#include "drivers/flash.h"
#include "drivers/lmt01.h"
#include "drivers/lsm6dsm.h"
#include "drivers/system.h"

#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

static void log_init(void)
{
    ret_code_t err_code = NRF_LOG_INIT(NULL);
    // APP_ERROR_CHECK(err_code); // is always NRF_SUCCESS
    NRF_LOG_DEFAULT_BACKENDS_INIT();
}

static void main_init(void)
{
	// Commands are served from every wait loop, including the waits inside the
	// initialisation below.
	system_set_idle_hook(msg_process_packet);

	system_init();
	adc_init();
	battery_init();
	flash_init();
	#ifdef ENABLE_DEBUG
	log_init();
	#endif
	lmt01_init();
	lsm6dsm_init();
	#ifdef ENABLE_DEBUG
	NRF_LOG_INFO("LSM6DSM WHO_AM_I=0x%02X (expect 0x%02X)", lsm6dsm_whoami(), LSM6DSM_WHO_AM_I_VALUE);
	NRF_LOG_FLUSH();
	#endif
	msg_init();
	ble_reid_init(msg_rx_next_byte);
}

static void log_frame(const reid_ble_packet_t* frame)
{
	#ifdef ENABLE_DEBUG
	NRF_LOG_INFO("Reading at %u, vdd=%umV", frame->time_ms, frame->vdd_mv);
	NRF_LOG_INFO("FSR 1-5  %u,%u,%u,%u,%u", frame->fsr[0], frame->fsr[1], frame->fsr[2], frame->fsr[3], frame->fsr[4]);
	NRF_LOG_INFO("FSR 6-10 %u,%u,%u,%u,%u", frame->fsr[5], frame->fsr[6], frame->fsr[7], frame->fsr[8], frame->fsr[9]);
	NRF_LOG_INFO("FSR 11-15 %u,%u,%u,%u,%u", frame->fsr[10], frame->fsr[11], frame->fsr[12], frame->fsr[13], frame->fsr[14]);
	NRF_LOG_INFO("FSR 16-19 %u,%u,%u,%u", frame->fsr[15], frame->fsr[16], frame->fsr[17], frame->fsr[18]);
	NRF_LOG_INFO("TEMP %d,%d,%d,%d,%d", frame->temp[0], frame->temp[1], frame->temp[2], frame->temp[3], frame->temp[4]);
	NRF_LOG_INFO("CAPN %u,%u,%u,%u,%u,%u", frame->cap[CAP_N(1)], frame->cap[CAP_N(2)], frame->cap[CAP_N(3)], frame->cap[CAP_N(4)], frame->cap[CAP_N(5)], frame->cap[CAP_N(6)]);
	NRF_LOG_INFO("CAPS %u,%u,%u,%u,%u,%u", frame->cap[CAP_S(1)], frame->cap[CAP_S(2)], frame->cap[CAP_S(3)], frame->cap[CAP_S(4)], frame->cap[CAP_S(5)], frame->cap[CAP_S(6)]);
	NRF_LOG_FLUSH();
	#endif
}

int main(void)
{
	system_delay_cycles(100000);
	main_init();
	uint64_t tick_ms = 0;

	while (1)
	{
		// The only loop in the firmware: one tick per power_tick_period_ms(),
		// which is a frame while awake and a wake-up check while asleep.
		while (system_time_ms() < (tick_ms + power_tick_period_ms())) system_sleep();
		tick_ms = system_time_ms();

		if (!power_tick_begin()) continue;

		const reid_ble_packet_t* frame = measure_sensors();
		power_frame_measured(frame);
		log_frame(frame);
		#ifdef SEND_EVERY_MEAS_OVER_BLE
		stream_push_frame(frame);
		#endif
		power_tick_end(frame);
	}
}
