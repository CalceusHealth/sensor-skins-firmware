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
#include "gpio.h"
#include "adc.h"
#include "lmt01.h"
#include "flash.h"
#include "battery.h"
#include "system.h"
#include "lsm6dsm.h"
#include "nrf_log.h"

// Last measure_sensors() duration in microseconds (DWT). Read by ;QI (MEASUS=).
volatile uint32_t measure_last_us = 0;
// Last CAP-section duration in us (DWT). Read by ;QI (CAPUS=) to split the
// per-frame budget (caps are the dominant cost after the bank scan -- SEN-58).
volatile uint32_t measure_cap_us = 0;

#define AVERAGE_SIZE	5

static inline int32_t find_average(int32_t* data) {
	int32_t ret_val = 0;
	for (uint8_t i=0; i<AVERAGE_SIZE; ++i) {
		ret_val += data[i];
	}
	ret_val /= AVERAGE_SIZE;
	return ret_val;
}

#define MEAS_SETTLING	10000
// SEN-58: cap mux settling, separate from FSR's MEAS_SETTLING so cap can be tuned
// without touching FSR. The 8 cap settle busy-loops dominate the CAP-section time
// (conversions + channel-init ruled out empirically).
#define CAP_SETTLING	2000

#define MIN_CAP		500
#define MAX_CAP		10000
#define MIN_TEMP	-1000
#define MAX_TEMP	7500
#define MIN_FSR		0
#define MAX_FSR		5000


#define FSR_MIN	50 // for subtraction

#define CAL_C1S		0
#define CAL_C1N		0
#define CAL_C2S		0
#define CAL_C2N		0
#define CAL_C3S		0
#define CAL_C3N		0
#define CAL_C4S		0
#define CAL_C4N		0
#define CAL_C5S		0 // note "S" is furthest from big toe
#define CAL_C5N		0
#define CAL_C6S		0
#define CAL_C6N		0


/*
// calibrate to 2050
#ifdef REID_LHS
#define CAL_C1S		-1280/3
#define CAL_C1N		-1405/3
#define CAL_C2S		1225/3
#define CAL_C2N		-55/3
#define CAL_C3S		-595/3
#define CAL_C3N		-620/3
#define CAL_C4S		3850/3
#define CAL_C4N		-510/3
#else
#define CAL_C1S		-60/3
#define CAL_C1N		-570/3
#define CAL_C2S		2090/3
#define CAL_C2N		650/3
#define CAL_C3S		1460/3
#define CAL_C3N		900/3
#define CAL_C4S		1640/3
#define CAL_C4N		835/3
#endif*/

int32_t cap1s_average[AVERAGE_SIZE];
int32_t cap1n_average[AVERAGE_SIZE];
int32_t cap2s_average[AVERAGE_SIZE];
int32_t cap2n_average[AVERAGE_SIZE];
int32_t cap3s_average[AVERAGE_SIZE];
int32_t cap3n_average[AVERAGE_SIZE];
int32_t cap4s_average[AVERAGE_SIZE];
int32_t cap4n_average[AVERAGE_SIZE];
int32_t cap5s_average[AVERAGE_SIZE];
int32_t cap5n_average[AVERAGE_SIZE];
int32_t cap6s_average[AVERAGE_SIZE];
int32_t cap6n_average[AVERAGE_SIZE];

uint8_t average_counter = 0;

