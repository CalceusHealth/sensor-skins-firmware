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


#define DEVICE_FW_VERSION	0x00020021

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
	#define SEND_BLE_MEAS_AS_ASCII

#define MAIN_LOOP_TIME_MS	125

/*#define MEASURE_TEMP_EVERY_N	(60*1000/MAIN_LOOP_TIME_MS)
#define NEW_SUMMARY_EVERY_N		(300*1000/MAIN_LOOP_TIME_MS)*/

//#define MEASURE_TEMP_EVERY_N	(12*1000/MAIN_LOOP_TIME_MS) // each measurement iteration will cycle through a different sensor
#define MEASURE_TEMP_EVERY_N	(80) // each measurement iteration will cycle through a different sensor
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


#define MSG_TX_BUFFER_SIZE				1024ul
#define MSG_RX_BUFFER_SIZE				256ul

#endif // CONFIGURE_FIRMWARE_H_ 


