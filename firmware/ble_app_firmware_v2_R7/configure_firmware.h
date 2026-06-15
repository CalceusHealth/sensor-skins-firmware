/*===========================================
//
// configure_firmware.h
// Written by Alex Gilmour
// Copyright (c) 2024, CarbonCircuits
// All rights reserved.
//
//=========================================*/


#ifndef CONFIGURE_FIRMWARE_H_
#define CONFIGURE_FIRMWARE_H_


// Firmware version, reported to the app as MAJOR.MINOR.PATCH (e.g. 2.0.40).
// Encoded into a uint32 as (MAJOR<<16)|(MINOR<<8)|PATCH so the on-flash layout is
// identical to the old hand-written hex (0x00020028 == 2.0.40, 0x00020027 == 2.0.39).
#define DEVICE_FW_VERSION_MAJOR	2
#define DEVICE_FW_VERSION_MINOR	0
#define DEVICE_FW_VERSION_PATCH	50
#define DEVICE_FW_VERSION	((DEVICE_FW_VERSION_MAJOR << 16) | (DEVICE_FW_VERSION_MINOR << 8) | DEVICE_FW_VERSION_PATCH)

#define FSR_ADC_GAIN NRF_SAADC_GAIN1_2

#define STREAM_BINARY_V2_MAX_ROWS 3

// final release switches
	#define ENABLE_CODE_PROTECT
	#define ENABLE_HARDFAULT_RECOVERY
	#define ENABLE_COPE_WITH_ERRORS
//	#define ENABLE_BAD_RESET_DETECTION
	#define ENABLE_DEBUG
	#define ENABLE_SLEEP
	#define ENABLE_SLEEP_SMART_IDLE
//	#define ENABLE_SHUTDOWN
	#define WAIT_FOR_TX_OF_EVERY_PACKET
	#define SEND_EVERY_MEAS_OVER_BLE
//	#define STREAM_PROTOCOL_ASCII_V1
//	#define ENABLE_FLASH_SUMMARY	// legacy 5-min summary-to-flash; OFF for stream/nostream (raw-NVMC write drops BLE every ~343s). Enabled only for the `summary` build variant.
	#define STREAM_PROTOCOL_BINARY_V2	// SEN-53: binary stream migration (>8Hz). ASCII_V1 above disabled (exactly one allowed).

#define MAIN_LOOP_TIME_MS	125		// default loop period (8Hz); runtime value lives in main_loop_period_ms, settable via ;CF <hz>
#define STREAM_BINARY_V2_MAX_LATENCY_MS (2 * MAIN_LOOP_TIME_MS)
// Bounds for the runtime ;CF <hz> sample-rate command (SEN-57). Requesting above
// the real ceiling just makes the loop run measurement-bound; the achieved rate
// (device_time_ms deltas) then reveals the ceiling.
#define MIN_STREAM_RATE_HZ	1
#define MAX_STREAM_RATE_HZ	200

#if defined(STREAM_PROTOCOL_ASCII_V1) && defined(STREAM_PROTOCOL_BINARY_V2)
#error Only one stream protocol may be enabled
#endif

#if !defined(STREAM_PROTOCOL_ASCII_V1) && !defined(STREAM_PROTOCOL_BINARY_V2)
#error A stream protocol must be enabled
#endif

/*#define MEASURE_TEMP_EVERY_N	(60*1000/MAIN_LOOP_TIME_MS)
#define NEW_SUMMARY_EVERY_N		(300*1000/MAIN_LOOP_TIME_MS)*/

//#define MEASURE_TEMP_EVERY_N	(12*1000/MAIN_LOOP_TIME_MS) // each measurement iteration will cycle through a different sensor
#define MEASURE_TEMP_EVERY_N	(80) // (legacy, frame-count) superseded by TEMP_SAMPLE_PERIOD_MS
// Time-based temp cadence (SEN-58): the LMT01 read is a ~90ms blocking pulse-count,
// so gate it on elapsed time, not frame count -- otherwise at high stream rates it
// fires far too often (e.g. every 0.8s at 100Hz) and stalls the loop. One sensor
// per period -> each of the 5 sensors updated every 5x this. 60s -> 5min/sensor.
#define TEMP_SAMPLE_PERIOD_MS	60000
#define NEW_SUMMARY_EVERY_N		(300*1000/MAIN_LOOP_TIME_MS)