void measure_sensors(reid_ble_packet_t* data, uint8_t force_temp)
{
	uint32_t meas_t0 = system_cycles(); // profile per-frame measurement cost (SEN-53)
	nrf_gpio_pin_clear(PIN_FSR_S0);
	nrf_gpio_pin_clear(PIN_FSR_S1);
    nrf_gpio_pin_clear(PIN_FSR_S2);
	nrf_gpio_pin_clear(PIN_CAP_S0);
	nrf_gpio_pin_clear(PIN_CAP_S1);
    nrf_gpio_pin_clear(PIN_CAP_S2);
	
	nrf_gpio_pin_clear(PIN_MUX_ON);
	// SEN-58 step 1: the SAADC is initialised once at startup (main_init) and
	// left running -- no per-frame adc_init()/adc_deinit() teardown. (The old
	// pre-init "clear the ADC" bank reads were no-ops on the torn-down driver.)
	data->time_ms = system_time_ms();
	data->vdd_mv = adc_read_vdd_mv();

	// Sample vbat in-sequence on a slow, frame-rate-independent cadence. The
	// SAADC is idle here (inited once at startup, just after the vdd read), so this read
	// always succeeds -- no busy/0 path, no forced-fresh teardown. Gating on
	// elapsed time (not frame count) keeps it at one read per VBAT_SAMPLE_PERIOD_MS
	// regardless of stream rate, so high-rate streaming never pays for it and the
	// low-battery average/protection stays reliable. See battery_submit_raw().
	static uint64_t last_vbat_ms = 0;
	if ((last_vbat_ms == 0) || (data->time_ms - last_vbat_ms >= VBAT_SAMPLE_PERIOD_MS)) {
		last_vbat_ms = data->time_ms;
		int32_t vbat_raw = adc_read_vbat_raw();
		if (vbat_raw > 0) battery_submit_raw(vbat_raw);
	}
	
	static uint64_t last_temp_ms = 0;
	static uint8_t temp_sensor = 0;

	// Time-based temp cadence (SEN-58): LMT01 is a ~90ms blocking read, so gate
	// on elapsed time (not frame count) -> stays slow regardless of stream rate.
	// One sensor per TEMP_SAMPLE_PERIOD_MS; each of 5 sensors every 5x that.
	if ((force_temp != 0) || (last_temp_ms == 0) || (data->time_ms - last_temp_ms >= TEMP_SAMPLE_PERIOD_MS)) {
		last_temp_ms = data->time_ms;
		if (++temp_sensor >= 5) temp_sensor = 0;
		switch (temp_sensor) {
		default:
			temp_sensor = 0;
		case 0:
			data->temp1 = lmt01_get_temp(PIN_TEMP_S0);
			break;
		case 1:
			data->temp2 = lmt01_get_temp(PIN_TEMP_S1);
			break;
		case 2:
			data->temp3 = lmt01_get_temp(PIN_TEMP_S2);
			break;
		case 3:
			data->temp4 = lmt01_get_temp(PIN_TEMP_S3);
			break;
		case 4:
			data->temp5 = lmt01_get_temp(PIN_TEMP_S4);
			break;
		}
	}
	
	// SEN-68: refresh the IMU every frame (I2C burst ~0.5 ms). The buffered
	// accel/gyro are read straight into the stream row by stream_send_measurement.
	// At 208 Hz ODR each 10 ms read gets a fresh sample.
	lsm6dsm_update();

	if (++average_counter >= AVERAGE_SIZE) average_counter = 0;

	adc_banks_begin(); // SEN-58: read the 3 FSR banks via one EasyDMA scan per mux step

	// SET 0/8/16
	nrf_gpio_pin_clear(PIN_FSR_S0);
	nrf_gpio_pin_clear(PIN_FSR_S1);
    nrf_gpio_pin_clear(PIN_FSR_S2);
	// shouldn't be anything here anyway
	#ifdef REID_LHS
	#else
	#endif

	// SET 1/9/17
    nrf_gpio_pin_set(PIN_FSR_S0);
	system_delay_cycles(MEAS_SETTLING);
    adc_read_bank1();
    adc_read_bank2();
    adc_read_bank3();
	#ifdef REID_LHS
	data->fsr12 = adc_read_bank1();
	data->fsr14 = adc_read_bank2();
	data->fsr17 = adc_read_bank3();
	#else
	data->fsr7 = adc_read_bank1();
	data->fsr8 = adc_read_bank2();
	data->fsr16 = adc_read_bank3();
	#endif
	
	// SET 2/10/18
    nrf_gpio_pin_set(PIN_FSR_S1);
    nrf_gpio_pin_clear(PIN_FSR_S0);
	system_delay_cycles(MEAS_SETTLING);
    adc_read_bank1();
    adc_read_bank2();
    adc_read_bank3();
	#ifdef REID_LHS
	data->fsr9 = adc_read_bank1();
	data->fsr11 = adc_read_bank2();
	data->fsr18 = adc_read_bank3();
	#else
	data->fsr2 = adc_read_bank1();
	data->fsr3 = adc_read_bank2();
	data->fsr15 = adc_read_bank3();
	#endif

	// SET 3/11/19
    nrf_gpio_pin_set(PIN_FSR_S0);
	system_delay_cycles(MEAS_SETTLING);
    adc_read_bank1();
    adc_read_bank2();
    adc_read_bank3();
	#ifdef REID_LHS
	data->fsr1 = adc_read_bank1();
	data->fsr10 = adc_read_bank2();
	adc_read_bank3();
	#else
	data->fsr6 = adc_read_bank1();
	data->fsr4 = adc_read_bank2();
	adc_read_bank3();
	#endif

	// SET 4/12/20
	nrf_gpio_pin_set(PIN_FSR_S2);
	nrf_gpio_pin_clear(PIN_FSR_S1);
    nrf_gpio_pin_clear(PIN_FSR_S0);
	system_delay_cycles(MEAS_SETTLING);
    adc_read_bank1();
    adc_read_bank2();
    adc_read_bank3();
	#ifdef REID_LHS
	data->fsr13 = adc_read_bank1();
	data->fsr5 = adc_read_bank2();
	data->fsr19 = adc_read_bank3();
	#else
	data->fsr13 = adc_read_bank1();
	data->fsr5 = adc_read_bank2();
	data->fsr19 = adc_read_bank3();
	#endif

	// SET 5/13/21
    nrf_gpio_pin_set(PIN_FSR_S0);
	system_delay_cycles(MEAS_SETTLING);
    adc_read_bank1();
    adc_read_bank2();
    adc_read_bank3();
	#ifdef REID_LHS
	data->fsr6 = adc_read_bank1();
	data->fsr4 = adc_read_bank2();
	adc_read_bank3();
	#else
	data->fsr1 = adc_read_bank1();
	data->fsr10 = adc_read_bank2();
	adc_read_bank3();
	#endif

	// SET 6/14/22
    nrf_gpio_pin_set(PIN_FSR_S1);
    nrf_gpio_pin_clear(PIN_FSR_S0);
	system_delay_cycles(MEAS_SETTLING);
    adc_read_bank1();
    adc_read_bank2();
    adc_read_bank3();
	#ifdef REID_LHS
	data->fsr2 = adc_read_bank1();
	data->fsr8 = adc_read_bank2();
	data->fsr15 = adc_read_bank3();
	#else
	data->fsr9 = adc_read_bank1();
	data->fsr14 = adc_read_bank2();
	data->fsr17 = adc_read_bank3();
	#endif

	// SET 7/15/23
    nrf_gpio_pin_set(PIN_FSR_S0);
	system_delay_cycles(MEAS_SETTLING);
    adc_read_bank1();
    adc_read_bank2();
    adc_read_bank3();
	#ifdef REID_LHS
	data->fsr7 = adc_read_bank1();
	data->fsr3 = adc_read_bank2();
	data->fsr16 = adc_read_bank3();
	#else
	data->fsr12 = adc_read_bank1();
	data->fsr11 = adc_read_bank2();
	data->fsr18 = adc_read_bank3();
	#endif

	nrf_gpio_pin_clear(PIN_FSR_S0);
	nrf_gpio_pin_clear(PIN_FSR_S1);
    nrf_gpio_pin_clear(PIN_FSR_S2);
	
	
	
	adc_banks_end(); // SEN-58: release scan channels before the CAP reads
	uint32_t cap_t0 = system_cycles(); // SEN-58: profile the CAP-section cost

	// SET 0/8
	nrf_gpio_pin_clear(PIN_CAP_S0);
	nrf_gpio_pin_clear(PIN_CAP_S1);
    nrf_gpio_pin_clear(PIN_CAP_S2);
	system_delay_cycles(CAP_SETTLING);
	// shouldn't be anything here anyway
	#ifdef REID_LHS
	#else
	#endif

	// SET 1/9
    nrf_gpio_pin_set(PIN_CAP_S0);
	system_delay_cycles(CAP_SETTLING);
	#ifdef REID_LHS
	cap1n_average[average_counter] = adc_read_cap1();
	data->cap1n = find_average(cap1n_average)-CAL_C1N;
	cap6n_average[average_counter] = adc_read_cap2();
	data->cap6n = find_average(cap6n_average)-CAL_C6N;
	#else
	cap2s_average[average_counter] = adc_read_cap1();
	data->cap2s = find_average(cap2s_average)-CAL_C2S;
	cap4n_average[average_counter] = adc_read_cap2();
	data->cap4n = find_average(cap4n_average)-CAL_C4N;
	#endif
	
	// SET 2/10
    nrf_gpio_pin_set(PIN_CAP_S1);
    nrf_gpio_pin_clear(PIN_CAP_S0);
	system_delay_cycles(CAP_SETTLING);
	#ifdef REID_LHS
	cap5n_average[average_counter] = adc_read_cap1();
	data->cap5n = find_average(cap5n_average)-CAL_C5N;
	cap6s_average[average_counter] = adc_read_cap2();
	data->cap6s = find_average(cap6s_average)-CAL_C6S;
	#else
	cap2n_average[average_counter] = adc_read_cap1();
	data->cap2n = find_average(cap2n_average)-CAL_C2N;
	cap4s_average[average_counter] = adc_read_cap2();
	data->cap4s = find_average(cap4s_average)-CAL_C4S;
	#endif

	// SET 3/11
    nrf_gpio_pin_set(PIN_CAP_S0);
	system_delay_cycles(CAP_SETTLING);
	#ifdef REID_LHS
	/*cap_average[average_counter] = adc_read_cap1();
	data->cap = find_average(cap_average)-CAL_C;
	cap_average[average_counter] = adc_read_cap2();
	data->cap = find_average(cap_average)-CAL_C;*/
	#else
	/*cap_average[average_counter] = adc_read_cap1();
	data->cap = find_average(cap_average)-CAL_C;
	cap_average[average_counter] = adc_read_cap2();
	data->cap = find_average(cap_average)-CAL_C;*/
	#endif

	// SET 4/12
	nrf_gpio_pin_set(PIN_CAP_S2);
	nrf_gpio_pin_clear(PIN_CAP_S1);
    nrf_gpio_pin_clear(PIN_CAP_S0);
	system_delay_cycles(CAP_SETTLING);
	#ifdef REID_LHS
	cap5s_average[average_counter] = adc_read_cap1();
	data->cap5s = find_average(cap5s_average)-CAL_C5S;
	cap3s_average[average_counter] = adc_read_cap2();
	data->cap3s = find_average(cap3s_average)-CAL_C3S;
	#else
	cap1s_average[average_counter] = adc_read_cap1();
	data->cap1s = find_average(cap1s_average)-CAL_C1S;
	cap3s_average[average_counter] = adc_read_cap2();
	data->cap3s = find_average(cap3s_average)-CAL_C3S;
	#endif

	// SET 5/13
    nrf_gpio_pin_set(PIN_CAP_S0);
	system_delay_cycles(CAP_SETTLING);
	#ifdef REID_LHS
	cap2s_average[average_counter] = adc_read_cap1();
	data->cap2s = find_average(cap2s_average)-CAL_C2S;
	cap3n_average[average_counter] = adc_read_cap2();
	data->cap3n = find_average(cap3n_average)-CAL_C3N;
	#else
	cap5n_average[average_counter] = adc_read_cap1();
	data->cap5n = find_average(cap5n_average)-CAL_C5N;
	cap3n_average[average_counter] = adc_read_cap2();
	data->cap3n = find_average(cap3n_average)-CAL_C3N;
	#endif

	// SET 6/14
    nrf_gpio_pin_set(PIN_CAP_S1);
    nrf_gpio_pin_clear(PIN_CAP_S0);
	system_delay_cycles(CAP_SETTLING);
	#ifdef REID_LHS
	cap1s_average[average_counter] = adc_read_cap1();
	data->cap1s = find_average(cap1s_average)-CAL_C1S;
	cap4s_average[average_counter] = adc_read_cap2();
	data->cap4s = find_average(cap4s_average)-CAL_C4S;
	#else
	cap1n_average[average_counter] = adc_read_cap1();
	data->cap1n = find_average(cap1n_average)-CAL_C1N;
	cap6s_average[average_counter] = adc_read_cap2();
	data->cap6s = find_average(cap6s_average)-CAL_C6S;
	#endif

	// SET 7/15
    nrf_gpio_pin_set(PIN_CAP_S0);
	system_delay_cycles(CAP_SETTLING);
	#ifdef REID_LHS
	cap2n_average[average_counter] = adc_read_cap1();
	data->cap2n = find_average(cap2n_average)-CAL_C2N;
	cap4n_average[average_counter] = adc_read_cap2();
	data->cap4n = find_average(cap4n_average)-CAL_C4N;
	#else
	cap5s_average[average_counter] = adc_read_cap1();
	data->cap5s = find_average(cap5s_average)-CAL_C5S;
	cap6n_average[average_counter] = adc_read_cap2();
	data->cap6n = find_average(cap6n_average)-CAL_C6N;
	#endif

	nrf_gpio_pin_clear(PIN_FSR_S0);
	nrf_gpio_pin_clear(PIN_FSR_S1);
    nrf_gpio_pin_clear(PIN_FSR_S2);
	nrf_gpio_pin_clear(PIN_CAP_S0);
	nrf_gpio_pin_clear(PIN_CAP_S1);
    nrf_gpio_pin_clear(PIN_CAP_S2);
	nrf_gpio_pin_set(PIN_MUX_ON);
	measure_cap_us = (system_cycles() - cap_t0) / SYSTEM_CYCLES_PER_US; // SEN-58 CAP-section cost


	if (data->fsr1  > FSR_MIN) data->fsr1  -=FSR_MIN; else data->fsr1  = 0;
	if (data->fsr2  > FSR_MIN) data->fsr2  -=FSR_MIN; else data->fsr2  = 0;
	if (data->fsr3  > FSR_MIN) data->fsr3  -=FSR_MIN; else data->fsr3  = 0;
	if (data->fsr4  > FSR_MIN) data->fsr4  -=FSR_MIN; else data->fsr4  = 0;
	if (data->fsr5  > FSR_MIN) data->fsr5  -=FSR_MIN; else data->fsr5  = 0;
	if (data->fsr6  > FSR_MIN) data->fsr6  -=FSR_MIN; else data->fsr6  = 0;
	if (data->fsr7  > FSR_MIN) data->fsr7  -=FSR_MIN; else data->fsr7  = 0;
	if (data->fsr8  > FSR_MIN) data->fsr8  -=FSR_MIN; else data->fsr8  = 0;
	if (data->fsr9  > FSR_MIN) data->fsr9  -=FSR_MIN; else data->fsr9  = 0;
	if (data->fsr10 > FSR_MIN) data->fsr10 -=FSR_MIN; else data->fsr10 = 0;
	if (data->fsr11 > FSR_MIN) data->fsr11 -=FSR_MIN; else data->fsr11 = 0;
	if (data->fsr12 > FSR_MIN) data->fsr12 -=FSR_MIN; else data->fsr12 = 0;
	if (data->fsr13 > FSR_MIN) data->fsr13 -=FSR_MIN; else data->fsr13 = 0;
	if (data->fsr14 > FSR_MIN) data->fsr14 -=FSR_MIN; else data->fsr14 = 0;
	if (data->fsr15 > FSR_MIN) data->fsr15 -=FSR_MIN; else data->fsr15 = 0;
	if (data->fsr16 > FSR_MIN) data->fsr16 -=FSR_MIN; else data->fsr16 = 0;
	if (data->fsr17 > FSR_MIN) data->fsr17 -=FSR_MIN; else data->fsr17 = 0;
	if (data->fsr18 > FSR_MIN) data->fsr18 -=FSR_MIN; else data->fsr18 = 0;
	if (data->fsr19 > FSR_MIN) data->fsr19 -=FSR_MIN; else data->fsr19 = 0;

	// SEN-58 step 1: no per-frame adc_deinit() -- SAADC stays initialised
	// (analog auto-powers only during sampling, so idle draw is negligible;
	// flagged for the SEN-49 sleep-current audit to confirm).

	// Per-frame measurement cost in us (DWT), stored every frame so ;QI can
	// report it (MEASUS=) without RTT -- the binding-constraint input for the
	// binary-stream rate ceiling (SEN-53/58). ~1-in-N frames also includes the
	// VBAT_SAMPLE_PERIOD_MS vbat read above, so expect periodic higher samples.
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
}

