/*===========================================
//
// main.h
// Written by Alex Gilmour
// Copyright (c) 2024, CarbonCircuits
// All rights reserved.
//
//=========================================*/

#ifndef MAIN_H_
#define MAIN_H_

#include "configure_firmware.h"
#include "messaging_defines.h"
#include <stdint.h>

extern volatile reid_ble_packet_t ble_data;
extern volatile reid_ble_summary_packet_t summary_data;

#endif // MAIN_H_ 