/*===========================================
//
// messaging.h
// Written by Alex Gilmour
// Copyright (c) 2024, CarbonCircuits
// All rights reserved.
//
//=========================================*/

#ifndef MESSAGING_H_
#define MESSAGING_H_

#include <stdint.h>
#include "flash.h"
#include "configure_firmware.h"
#include "main.h"
#include "ble_reid.h"
#include "messaging_defines.h"

// Initialises packets
void msg_init(void);
void msg_deinit(void);

// AUTOMATIC RESPONSES OCCUR IN HERE
void msg_process_packet(void);

// INTERFACES FOR UART MODULE are covered by the tx/rx commands

void msg_rx_next_byte(uint8_t rx_byte);
uint8_t msg_rx_accept_data(void);

#endif // MESSAGING_H_ 