void measure_update_summary(reid_ble_summary_packet_t* summary, reid_ble_packet_t* data)
{
	if (summary->time_s_start > (data->time_ms/1000)) summary->time_s_start = (uint32_t) (data->time_ms/1000);
	if (summary->time_s_end   < (data->time_ms/1000)) summary->time_s_end =   (uint32_t) (data->time_ms/1000);

	if (summary->num_samples < 65534) summary->num_samples += 1;

	if (summary->vdd_mv_min < data->vdd_mv) summary->vdd_mv_min = data->vdd_mv;

	if ((data->fsr1  >= MIN_FSR)&&(data->fsr1  <= MAX_FSR)&&(summary->fsr1_max  < data->fsr1 )) summary->fsr1_max  = data->fsr1;
	if ((data->fsr2  >= MIN_FSR)&&(data->fsr2  <= MAX_FSR)&&(summary->fsr2_max  < data->fsr2 )) summary->fsr2_max  = data->fsr2;
	if ((data->fsr3  >= MIN_FSR)&&(data->fsr3  <= MAX_FSR)&&(summary->fsr3_max  < data->fsr3 )) summary->fsr3_max  = data->fsr3;
	if ((data->fsr4  >= MIN_FSR)&&(data->fsr4  <= MAX_FSR)&&(summary->fsr4_max  < data->fsr4 )) summary->fsr4_max  = data->fsr4;
	if ((data->fsr5  >= MIN_FSR)&&(data->fsr5  <= MAX_FSR)&&(summary->fsr5_max  < data->fsr5 )) summary->fsr5_max  = data->fsr5;
	if ((data->fsr6  >= MIN_FSR)&&(data->fsr6  <= MAX_FSR)&&(summary->fsr6_max  < data->fsr6 )) summary->fsr6_max  = data->fsr6;
	if ((data->fsr7  >= MIN_FSR)&&(data->fsr7  <= MAX_FSR)&&(summary->fsr7_max  < data->fsr7 )) summary->fsr7_max  = data->fsr7;
	if ((data->fsr8  >= MIN_FSR)&&(data->fsr8  <= MAX_FSR)&&(summary->fsr8_max  < data->fsr8 )) summary->fsr8_max  = data->fsr8;
	if ((data->fsr9  >= MIN_FSR)&&(data->fsr9  <= MAX_FSR)&&(summary->fsr9_max  < data->fsr9 )) summary->fsr9_max  = data->fsr9;
	if ((data->fsr10 >= MIN_FSR)&&(data->fsr10 <= MAX_FSR)&&(summary->fsr10_max < data->fsr10)) summary->fsr10_max = data->fsr10;
	if ((data->fsr11 >= MIN_FSR)&&(data->fsr11 <= MAX_FSR)&&(summary->fsr11_max < data->fsr1 )) summary->fsr11_max = data->fsr11;
	if ((data->fsr12 >= MIN_FSR)&&(data->fsr12 <= MAX_FSR)&&(summary->fsr12_max < data->fsr2 )) summary->fsr12_max = data->fsr12;
	if ((data->fsr13 >= MIN_FSR)&&(data->fsr13 <= MAX_FSR)&&(summary->fsr13_max < data->fsr3 )) summary->fsr13_max = data->fsr13;
	if ((data->fsr14 >= MIN_FSR)&&(data->fsr14 <= MAX_FSR)&&(summary->fsr14_max < data->fsr4 )) summary->fsr14_max = data->fsr14;
	if ((data->fsr15 >= MIN_FSR)&&(data->fsr15 <= MAX_FSR)&&(summary->fsr15_max < data->fsr5 )) summary->fsr15_max = data->fsr15;
	if ((data->fsr16 >= MIN_FSR)&&(data->fsr16 <= MAX_FSR)&&(summary->fsr16_max < data->fsr6 )) summary->fsr16_max = data->fsr16;
	if ((data->fsr17 >= MIN_FSR)&&(data->fsr17 <= MAX_FSR)&&(summary->fsr17_max < data->fsr7 )) summary->fsr17_max = data->fsr17;
	if ((data->fsr18 >= MIN_FSR)&&(data->fsr18 <= MAX_FSR)&&(summary->fsr18_max < data->fsr8 )) summary->fsr18_max = data->fsr18;
	if ((data->fsr19 >= MIN_FSR)&&(data->fsr19 <= MAX_FSR)&&(summary->fsr19_max < data->fsr9 )) summary->fsr19_max = data->fsr19;

	if ((data->temp1 >= MIN_TEMP)&&(data->temp1 <= MAX_TEMP)&&(summary->temp1_max < data->temp1)) summary->temp1_max = data->temp1;
	if ((data->temp2 >= MIN_TEMP)&&(data->temp2 <= MAX_TEMP)&&(summary->temp2_max < data->temp2)) summary->temp2_max = data->temp2;
	if ((data->temp3 >= MIN_TEMP)&&(data->temp3 <= MAX_TEMP)&&(summary->temp3_max < data->temp3)) summary->temp3_max = data->temp3;
	if ((data->temp4 >= MIN_TEMP)&&(data->temp4 <= MAX_TEMP)&&(summary->temp4_max < data->temp4)) summary->temp4_max = data->temp4;
	if ((data->temp5 >= MIN_TEMP)&&(data->temp5 <= MAX_TEMP)&&(summary->temp5_max < data->temp5)) summary->temp5_max = data->temp5;

	if ((data->cap1n >= MIN_CAP)&&(data->cap1n <= MAX_CAP)&&(data->cap1s >= MIN_CAP)&&(data->cap1s <= MAX_CAP)) {
		int16_t cap_delta = data->cap1n - data->cap1s;
		if (summary->cap1_delta_min > cap_delta) summary->cap1_delta_min = cap_delta;
		if (summary->cap1_delta_max < cap_delta) summary->cap1_delta_max = cap_delta;
	}

	if ((data->cap2n >= MIN_CAP)&&(data->cap2n <= MAX_CAP)&&(data->cap2s >= MIN_CAP)&&(data->cap2s <= MAX_CAP)) {
		int16_t cap_delta = data->cap2n - data->cap2s;
		if (summary->cap2_delta_min > cap_delta) summary->cap2_delta_min = cap_delta;
		if (summary->cap2_delta_max < cap_delta) summary->cap2_delta_max = cap_delta;
	}

	if ((data->cap3n >= MIN_CAP)&&(data->cap3n <= MAX_CAP)&&(data->cap3s >= MIN_CAP)&&(data->cap3s <= MAX_CAP)) {
		int16_t cap_delta = data->cap3n - data->cap3s;
		if (summary->cap3_delta_min > cap_delta) summary->cap3_delta_min = cap_delta;
		if (summary->cap3_delta_max < cap_delta) summary->cap3_delta_max = cap_delta;
	}

	if ((data->cap4n >= MIN_CAP)&&(data->cap4n <= MAX_CAP)&&(data->cap4s >= MIN_CAP)&&(data->cap4s <= MAX_CAP)) {
		int16_t cap_delta = data->cap4n - data->cap4s;
		if (summary->cap4_delta_min > cap_delta) summary->cap4_delta_min = cap_delta;
		if (summary->cap4_delta_max < cap_delta) summary->cap4_delta_max = cap_delta;
	}

	if ((data->cap5n >= MIN_CAP)&&(data->cap5n <= MAX_CAP)&&(data->cap5s >= MIN_CAP)&&(data->cap5s <= MAX_CAP)) {
		int16_t cap_delta = data->cap5n - data->cap5s;
		if (summary->cap5_delta_min > cap_delta) summary->cap5_delta_min = cap_delta;
		if (summary->cap5_delta_max < cap_delta) summary->cap5_delta_max = cap_delta;
	}

	if ((data->cap6n >= MIN_CAP)&&(data->cap6n <= MAX_CAP)&&(data->cap6s >= MIN_CAP)&&(data->cap6s <= MAX_CAP)) {
		int16_t cap_delta = data->cap4n - data->cap4s;
		if (summary->cap6_delta_min > cap_delta) summary->cap6_delta_min = cap_delta;
		if (summary->cap6_delta_max < cap_delta) summary->cap6_delta_max = cap_delta;
	}
}