#define MSG_MAX_RECORDS_PER_REQUEST		101

#define FSR_SLEEP_THRESHOLD		10
#define FSR_SLEEP_NUM			15 // this many under the threshold
#define SLEEP_NUM_MEAS			600 // for this many consecutive measurements
#define SLEEP_CHECK_EVERY_MS	5000 // check every N after sleeping

#define CHARGING_STATE_SAMPLE_EVERY_N_BATTERY_UPDATES	5
#define CHARGING_STATE_HISTORY_SAMPLES				5
#define CHARGING_STATE_CONFIRM_MIN_C_SAMPLES		3
#define CHARGING_STATE_CLEAR_AFTER_NON_C_SAMPLES	3
#define LOW_BATTERY_SLEEP_MIN_MV					3250
#define LOW_BATTERY_WAKE_MIN_MV					3450
#define CHARGE_RECOVERY_VBAT_MIN_MV				3450
#define CHARGING_IDLE_SLEEP_TIMEOUT_MS			15000
#define IDLE_SLEEP_TIMEOUT_MS					180000
// How often vbat is sampled for the battery average / low-battery protection.
// Decoupled from the sensor frame rate (8Hz now, 50-100Hz binary later): one
// vbat read per period regardless of stream rate, so it never burdens the
// sample-rate ceiling. Kept short enough that the average (VBAT_AVERAGE_N deep)
// reacts to the acute discharge knee near LOW_BATTERY_SLEEP_MIN_MV within ~1 min.
#define VBAT_SAMPLE_PERIOD_MS					5000
#define BLE_ACTIVITY_HOLD_MS					120000
#define FSR_DELTA_THRESHOLD						150
#define FSR_DELTA_WAKE_MIN						2
#define CAP_DELTA_THRESHOLD						80
#define CAP_DELTA_WAKE_MIN						2

#define REID_LHS
//#define REID_RHS

#ifdef REID_LHS
#ifdef REID_RHS
#error Both sides defined
#else
#define DEVICE_NAME "REIDLHS"
#endif
#else
#ifdef REID_RHS
#define DEVICE_NAME "REIDRHS"
#else
#error No side defined
#endif
#endif
	
    #define BLE_TX_POWER_ADV	(4)
    #define BLE_TX_POWER_CONN	(4)
    //Supported tx_power values: -40dBm, -20dBm, -16dBm, -12dBm, -8dBm, -4dBm, 0dBm, +3dBm and +4dBm.

/**< The advertising interval (in units of 0.625 ms. This value corresponds to 40 ms). */
//#define APP_ADV_INTERVAL				64                                          
//#define APP_ADV_INTERVAL				160                                          
//#define APP_ADV_INTERVAL				320
//#define APP_ADV_INTERVAL				244
//#define APP_ADV_INTERVAL				338
//#define APP_ADV_INTERVAL				510
//#define APP_ADV_INTERVAL				668
#define APP_ADV_INTERVAL				874
//#define APP_ADV_INTERVAL				1216
//#define APP_ADV_INTERVAL				1364
//#define APP_ADV_INTERVAL				1636
//#define APP_ADV_INTERVAL				2056

// Slow advertising interval used by the BLE lifeline (sleep-state advertising).
// 8000 units * 0.625 ms = 5 seconds between adverts. ~5 uA average system current.
#define APP_ADV_INTERVAL_SLOW			8000


#define MSG_TX_BUFFER_SIZE				1024ul
#define MSG_RX_BUFFER_SIZE				256ul

#endif // CONFIGURE_FIRMWARE_H_ 


