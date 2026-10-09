/*===========================================
//
// adc.c
// Written by Alex Gilmour
// Copyright (c) 2024, CarbonCircuits
// All rights reserved.
//
//=========================================*/

#include "drivers/adc.h"
#include "drivers/board.h"
#include "drivers/system.h"
#include "configure_firmware.h"
#include "nrf_gpio.h"
#include "nrf_delay.h"
#include "nrfx_saadc.h"
#include "nrf_saadc.h"

#define ADC_SCALING_FACTOR	6

#define ADC_CHANNEL(_pin_p, _pin_n, _resistor_p, _gain, _reference, _acq_time, _mode)	\
{																						\
	.resistor_p = _resistor_p,															\
	.resistor_n = NRF_SAADC_RESISTOR_DISABLED,											\
	.gain       = _gain,																\
	.reference  = _reference,															\
	.acq_time   = _acq_time,															\
	.mode       = _mode,																\
	.burst      = NRF_SAADC_BURST_DISABLED,												\
	.pin_p      = _pin_p,																\
	.pin_n      = _pin_n																\
}

// VDD and VBAT: single-ended against the internal 0.6 V reference, gain 1/6.
#define ADC_CHANNEL_SUPPLY(_pin)	ADC_CHANNEL(_pin, NRF_SAADC_INPUT_DISABLED, NRF_SAADC_RESISTOR_DISABLED, NRF_SAADC_GAIN1_6, NRF_SAADC_REFERENCE_INTERNAL, NRF_SAADC_ACQTIME_40US, NRF_SAADC_MODE_SINGLE_ENDED)
// FSR bank: differential VDD - pin against VDD/4, so the reading is ratiometric.
#define ADC_CHANNEL_FSR(_pin)		ADC_CHANNEL(NRF_SAADC_INPUT_VDD, _pin, NRF_SAADC_RESISTOR_PULLUP, FSR_ADC_GAIN, NRF_SAADC_REFERENCE_VDD4, NRF_SAADC_ACQTIME_10US, NRF_SAADC_MODE_DIFFERENTIAL)
// Cap electrode: the same pin sampled with the internal pull-up or pull-down.
#define ADC_CHANNEL_CAP(_pin, _pull)	ADC_CHANNEL(_pin, NRF_SAADC_INPUT_DISABLED, _pull, NRF_SAADC_GAIN1_4, NRF_SAADC_REFERENCE_VDD4, NRF_SAADC_ACQTIME_3US, NRF_SAADC_MODE_SINGLE_ENDED)

static const nrf_saadc_channel_config_t adc_channel_vdd			= ADC_CHANNEL_SUPPLY(ADC_VDD);
static const nrf_saadc_channel_config_t adc_channel_vbat		= ADC_CHANNEL_SUPPLY(ADC_VBAT);
static const nrf_saadc_channel_config_t adc_channel_bank[ADC_FSR_BANKS]	= { ADC_CHANNEL_FSR(ADC_FSR_CH0), ADC_CHANNEL_FSR(ADC_FSR_CH1), ADC_CHANNEL_FSR(ADC_FSR_CH2) };
static const nrf_saadc_channel_config_t adc_channel_cap_high[ADC_CAP_BANKS]	= { ADC_CHANNEL_CAP(ADC_CAP_CH0, NRF_SAADC_RESISTOR_PULLUP), ADC_CHANNEL_CAP(ADC_CAP_CH1, NRF_SAADC_RESISTOR_PULLUP) };
static const nrf_saadc_channel_config_t adc_channel_cap_low[ADC_CAP_BANKS]	= { ADC_CHANNEL_CAP(ADC_CAP_CH0, NRF_SAADC_RESISTOR_PULLDOWN), ADC_CHANNEL_CAP(ADC_CAP_CH1, NRF_SAADC_RESISTOR_PULLDOWN) };

