/*===========================================
//
// ble_reid.c
// Written by Alex Gilmour
// Copyright (c) 2024, CarbonCircuits
// All rights reserved.
//
//=========================================*/

#include "ble_reid.h"
#include "flash.h"
#include "system.h"
#include "configure_firmware.h"

#define APP_BLE_CONN_CFG_TAG            1                                           /**< A tag identifying the SoftDevice BLE configuration. */
#define NUS_SERVICE_UUID_TYPE           BLE_UUID_TYPE_VENDOR_BEGIN                  /**< UUID type for the Nordic UART Service (vendor specific). */
#define APP_BLE_OBSERVER_PRIO           3                                           /**< Application's BLE observer priority. You shouldn't need to modify this value. */
#define APP_ADV_DURATION                18000                                       /**< The advertising duration (3 minutes) in units of 10 milliseconds. */
#define MIN_CONN_INTERVAL               MSEC_TO_UNITS(10, UNIT_1_25_MS)             /**< Minimum acceptable connection interval (20 ms), Connection interval uses 1.25 ms units. */
#define MAX_CONN_INTERVAL               MSEC_TO_UNITS(20, UNIT_1_25_MS)             /**< Maximum acceptable connection interval (75 ms), Connection interval uses 1.25 ms units. */
#define SLAVE_LATENCY                   1                                           /**< Slave latency. */
#define CONN_SUP_TIMEOUT                MSEC_TO_UNITS(4000, UNIT_10_MS)             /**< Connection supervisory timeout (4 seconds), Supervision Timeout uses 10 ms units. */
#define FIRST_CONN_PARAMS_UPDATE_DELAY  APP_TIMER_TICKS(5000)                       /**< Time from initiating event (connect or start of notification) to first time sd_ble_gap_conn_param_update is called (5 seconds). */
#define NEXT_CONN_PARAMS_UPDATE_DELAY   APP_TIMER_TICKS(30000)                      /**< Time between each call to sd_ble_gap_conn_param_update after the first call (30 seconds). */
#define MAX_CONN_PARAMS_UPDATE_COUNT    3                                           /**< Number of attempts before giving up the connection parameter negotiation. */

BLE_NUS_DEF(m_nus, NRF_SDH_BLE_TOTAL_LINK_COUNT);                                   /**< BLE NUS service instance. */
NRF_BLE_GATT_DEF(m_gatt);                                                           /**< GATT module instance. */
NRF_BLE_QWR_DEF(m_qwr);                                                             /**< Context for the Queued Write module.*/
BLE_ADVERTISING_DEF(m_advertising);                                                 /**< Advertising module instance. */

static volatile uint16_t   m_conn_handle          = BLE_CONN_HANDLE_INVALID;        /**< Handle of the current connection. */
static volatile uint16_t   m_ble_nus_max_data_len = BLE_GATT_ATT_MTU_DEFAULT - 3;   /**< Maximum length of data (in bytes) that can be transmitted to the peer by the Nordic UART service module. */
static ble_uuid_t m_adv_uuids[]          =                                          /**< Universally unique service identifier. */
{
    {BLE_UUID_NUS_SERVICE, NUS_SERVICE_UUID_TYPE}
};

static ble_advertising_init_t init;
static ble_advdata_manuf_data_t manuf_data;
static ble_gap_adv_data_t ble_reid_adv;


static void ble_evt_handler(ble_evt_t const * p_ble_evt, void * p_context);
void gatt_evt_handler(nrf_ble_gatt_t * p_gatt, nrf_ble_gatt_evt_t const * p_evt);
static void nus_data_handler(ble_nus_evt_t * p_evt);
static void on_conn_params_evt(ble_conn_params_evt_t * p_evt);
static void on_adv_evt(ble_adv_evt_t ble_adv_evt);
static void nrf_qwr_error_handler(uint32_t nrf_error);
static void conn_params_error_handler(uint32_t nrf_error);

static volatile uint8_t ble_connection_status = 0;

volatile uint8_t bluetooth_dev_name[] = DEVICE_NAME;