void measure_reset_summary(reid_ble_summary_packet_t* summary)
{
	summary->time_s_start = 0xFFFFFFFF;
	summary->time_s_end = 0;
	summary->num_samples = 0;
	summary->vdd_mv_min = 9999;
	summary->index = flash_next_high_index();
	summary->is_synced = 0xFFFF;
	summary->fsr1_max = 0;
	summary->fsr2_max = 0;
	summary->fsr3_max = 0;
	summary->fsr4_max = 0;
	summary->fsr5_max = 0;
	summary->fsr6_max = 0;
	summary->fsr7_max = 0;
	summary->fsr8_max = 0;
	summary->fsr9_max = 0;
	summary->fsr10_max = 0;
	summary->fsr11_max = 0;
	summary->fsr12_max = 0;
	summary->fsr13_max = 0;
	summary->fsr14_max = 0;
	summary->fsr15_max = 0;
	summary->fsr16_max = 0;
	summary->fsr17_max = 0;
	summary->fsr18_max = 0;
	summary->fsr19_max = 0;
	summary->temp1_max = MIN_TEMP-1;
	summary->temp2_max = MIN_TEMP-1;
	summary->temp3_max = MIN_TEMP-1;
	summary->temp4_max = MIN_TEMP-1;
	summary->temp5_max = MIN_TEMP-1;
	summary->cap1_delta_min = 9999;
	summary->cap1_delta_max = -9999;
	summary->cap2_delta_min = 9999;
	summary->cap2_delta_max = -9999;
	summary->cap3_delta_min = 9999;
	summary->cap3_delta_max = -9999;
	summary->cap4_delta_min = 9999;
	summary->cap4_delta_max = -9999;
	summary->cap5_delta_min = 9999;
	summary->cap5_delta_max = -9999;
	summary->cap6_delta_min = 9999;
	summary->cap6_delta_max = -9999;
}