static const nrfx_saadc_config_t adc_config =
{
	.resolution         = NRF_SAADC_RESOLUTION_12BIT,
	.oversample         = NRF_SAADC_OVERSAMPLE_DISABLED,
	.interrupt_priority = 2,
	.low_power_mode     = 0
};

// SEN-102: the R4 divider is R43:R44 = 100k:100k (÷2) with C2 = 100n, giving
// the ADC node a 5 ms time constant. The old 21:10 (×3.1) constant was an
// empirical calibration that compensated for sampling at exactly 1τ (the old
// 5 ms settle reads ~63% of VBAT/2; 3.1 ≈ 2/(1-e^-1)) — i.e. accuracy rode on
// C2 tolerance and code-path timing, not on a real ratio. Reads now settle
// >=6τ (VBAT_SETTLE_MS) and use the true ÷2, which is ratio-metric and
// per-unit robust. This also makes the fresh (QB RAW) and in-sequence paths
// agree in scale (closes the SEN-52 "fresh reads ~1.5x high" mystery).
// BENCH-VERIFY against a multimeter on >=3 units before field rollout.
#define VDIV_VBAT_R1	(10)
#define VDIV_VBAT_R2	(10)

typedef enum adc_current_state_t
{
	ADC_INIT = 0,
	ADC_IDLE,
	ADC_ONESHOT,
	ADC_RUNNING,
	ADC_DONE
} adc_current_state_t;

static volatile adc_current_state_t adc_current_state;

static void adc_event_handler(nrfx_saadc_evt_t const *p_event)
{
	if (p_event->type != NRFX_SAADC_EVT_DONE) return;

	if (adc_current_state == ADC_ONESHOT) {
		adc_current_state = ADC_IDLE;
	} else if (adc_current_state == ADC_RUNNING) {
		// end of a bank scan (see adc_banks_scan)
		adc_current_state = ADC_DONE;
		NRF_SAADC->SAMPLERATE = 0;
	}
}

// The SAADC is initialised once at startup and left running (SEN-58); channel
// 0 is reconfigured by whichever read runs next.
void adc_init(void)
{
	nrfx_err_t ret_err;
	adc_current_state = ADC_INIT;
	do {
		nrfx_saadc_abort();
		nrfx_saadc_uninit();
		ret_err = nrfx_saadc_init(&adc_config, adc_event_handler);
		for (uint8_t i = 1; (i <= 8) && (ret_err == NRFX_SUCCESS); ++i) ret_err = nrfx_saadc_channel_uninit(i % 8); // 1..7, then 0
		if (ret_err == NRFX_SUCCESS) ret_err = nrfx_saadc_channel_init(0,&adc_channel_vdd);
	} while (ret_err != NRFX_SUCCESS);
	adc_current_state = ADC_IDLE;
}

// ---- single-channel reads (VDD, battery) -----------------------------------
// These use channel 0 and blocking conversions.

static uint16_t adc_raw_to_mv(int32_t reading)
{
	return (uint16_t) (((reading) * ADC_SCALING_FACTOR * (600/8))/(ADC_TOP / 8));
}

// Sum of `samples` conversions on channel 0, clamped at 0; 0 if the driver
// reported an error. ret_err carries in the status of the caller's set-up.
static int32_t adc_convert(nrfx_err_t ret_err, uint8_t samples)
{
	int32_t reading = 0;
	nrf_saadc_value_t adc_result = 0;

	for (uint8_t i=0; i<samples; ++i)
	{
		if (ret_err == NRFX_SUCCESS) ret_err = nrfx_saadc_sample_convert(0,&adc_result);
		reading += adc_result;
	}
	if (reading<0) reading=0;
	return (ret_err == NRFX_SUCCESS) ? reading : 0;
}

// Points channel 0 at an input and converts.
static int32_t adc_oneshot(const nrf_saadc_channel_config_t* channel, uint8_t samples)
{
	adc_current_state = ADC_ONESHOT;
	nrfx_err_t ret_err = nrfx_saadc_channel_uninit(0);
	if (ret_err == NRFX_SUCCESS) ret_err = nrfx_saadc_channel_init(0,channel);
	int32_t reading = adc_convert(ret_err, samples);
	adc_current_state = ADC_IDLE;
	return reading;
}