void ble_reid_init(void)
{
    ret_code_t err_code = NRF_SUCCESS;
	ble_connection_status = 0;

/*******************************************
ble_stack_init - initializes the SoftDevice and the BLE event interrupt
********************************************/

	err_code = nrf_sdh_enable_request();													// Enable the SoftDevice
	//APP_ERROR_CHECK(err_code); // is dealt with in code section instead
	while ((err_code != NRF_SUCCESS)&&(err_code != NRF_ERROR_INVALID_STATE))
	{
		system_delay_cycles(10000);
		err_code = nrf_sdh_enable_request();
	}

	static uint32_t ram_start = 0;															// Configure the BLE stack using the default settings.
	err_code = nrf_sdh_ble_default_cfg_set(APP_BLE_CONN_CFG_TAG, &ram_start);
	APP_ERROR_CHECK(err_code);
	err_code = nrf_sdh_ble_enable(&ram_start);												// Enable BLE stack.
	APP_ERROR_CHECK(err_code);

	NRF_SDH_BLE_OBSERVER(m_ble_observer, APP_BLE_OBSERVER_PRIO, ble_evt_handler, NULL);		// Register a handler for BLE events.


/*******************************************
	gap_params_init - set up all the necessary GAP (Generic Access Profile) parameters of the device
********************************************/

	ble_gap_conn_params_t   gap_conn_params;
	ble_gap_conn_sec_mode_t sec_mode;

	BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);

	err_code = sd_ble_gap_device_name_set(&sec_mode,
										  (const uint8_t *) bluetooth_dev_name,
										  sizeof(bluetooth_dev_name));
	APP_ERROR_CHECK(err_code);

	memset(&gap_conn_params, 0, sizeof(gap_conn_params));

	gap_conn_params.min_conn_interval = MIN_CONN_INTERVAL;
	gap_conn_params.max_conn_interval = MAX_CONN_INTERVAL;
	gap_conn_params.slave_latency     = SLAVE_LATENCY;
	gap_conn_params.conn_sup_timeout  = CONN_SUP_TIMEOUT;

	err_code = sd_ble_gap_ppcp_set(&gap_conn_params);
	APP_ERROR_CHECK(err_code);


/*******************************************
	gatt_init - initialise the GATT library
********************************************/

	err_code = nrf_ble_gatt_init(&m_gatt, gatt_evt_handler);
	APP_ERROR_CHECK(err_code);

	err_code = nrf_ble_gatt_att_mtu_periph_set(&m_gatt, NRF_SDH_BLE_GATT_MAX_MTU_SIZE);
	APP_ERROR_CHECK(err_code);


/*******************************************
	services_init - initializing services that will be used by the application
********************************************/

	ble_nus_init_t     nus_init;
	nrf_ble_qwr_init_t qwr_init = {0};

	// Initialize Queued Write Module.
	qwr_init.error_handler = nrf_qwr_error_handler;

	err_code = nrf_ble_qwr_init(&m_qwr, &qwr_init);
	APP_ERROR_CHECK(err_code);

	// Initialize NUS.
	memset(&nus_init, 0, sizeof(nus_init));

	nus_init.data_handler = nus_data_handler;

	err_code = ble_nus_init(&m_nus, &nus_init);
	APP_ERROR_CHECK(err_code);

/*******************************************
	advertising_init - initializing the Advertising functionality
********************************************/

	memset(&init, 0, sizeof(init));

	init.advdata.name_type          = BLE_ADVDATA_FULL_NAME;
	init.advdata.short_name_len		= 8;
	init.advdata.include_appearance = false;
	init.advdata.flags              = BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE;
	
	init.advdata.uuids_complete.uuid_cnt = sizeof(m_adv_uuids) / sizeof(m_adv_uuids[0]);
	init.advdata.uuids_complete.p_uuids  = m_adv_uuids;

	//init.srdata.uuids_complete.uuid_cnt = sizeof(m_adv_uuids) / sizeof(m_adv_uuids[0]);
	//init.srdata.uuids_complete.p_uuids  = m_adv_uuids;

	init.config.ble_adv_fast_enabled  = true;
	init.config.ble_adv_fast_interval = APP_ADV_INTERVAL;
	init.config.ble_adv_fast_timeout  = APP_ADV_DURATION;
	init.evt_handler = on_adv_evt;

	m_advertising.adv_data = ble_reid_adv;

	err_code = ble_advertising_init(&m_advertising, &init);
	APP_ERROR_CHECK(err_code);

	ble_advertising_conn_cfg_tag_set(&m_advertising, APP_BLE_CONN_CFG_TAG);


/*******************************************
	conn_params_init - Function for initializing the Connection Parameters module
********************************************/

	ble_conn_params_init_t cp_init;

	memset(&cp_init, 0, sizeof(cp_init));

	cp_init.p_conn_params                  = NULL;
	cp_init.first_conn_params_update_delay = FIRST_CONN_PARAMS_UPDATE_DELAY;
	cp_init.next_conn_params_update_delay  = NEXT_CONN_PARAMS_UPDATE_DELAY;
	cp_init.max_conn_params_update_count   = MAX_CONN_PARAMS_UPDATE_COUNT;
	cp_init.start_on_notify_cccd_handle    = BLE_GATT_HANDLE_INVALID;
	cp_init.disconnect_on_fail             = false;
	cp_init.evt_handler                    = on_conn_params_evt;
	cp_init.error_handler                  = conn_params_error_handler;

	err_code = ble_conn_params_init(&cp_init);
	APP_ERROR_CHECK(err_code);


