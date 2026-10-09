/*===========================================
//
// ble_REID.h
// Written by Alex Gilmour
// Copyright (c) 2022, CarbonCircuits
// All rights reserved.
//
//=========================================*/

// BLE link: SoftDevice, advertising and the Nordic UART Service.

#ifndef BLE_REID_H
#define BLE_REID_H

#include <stdint.h>

// Starts the SoftDevice, the Nordic UART Service and fast advertising.
// rx_handler is called for every byte received, from the BLE event interrupt.
void ble_reid_init(void (*rx_handler)(uint8_t rx_byte));

void ble_reid_force_disconnect(void);
// Sleep-state BLE: drop any connection and advertise slowly (APP_ADV_INTERVAL_SLOW).
void ble_reid_enter_lifeline(void);
void ble_advertise_again(void);

// Blocking send for command replies: retries while the SoftDevice queue is full.
void ble_reid_tx(uint8_t* data, uint16_t length);
void ble_reid_tx_stream(uint8_t* data, uint16_t length); // SEN-59: non-blocking, for the high-rate stream flush
// Largest notification payload the current link can carry.
uint16_t ble_reid_max_tx_len(void);
uint8_t ble_is_connected(void);

#endif // #define BLE_REID_H