uint16_t adc_read_vdd_mv(void)
{
	if ((adc_current_state != ADC_IDLE)||(nrfx_saadc_is_busy())) return 0;
	return adc_raw_to_mv(adc_oneshot(&adc_channel_vdd, 1));
}

// SEN-102: enable the gated divider so it can settle (>=6 tau = VBAT_SETTLE_MS)
// before adc_read_vbat_raw_presettled() samples it. Split out so the awake
// measurement sequence can settle across frames instead of blocking ~30 ms.
void adc_vbat_settle_begin(void)
{
	nrf_gpio_cfg_output(PIN_VBAT_ON);
	nrf_gpio_pin_set(PIN_VBAT_ON);
}

// SEN-102: convert an already-settled divider (adc_vbat_settle_begin called
// >= VBAT_SETTLE_MS earlier). Clears the divider gate when done. A sample
// taken with the gate accidentally off reads ~0, which callers discard, so
// stale settle state after a sleep transition costs one skipped sample.
int32_t adc_read_vbat_raw_presettled(void)
{
	if (adc_current_state == ADC_DONE) adc_current_state = ADC_IDLE;

	for (uint8_t wait_count = 0; wait_count < 10; ++wait_count)
	{
		if ((adc_current_state == ADC_IDLE) && (!nrfx_saadc_is_busy())) break;
		nrf_delay_ms(1);
		if (adc_current_state == ADC_DONE) adc_current_state = ADC_IDLE;
	}

	if ((adc_current_state != ADC_IDLE)||(nrfx_saadc_is_busy())) { nrf_gpio_pin_clear(PIN_VBAT_ON); return 0; }

	int32_t reading = adc_oneshot(&adc_channel_vbat, ADC_AVG_SAMPLES);
	nrf_gpio_pin_clear(PIN_VBAT_ON);
	return reading;
}

// Blocking read: settle + convert. For contexts with no frame budget (sleep
// loop, QB query).
int32_t adc_read_vbat_raw(void)
{
	adc_vbat_settle_begin();
	nrf_delay_ms(VBAT_SETTLE_MS);
	return adc_read_vbat_raw_presettled();
}

// Tears the SAADC down and brings it back up around one settled read.
int32_t adc_read_vbat_raw_fresh(void)
{
	nrfx_saadc_abort();
	nrfx_saadc_uninit();
	nrfx_err_t ret_err = nrfx_saadc_init(&adc_config, adc_event_handler);
	if (ret_err == NRFX_SUCCESS) ret_err = nrfx_saadc_channel_init(0,&adc_channel_vbat);

	nrf_gpio_cfg_output(PIN_VBAT_ON);
	nrf_gpio_pin_set(PIN_VBAT_ON);
	nrf_delay_ms(VBAT_SETTLE_MS); // SEN-102: full settle; same scale as the normal path

	int32_t reading = adc_convert(ret_err, ADC_AVG_SAMPLES);

	nrf_gpio_pin_clear(PIN_VBAT_ON);
	nrfx_saadc_abort();
	nrfx_saadc_uninit();
	adc_init();

	return reading;
}

uint16_t adc_vbat_raw_to_mv(int32_t reading)
{
	if (reading < 0) reading = 0;
	reading /= ADC_AVG_SAMPLES;
	return (uint16_t) (((reading) * ADC_SCALING_FACTOR * (600/8) * (VDIV_VBAT_R1 + VDIV_VBAT_R2))/(ADC_TOP / 8)/VDIV_VBAT_R2);
}

// ---- sensor multiplexers ---------------------------------------------------
// MUX_ON (active low) switches the supply of the five 74LV4051 muxes and the
// FSR dividers, so the sensors draw nothing between frames. The three select
// lines of the FSR muxes are shared, as are those of the cap muxes.