/*******************************************
	set TX power
********************************************/

	err_code = sd_ble_gap_tx_power_set(BLE_GAP_TX_POWER_ROLE_ADV, m_advertising.adv_handle, BLE_TX_POWER_ADV);
	APP_ERROR_CHECK(err_code);

/*******************************************
	advertising_start - start advertising
********************************************/
	
	sd_power_dcdc_mode_set(NRF_POWER_DCDC_ENABLE);
	err_code = ble_advertising_start(&m_advertising, BLE_ADV_MODE_FAST);
	APP_ERROR_CHECK(err_code);
}

void ble_reid_deinit(void)
{
	ret_code_t err_code = sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
	ble_advertising_start(&m_advertising, BLE_ADV_MODE_IDLE);
    sd_ble_gap_adv_stop(m_advertising.adv_handle);
    
    err_code = nrf_sdh_disable_request();
	while ((err_code != NRF_SUCCESS)&&(err_code != NRF_ERROR_INVALID_STATE))
	{
		system_delay_cycles(10000);
		err_code = nrf_sdh_disable_request();
	}
}

void ble_reid_force_disconnect(void)
{
	sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
	ble_advertising_start(&m_advertising, BLE_ADV_MODE_IDLE);
	sd_ble_gap_adv_stop(m_advertising.adv_handle);
}

void ble_advertise_again(void)
{
	ble_advertising_start(&m_advertising, BLE_ADV_MODE_FAST);
}

void ble_reid_tx(uint8_t* data, uint16_t length)
{
	if (ble_is_connected()==0) return;
	ret_code_t err_code;
	do
	{
		err_code = ble_nus_data_send(&m_nus, data, &length, m_conn_handle);
		if ((err_code != NRF_ERROR_INVALID_STATE) &&
			(err_code != NRF_ERROR_RESOURCES) &&
			(err_code != NRF_ERROR_NOT_FOUND))
		{
			// APP_ERROR_CHECK(err_code); // ignore the error when it comes up
		}
	#ifdef WAIT_FOR_TX_OF_EVERY_PACKET
	} while (err_code == NRF_ERROR_RESOURCES);
	#else // #ifdef WAIT_FOR_TX_OF_EVERY_PACKET
	} while (0);
	#endif // #ifdef WAIT_FOR_TX_OF_EVERY_PACKET
}

uint8_t ble_is_connected(void)
{
	return ble_connection_status;
}

