/*===========================================
//
// main.c
// Written by Alex Gilmour
// Copyright (c) 2024, CarbonCircuits
// All rights reserved.
//
//=========================================*/

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "configure_firmware.h"
#include "gpio.h"
#include "app_timer.h"
#include "nrf_delay.h"
#include "system.h"
#include "flash.h"
#include "adc.h"
#include "messaging.h"
#include "ble_reid.h"
#include "lmt01.h"
#include "i2c.h"
#include "lsm6dsm.h"
#include "measure.h"

#include "nordic_common.h"
#include "nrf.h"
#include "app_util_platform.h"

#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

static void log_init(void);
static void main_init(void);
static void main_deinit(void);

volatile reid_ble_packet_t ble_data = {0};
volatile reid_ble_summary_packet_t summary_data = {0};

static void log_init(void)
{
    ret_code_t err_code = NRF_LOG_INIT(NULL);
    // APP_ERROR_CHECK(err_code); // is always NRF_SUCCESS
    NRF_LOG_DEFAULT_BACKENDS_INIT();
}

static void main_init(void)
{
    // Initialize.
	#ifdef ENABLE_CODE_PROTECT // Set Level 1 code protection
	system_set_code_protection();
	#endif // ENABLE_CODE_PROTECT
	system_init();
	gpio_init();
	adc_init();
	flash_init();
	#ifdef ENABLE_DEBUG
	log_init();
	#endif
	lmt01_init();
	msg_init();
    //ble_reid_init(); // inside msg_init()
}

