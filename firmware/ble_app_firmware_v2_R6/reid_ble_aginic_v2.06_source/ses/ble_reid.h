/*===========================================
//
// ble_REID.h
// Written by Alex Gilmour
// Copyright (c) 2022, CarbonCircuits
// All rights reserved.
//
//=========================================*/


#ifndef BLE_REID_H
#define BLE_REID_H

#include <stdint.h>
#include "messaging.h"
#include "configure_firmware.h"
#include "flash.h"

#include "ble_hci.h"
#include "ble_advdata.h"
#include "ble_advertising.h"
#include "ble_conn_params.h"
#include "nrf_sdh.h"
#include "nrf_sdh_soc.h"
#include "nrf_sdh_ble.h"
#include "nrf_ble_gatt.h"
#include "nrf_ble_qwr.h"
#include "ble_nus.h"
#include "nordic_common.h"
#include "nrf.h"
#include "app_util_platform.h"

#include "app_timer.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

void ble_reid_init(void);
void ble_reid_deinit(void);

void ble_reid_force_disconnect(void);
void ble_advertise_again(void);

void ble_reid_tx(uint8_t* data, uint16_t length);
uint8_t ble_is_connected(void);

#endif // #define BLE_REID_H