static void ble_evt_handler(ble_evt_t const * p_ble_evt, void * p_context)
{
    uint32_t err_code;

    switch (p_ble_evt->header.evt_id)
    {
        case BLE_GAP_EVT_CONNECTED:
            NRF_LOG_INFO("Connected");
            ble_connection_status = 1;
			//board_LED_set(LED_INDICATE_CONNECTED);
            m_conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
            err_code = nrf_ble_qwr_conn_handle_assign(&m_qwr, m_conn_handle);
            // APP_ERROR_CHECK(err_code); // software flow should ensure is always correct?
            err_code = sd_ble_gap_tx_power_set(BLE_GAP_TX_POWER_ROLE_CONN, m_conn_handle, BLE_TX_POWER_CONN);
			// APP_ERROR_CHECK(err_code); // if parameter or handle is invalid, we don't really want the whole stick to freeze
            break;

        case BLE_GAP_EVT_DISCONNECTED:
            NRF_LOG_INFO("Disconnected");
            ble_connection_status = 0;
            // LED indication will be changed when advertising starts.
            m_conn_handle = BLE_CONN_HANDLE_INVALID;
            break;

        case BLE_GAP_EVT_PHY_UPDATE_REQUEST:
        {
            NRF_LOG_DEBUG("PHY update request.");
            ble_gap_phys_t const phys =
            {
                .rx_phys = BLE_GAP_PHY_AUTO,
                .tx_phys = BLE_GAP_PHY_AUTO,
            };
            err_code = sd_ble_gap_phy_update(p_ble_evt->evt.gap_evt.conn_handle, &phys);
			if ((err_code != NRF_SUCCESS) && (err_code != NRF_ERROR_BUSY))
			{
				// APP_ERROR_CHECK(err_code); // if parameter or handle is invalid, we don't really want the whole stick to freeze
			}
        } break;

        case BLE_GAP_EVT_SEC_PARAMS_REQUEST:
            // Pairing not supported
            err_code = sd_ble_gap_sec_params_reply(m_conn_handle, BLE_GAP_SEC_STATUS_PAIRING_NOT_SUPP, NULL, NULL);
			if ((err_code != NRF_SUCCESS) && (err_code != NRF_ERROR_BUSY))
			{
				// APP_ERROR_CHECK(err_code); // if parameter or handle is invalid, we don't really want the whole stick to freeze
			}
            break;

        case BLE_GATTS_EVT_SYS_ATTR_MISSING:
            // No system attributes have been stored.
            err_code = sd_ble_gatts_sys_attr_set(m_conn_handle, NULL, 0, 0);
			if ((err_code != NRF_SUCCESS))
			{
				// APP_ERROR_CHECK(err_code); // if parameter or handle is invalid, we don't really want the whole stick to freeze
			}
            break;

        case BLE_GATTC_EVT_TIMEOUT:
            // Disconnect on GATT Client timeout event.
            err_code = sd_ble_gap_disconnect(p_ble_evt->evt.gattc_evt.conn_handle,
                                             BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
			if ((err_code != NRF_SUCCESS) && (err_code != NRF_ERROR_INVALID_STATE))
			{
				// APP_ERROR_CHECK(err_code); // if parameter or handle is invalid, we don't really want the whole stick to freeze
			}
            break;

        case BLE_GATTS_EVT_TIMEOUT:
            // Disconnect on GATT Server timeout event.
            err_code = sd_ble_gap_disconnect(p_ble_evt->evt.gatts_evt.conn_handle,
                                             BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
			if ((err_code != NRF_SUCCESS) && (err_code != NRF_ERROR_INVALID_STATE))
			{
				// APP_ERROR_CHECK(err_code); // if parameter or handle is invalid, we don't really want the whole stick to freeze
			}
            break;

        default:
            // No implementation needed.
            break;
    }
}

void gatt_evt_handler(nrf_ble_gatt_t * p_gatt, nrf_ble_gatt_evt_t const * p_evt)
{
    if ((m_conn_handle == p_evt->conn_handle) && (p_evt->evt_id == NRF_BLE_GATT_EVT_ATT_MTU_UPDATED))
    {
        m_ble_nus_max_data_len = p_evt->params.att_mtu_effective - OPCODE_LENGTH - HANDLE_LENGTH;
        NRF_LOG_INFO("Data len is set to 0x%X(%d)", m_ble_nus_max_data_len, m_ble_nus_max_data_len);
    }
    NRF_LOG_DEBUG("ATT MTU exchange completed. central 0x%x peripheral 0x%x",
                  p_gatt->att_mtu_desired_central,
                  p_gatt->att_mtu_desired_periph);
}

static void nus_data_handler(ble_nus_evt_t * p_evt)
{
    if (p_evt->type == BLE_NUS_EVT_RX_DATA)
    {
        uint32_t err_code;

        NRF_LOG_DEBUG("Received data from BLE NUS.");
        NRF_LOG_HEXDUMP_DEBUG(p_evt->params.rx_data.p_data, p_evt->params.rx_data.length);
	
		for (uint32_t i = 0; i < p_evt->params.rx_data.length; i++)
        {
			msg_rx_next_byte(p_evt->params.rx_data.p_data[i]);
		}
    }

}

static void on_conn_params_evt(ble_conn_params_evt_t * p_evt)
{
    uint32_t err_code;

    if (p_evt->evt_type == BLE_CONN_PARAMS_EVT_FAILED)
    {
        err_code = sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_CONN_INTERVAL_UNACCEPTABLE);
        if ((err_code != NRF_SUCCESS) && (err_code != NRF_ERROR_INVALID_STATE))
		{
			// APP_ERROR_CHECK(err_code); // if parameter or handle is invalid, we don't really want the whole stick to freeze
		}
    }
}

static void on_adv_evt(ble_adv_evt_t ble_adv_evt)
{
    uint32_t err_code;

    switch (ble_adv_evt)
    {
        case BLE_ADV_EVT_FAST:
            //board_LED_set(LED_INDICATE_ADVERTISING);
            ble_connection_status = 0;
            break;
        case BLE_ADV_EVT_IDLE:
			#ifdef ENABLE_SHUTDOWN
			//system_shutdown();
			#endif
			(void) sd_ble_gap_adv_stop(m_advertising.adv_handle);
			ble_advertising_start(&m_advertising, BLE_ADV_MODE_FAST);
            break;
        default:
            break;
    }
}

static void nrf_qwr_error_handler(uint32_t nrf_error)
{
    APP_ERROR_HANDLER(nrf_error);
}

static void conn_params_error_handler(uint32_t nrf_error)
{
    APP_ERROR_HANDLER(nrf_error);
}

