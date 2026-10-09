/*===========================================
//
// power.h
// Written by Alex Gilmour
// Copyright (c) 2024, CarbonCircuits
// All rights reserved.
//
//=========================================*/

// When the device runs and when it sleeps.
//
//   AWAKE              one frame per ;CF period
//     (idle-connected) connected but nothing happening for IDLE_CONNECTED_TIMEOUT_MS:
//                      frames slow to IDLE_CONNECTED_PERIOD_MS, IMU in wake-on-motion
//   SLEEP              idle and not connected for IDLE_SLEEP_TIMEOUT_MS (or
//                      CHARGING_IDLE_SLEEP_TIMEOUT_MS on the charger): slow
//                      advertising, no measurements; wakes on motion or a connection
//   PROTECTION SLEEP   battery too low: IMU off; wakes only once the battery has
//                      recovered, powers off completely if it keeps falling
//
// The thresholds and timeouts are in configure_firmware.h.

#ifndef POWER_H_
#define POWER_H_

#include <stdint.h>
#include "measure.h"

// ---- inputs from the command protocol ----------------------------------------
void power_set_bench_keepawake(uint8_t on);	// ;CK 1: never idle-sleep (bench use)
void power_set_session_active(uint8_t on);		// ;CX 1: a recording session is in progress
uint8_t power_session_active(void);
// Any command or query counts as BLE activity for BLE_ACTIVITY_HOLD_MS.
void power_note_command(void);
// ;CF: frames per second while awake, clamped to MIN/MAX_STREAM_RATE_HZ.
void power_set_frame_rate_hz(uint32_t hz);
uint16_t power_frame_period_ms(void);
// The next awake tick does not measure a frame (;QB has just used the ADC).
void power_skip_next_frame(void);

// ---- the main loop's tick ----------------------------------------------------
// The main loop waits power_tick_period_ms() between ticks. On each tick:
//
//   if (power_tick_begin()) {              // 0 = asleep or skipping: no frame
//       frame = measure_sensors();
//       power_frame_measured(frame);       // before the frame is sent: going
//       ...send the frame...               // idle-connected zeroes its IMU values
//       power_tick_end(frame);             // may put the device to sleep
//   }
uint16_t power_tick_period_ms(void);
uint8_t power_tick_begin(void);
void power_frame_measured(const reid_ble_packet_t* frame);
void power_tick_end(const reid_ble_packet_t* frame);

#endif // POWER_H_