// Settling time after changing a mux input, in CPU cycles.
#define FSR_SETTLING	10000
// SEN-58: cap settling is separate so cap can be tuned without touching FSR.
// These busy-loops dominate the CAP-section time (conversions + channel-init
// ruled out empirically).
#define CAP_SETTLING	2000

static const uint8_t fsr_select_pins[3] = { PIN_FSR_S0, PIN_FSR_S1, PIN_FSR_S2 };
static const uint8_t cap_select_pins[3] = { PIN_CAP_S0, PIN_CAP_S1, PIN_CAP_S2 };
static uint8_t fsr_input = 0;	// input currently selected on the FSR muxes
static uint8_t cap_input = 0;

// Moves a select bus to a new input: raise the bits that turn on before
// dropping the ones that turn off (most significant first).
static void mux_select(const uint8_t select_pins[3], uint8_t* current, uint8_t input)
{
	for (int8_t bit = 2; bit >= 0; --bit) if ((input & ~*current) & (1u << bit)) nrf_gpio_pin_set(select_pins[bit]);
	for (int8_t bit = 2; bit >= 0; --bit) if ((*current & ~input) & (1u << bit)) nrf_gpio_pin_clear(select_pins[bit]);
	*current = input;
}

// Back to input 0 (no sensor).
static void mux_release(const uint8_t select_pins[3], uint8_t* current)
{
	for (uint8_t bit = 0; bit < 3; ++bit) nrf_gpio_pin_clear(select_pins[bit]);
	*current = 0;
}

void adc_sensors_on(void)
{
	mux_release(fsr_select_pins, &fsr_input);
	mux_release(cap_select_pins, &cap_input);
	nrf_gpio_pin_clear(PIN_MUX_ON);
}

void adc_sensors_off(void)
{
	mux_release(fsr_select_pins, &fsr_input);
	mux_release(cap_select_pins, &cap_input);
	nrf_gpio_pin_set(PIN_MUX_ON);
}

// ---- FSR banks: EasyDMA scan (SEN-58) --------------------------------------
// The 3 FSR bank channels are read in ONE DMA scan instead of 3 separate
// blocking single-channel reads, eliminating the per-conversion driver
// overhead. The scan is summed ADC_AVG_SAMPLES times.
#define ADC_BANK_SCAN_TIMEOUT 200000u
static nrf_saadc_value_t bank_scan_buf[ADC_FSR_BANKS];
static int32_t bank_cache[ADC_FSR_BANKS];

void adc_fsr_begin(void)
{
	static const uint8_t bank_pins[ADC_FSR_BANKS] = { PIN_FSR_CH0, PIN_FSR_CH1, PIN_FSR_CH2 };

	// Configure channels 0/1/2 = bank1/2/3 for scan mode (active_channels == 3).
	for (uint8_t i = 0; i < ADC_FSR_BANKS; ++i) nrf_gpio_cfg(bank_pins[i], GPIO_PIN_CNF_DIR_Input, GPIO_PIN_CNF_INPUT_Disconnect, GPIO_PIN_CNF_PULL_Disabled, GPIO_PIN_CNF_DRIVE_H0D1, GPIO_PIN_CNF_SENSE_Disabled);
	for (uint8_t i = 0; i < ADC_FSR_BANKS; ++i) nrfx_saadc_channel_uninit(i);
	for (uint8_t i = 0; i < ADC_FSR_BANKS; ++i) nrfx_saadc_channel_init(i,&adc_channel_bank[i]);
	adc_current_state = ADC_IDLE;
}

void adc_fsr_end(void)
{
	mux_release(fsr_select_pins, &fsr_input);
	// Release the scan channels so the cap/vbat single-channel reads behave as
	// before (channel 0 is left for the next read to reconfigure).
	nrfx_saadc_channel_uninit(1);
	nrfx_saadc_channel_uninit(2);
	adc_current_state = ADC_IDLE;
}