int main(void)
{
	system_delay_cycles(100000);
	main_init();
	uint64_t timer = 0;

	while (1)
	{
		while (system_time_ms() < (timer + MAIN_LOOP_TIME_MS)) system_sleep();
        timer = system_time_ms();

		measure_sensors((reid_ble_packet_t*) &ble_data,0);
        measure_update_summary((reid_ble_summary_packet_t*) &summary_data, (reid_ble_packet_t*) &ble_data);
		static int32_t summary_counter = 0;
		if (++summary_counter >= NEW_SUMMARY_EVERY_N) {
			flash_write_record((reid_ble_summary_packet_t*) &summary_data);
			measure_reset_summary((reid_ble_summary_packet_t*) &summary_data);
			summary_counter = 0;
		}

		#ifdef ENABLE_DEBUG
		NRF_LOG_INFO("Reading at %u, vdd=%umV", ble_data.time_ms, ble_data.vdd_mv);
		NRF_LOG_INFO("FSR 1-5  %u,%u,%u,%u,%u",\
			ble_data.fsr1,\
			ble_data.fsr2,\
			ble_data.fsr3,\
			ble_data.fsr4,\
			ble_data.fsr5);
		NRF_LOG_INFO("FSR 6-10 %u,%u,%u,%u,%u",\
			ble_data.fsr6,\
			ble_data.fsr7,\
			ble_data.fsr8,\
			ble_data.fsr9,\
			ble_data.fsr10);
		NRF_LOG_INFO("FSR 11-15 %u,%u,%u,%u,%u",\
			ble_data.fsr11,\
			ble_data.fsr12,\
			ble_data.fsr13,\
			ble_data.fsr14,\
			ble_data.fsr15);
		NRF_LOG_INFO("FSR 16-19 %u,%u,%u,%u",\
			ble_data.fsr16,\
			ble_data.fsr17,\
			ble_data.fsr18,\
			ble_data.fsr19);
		NRF_LOG_INFO("TEMP %d,%d,%d,%d,%d",\
			ble_data.temp1,\
			ble_data.temp2,\
			ble_data.temp3,\
			ble_data.temp4,\
			ble_data.temp5);
		NRF_LOG_INFO("CAPN %u,%u,%u,%u,%u,%u",\
			ble_data.cap1n,\
			ble_data.cap2n,\
			ble_data.cap3n,\
			ble_data.cap4n,\
			ble_data.cap5n,\
			ble_data.cap6n);
		NRF_LOG_INFO("CAPS %u,%u,%u,%u,%u,%u",\
			ble_data.cap1s,\
			ble_data.cap2s,\
			ble_data.cap3s,\
			ble_data.cap4s,\
			ble_data.cap5s,\
			ble_data.cap6s);
		NRF_LOG_FLUSH();
		#endif
        
		#ifdef SEND_EVERY_MEAS_OVER_BLE
		#ifdef SEND_BLE_MEAS_AS_ASCII
		#define TX_STR_LEN_MAX 1024
		uint8_t tx_string[TX_STR_LEN_MAX] = {0};;
		uint16_t tx_string_len = 0;
		tx_string_len = snprintf(tx_string, TX_STR_LEN_MAX, "%u,%u,%u,%u,%u,",\
															ble_data.fsr1*4,\
															ble_data.fsr2*4,\
															ble_data.fsr3*4,\
															ble_data.fsr4*4,\
															ble_data.fsr5*4);
		tx_string_len += snprintf(tx_string+tx_string_len, TX_STR_LEN_MAX-tx_string_len, "%u,%u,%u,%u,%u,",\
															ble_data.fsr6*4,\
															ble_data.fsr7*4,\
															ble_data.fsr8*4,\
															ble_data.fsr9*4,\
															ble_data.fsr10*4);
		tx_string_len += snprintf(tx_string+tx_string_len, TX_STR_LEN_MAX-tx_string_len, "%u,%u,%u,%u,%u,",\
															ble_data.fsr11*4,\
															ble_data.fsr12*4,\
															ble_data.fsr13*4,\
															ble_data.fsr14*4,\
															ble_data.fsr15*4);
		tx_string_len += snprintf(tx_string+tx_string_len, TX_STR_LEN_MAX-tx_string_len, "%u,%u,%u,%u,",\
															ble_data.fsr16*4,\
															ble_data.fsr17*4,\
															ble_data.fsr18*4,\
															ble_data.fsr19*4);
		tx_string_len += snprintf(tx_string+tx_string_len, TX_STR_LEN_MAX-tx_string_len, "%d,%d,%d,%d,%d,",\
															ble_data.temp1,\
															ble_data.temp2,\
															ble_data.temp3,\
															ble_data.temp4,\
															ble_data.temp5);
		tx_string_len += snprintf(tx_string+tx_string_len, TX_STR_LEN_MAX-tx_string_len, "%u,%u,%u,%u,",\
															ble_data.cap1s*3,\
															ble_data.cap1n*3,\
															ble_data.cap2s*3,\
															ble_data.cap2n*3);
		tx_string_len += snprintf(tx_string+tx_string_len, TX_STR_LEN_MAX-tx_string_len, "%u,%u,%u,%u,",\
															ble_data.cap3s*3,\
															ble_data.cap3n*3,\
															ble_data.cap4s*3,\
															ble_data.cap4n*3);
		tx_string_len += snprintf(tx_string+tx_string_len, TX_STR_LEN_MAX-tx_string_len, "%u,%u,%u,%u\r\n",\
															ble_data.cap5s*3,\
															ble_data.cap5n*3,\
															ble_data.cap6s*3,\
															ble_data.cap6n*3);
		ble_reid_tx(tx_string,tx_string_len);
		#else
		static uint8_t ble_message[150] = {0};
		ble_message[0] = ';';
		ble_message[1] = 'R';
		ble_message[2] = 'F';
		ble_message[3] = ' ';
		memcpy(ble_message+4,(reid_ble_packet_t*)&ble_data,sizeof(reid_ble_packet_t));
		ble_message[4+sizeof(reid_ble_packet_t)] = '\r';
		ble_message[5+sizeof(reid_ble_packet_t)] = '\n';
		ble_reid_tx(ble_message,sizeof(reid_ble_packet_t)+6);
		#endif
		#endif
		
		static int16_t sleep_counter = 0;
		uint16_t num_low_sensors = 0;
		if (ble_data.fsr1 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
		if (ble_data.fsr2 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
		if (ble_data.fsr3 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
		if (ble_data.fsr4 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
		if (ble_data.fsr5 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
		if (ble_data.fsr6 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
		if (ble_data.fsr7 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
		if (ble_data.fsr8 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
		if (ble_data.fsr9 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
		if (ble_data.fsr10 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
		if (ble_data.fsr11 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
		if (ble_data.fsr12 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
		if (ble_data.fsr13 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
		if (ble_data.fsr14 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
		if (ble_data.fsr15 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
		if (ble_data.fsr16 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
		if (ble_data.fsr17 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
		if (ble_data.fsr18 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
		if (ble_data.fsr19 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
		if (ble_is_connected()) {
			sleep_counter = 0;
		} else if (num_low_sensors >= FSR_SLEEP_NUM) {
			sleep_counter += 1;
			if (sleep_counter > SLEEP_NUM_MEAS) sleep_counter = SLEEP_NUM_MEAS;
		} else {
			sleep_counter -= 4;
			if (sleep_counter < 0) sleep_counter = 0;
		}

		if (sleep_counter >= SLEEP_NUM_MEAS) { // enter "sleep" mode
			if (summary_counter > 0) {
				flash_write_record((reid_ble_summary_packet_t*) &summary_data);
				measure_reset_summary((reid_ble_summary_packet_t*) &summary_data);
				summary_counter = 0;
			}
			system_wait_for_ms_no_bg(250);
            ble_reid_force_disconnect();
			uint16_t num_low_sensors = 0;
			do {
				while (system_time_ms() < (timer + SLEEP_CHECK_EVERY_MS)) system_sleep();
				timer = system_time_ms();
				num_low_sensors = 0;
				measure_sensors((reid_ble_packet_t*) &ble_data,0);
				if (ble_data.fsr1 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
				if (ble_data.fsr2 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
				if (ble_data.fsr3 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
				if (ble_data.fsr4 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
				if (ble_data.fsr5 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
				if (ble_data.fsr6 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
				if (ble_data.fsr7 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
				if (ble_data.fsr8 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
				if (ble_data.fsr9 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
				if (ble_data.fsr10 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
				if (ble_data.fsr11 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
				if (ble_data.fsr12 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
				if (ble_data.fsr13 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
				if (ble_data.fsr14 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
				if (ble_data.fsr15 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
				if (ble_data.fsr16 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
				if (ble_data.fsr17 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
				if (ble_data.fsr18 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
				if (ble_data.fsr19 < FSR_SLEEP_THRESHOLD) ++num_low_sensors;
			} while (num_low_sensors >= FSR_SLEEP_NUM);
            ble_advertise_again();
		}
	}
}