static void adc_banks_scan(void)
{
	bank_cache[0] = 0; bank_cache[1] = 0; bank_cache[2] = 0;
	for (uint8_t s=0; s<ADC_AVG_SAMPLES; ++s) {
		uint32_t to;
		adc_current_state = ADC_RUNNING;
		if (nrfx_saadc_buffer_convert(bank_scan_buf, 3) != NRFX_SUCCESS) { adc_current_state = ADC_IDLE; return; }
		// buffer_convert triggered TASKS_START; wait STARTED, then SAMPLE the scan.
		to = 0; while ((nrf_saadc_event_check(NRF_SAADC_EVENT_STARTED) == 0) && (++to < ADC_BANK_SCAN_TIMEOUT)) {}
		if (nrfx_saadc_sample() != NRFX_SUCCESS) { nrfx_saadc_abort(); adc_current_state = ADC_IDLE; return; }
		// handler sets ADC_DONE on END (buffer of 3 filled = one scan of 3 channels).
		to = 0; while ((adc_current_state != ADC_DONE) && (++to < ADC_BANK_SCAN_TIMEOUT)) {}
		if (adc_current_state != ADC_DONE) { nrfx_saadc_abort(); adc_current_state = ADC_IDLE; return; }
		adc_current_state = ADC_IDLE;
		bank_cache[0] += bank_scan_buf[0];
		bank_cache[1] += bank_scan_buf[1];
		bank_cache[2] += bank_scan_buf[2];
	}
	if (bank_cache[0]<0) bank_cache[0]=0;
	if (bank_cache[1]<0) bank_cache[1]=0;
	if (bank_cache[2]<0) bank_cache[2]=0;
}

void adc_fsr_read(uint8_t input, int32_t bank[ADC_FSR_BANKS])
{
	mux_select(fsr_select_pins, &fsr_input, input);
	system_delay_cycles(FSR_SETTLING);

	// Two scans: the first one after a mux change is discarded. If the
	// converter is busy a scan is skipped, and then bank 1 reads 0 while banks
	// 2/3 keep the previous scan's values.
	uint8_t ready = 0;
	for (uint8_t scan = 0; scan < 2; ++scan) {
		ready = (adc_current_state == ADC_IDLE) && (!nrfx_saadc_is_busy());
		if (ready) adc_banks_scan();
	}
	bank[0] = ready ? bank_cache[0] : 0;
	bank[1] = bank_cache[1];
	bank[2] = bank_cache[2];
}

// ---- cap electrodes --------------------------------------------------------

void adc_cap_select(uint8_t input)
{
	mux_select(cap_select_pins, &cap_input, input);
	system_delay_cycles(CAP_SETTLING);
}

// The pad is sampled alternately with the SAADC's internal pull-down and
// pull-up (3 conversions each time, the last one kept), starting and ending on
// the pull-down, and the 4 high-minus-low differences are summed.
int32_t adc_cap_read(uint8_t bank)
{
	nrf_saadc_value_t adc_result = 0;
	nrf_saadc_value_t adc_result_low = 0;
	int32_t count = 0;

	if ((adc_current_state != ADC_IDLE)||(nrfx_saadc_is_busy())) return 0;
	adc_current_state = ADC_ONESHOT;
	nrfx_saadc_channel_uninit(0);
	nrfx_saadc_channel_uninit(1);
	nrfx_saadc_channel_init(0,&adc_channel_cap_high[bank]);
	nrfx_saadc_channel_init(1,&adc_channel_cap_low[bank]);
	for (uint8_t phase = 0; phase < 5; ++phase) {
		uint8_t high = phase & 1;	// low, high, low, high, low
		for (uint8_t i = 0; i < 3; ++i) nrfx_saadc_sample_convert(high ? 0 : 1, high ? &adc_result : &adc_result_low);
		if (phase > 0) count += (adc_result-adc_result_low);
	}
	adc_current_state = ADC_IDLE;
	return count;
}
