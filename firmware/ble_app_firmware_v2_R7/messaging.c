/*===========================================
//
// messaging.c
// Written by Alex Gilmour
// Copyright (c) 2024, CarbonCircuits
// All rights reserved.
//
//=========================================*/

#include "messaging.h"
#include "flash.h"
#include "main.h"
#include "system.h"
#include "battery.h"
#include "adc.h"
#include "measure.h"
#include <stdio.h>

typedef enum msg_rx_state_t
{
	RX_STATE_STOP		= 0,
	RX_STATE_SYNC		= 1,
	RX_STATE_CQ			= 2,
	RX_STATE_TYPE		= 3,
	RX_STATE_PAYLOAD	= 4
} msg_rx_state_t;

// Buffers for outgoing messages
volatile uint8_t		msg_tx_buffer[MSG_TX_BUFFER_SIZE];
volatile uint32_t		msg_tx_buffer_end;

// Buffers for incoming messages
volatile msg_rx_state_t	msg_rx_state;
volatile uint8_t		msg_rx_cq;
volatile uint8_t		msg_rx_type;
volatile uint8_t		msg_rx_buffer[MSG_RX_BUFFER_SIZE];
volatile uint16_t		msg_rx_buffer_end;

static inline void msg_add_sync(void);
static inline void msg_add_byte(uint8_t byte);
static inline void msg_add_delim(void);
static inline void msg_add_endline(void);

static inline void msg_rx_enable(void);
static inline void msg_rx_resync(void);
static inline uint8_t msg_packet_received(void);

static inline void msg_tx_ack(uint8_t command_type);
static inline void msg_tx_error(uint8_t reason);

void msg_init(void) // Initialises UART peripheral and IO pins
{
	msg_tx_buffer_end = 0;
	
	msg_rx_state = RX_STATE_SYNC;
	msg_rx_cq = 0;
	msg_rx_type = 0;
	msg_rx_buffer_end = 0;
	
	ble_reid_init();

	msg_rx_enable();
}

void msg_deinit(void)
{
	msg_tx_buffer_end = 0;
	
	msg_rx_state = RX_STATE_SYNC;
	msg_rx_cq = 0;
	msg_rx_type = 0;
	msg_rx_buffer_end = 0;
	
	ble_reid_deinit();
}

static inline void msg_tx_ack(uint8_t command_type)
{
	msg_tx_buffer_end = 0;
	msg_add_sync();
	msg_add_byte(MSG_TYPE_ACK);
	msg_add_byte(command_type);
	msg_add_endline();
	
	ble_reid_tx((uint8_t*) msg_tx_buffer, msg_tx_buffer_end);
}

static inline void msg_tx_error(uint8_t reason)
{
	msg_tx_buffer_end = 0;
	msg_add_sync();
	msg_add_byte(MSG_TYPE_ERROR);
	msg_add_byte(reason);
	msg_add_endline();
	
	ble_reid_tx((uint8_t*) msg_tx_buffer, msg_tx_buffer_end);
}

void msg_process_packet(void)
{
	if (msg_packet_received())
	{
		last_ble_activity_ms = system_time_ms();
		if (msg_rx_cq == MSG_TYPE_QUERY)
		{
			switch (msg_rx_type)
			{
				case MSG_QUERY_SYSINFO:
				{
					const char* stream_capability = "UNKNOWN";

#ifdef STREAM_PROTOCOL_ASCII_V1
					stream_capability = "ASCII_V1_TS";
#endif
#ifdef STREAM_PROTOCOL_BINARY_V2
					stream_capability = "BINARY_V2";
#endif

					msg_tx_buffer_end = 0;
					msg_add_sync();
					msg_add_byte(MSG_TYPE_RESPONSE);
					msg_add_byte(MSG_QUERY_SYSINFO);
					msg_add_delim();
						msg_tx_buffer_end += snprintf(
							(uint8_t*)msg_tx_buffer+msg_tx_buffer_end,
							MSG_TX_BUFFER_SIZE-msg_tx_buffer_end,
							"UID=%08X%08X,VER=%u.%u.%u,STREAM=%s,LOOPMS=%u,MEASUS=%u",
							(unsigned int)(flash_sysdata.device_id >> 32),
							(unsigned int)(flash_sysdata.device_id & 0xFFFFFFFFu),
							(unsigned int)((flash_sysdata.device_version >> 16) & 0xFF),
							(unsigned int)((flash_sysdata.device_version >> 8) & 0xFF),
							(unsigned int)(flash_sysdata.device_version & 0xFF),
							stream_capability,
							(unsigned int)main_loop_period_ms,
							(unsigned int)measure_last_us
						);
					msg_add_endline();
					ble_reid_tx((uint8_t*) msg_tx_buffer, msg_tx_buffer_end);
					break;
				}
				
				case MSG_QUERY_BATTLEVEL:
				{
					int32_t battery_raw = 0;
					uint16_t battery_mv = 0;
					uint8_t battery_pct = 0;
					battery_state_t battery_state = battery_unknown;

					battery_query_pause = 1;
					system_wait_for_ms_no_bg(MAIN_LOOP_TIME_MS + 10);
					battery_update();
					battery_raw = adc_read_vbat_raw_fresh();
					battery_mv = battery_pack_voltage_mv();
					battery_pct = battery_pack_charge();
					battery_state = battery_current_state();

					msg_tx_buffer_end = 0;
					msg_add_sync();
					msg_add_byte(MSG_TYPE_RESPONSE);
					msg_add_byte(MSG_QUERY_BATTLEVEL);
					msg_add_delim();
					msg_tx_buffer_end += snprintf(
						(uint8_t*)(msg_tx_buffer+msg_tx_buffer_end),
						MSG_TX_BUFFER_SIZE-msg_tx_buffer_end,
						"%u %u %c RAW=%ld",
						(unsigned int)battery_pct,
						(unsigned int)battery_mv,
						battery_state,
						(long)battery_raw
					);
					msg_add_endline();
					ble_reid_tx((uint8_t*) msg_tx_buffer, msg_tx_buffer_end);
					break;
				}
				
				case MSG_QUERY_TIME:
				{
					msg_tx_buffer_end = 0;
					msg_add_sync();
					msg_add_byte(MSG_TYPE_RESPONSE);
					msg_add_byte(MSG_QUERY_TIME);
					msg_add_delim();
					msg_tx_buffer_end += snprintf((uint8_t*)(msg_tx_buffer+msg_tx_buffer_end),MSG_TX_BUFFER_SIZE-msg_tx_buffer_end,"%u",system_time_sec());
					msg_add_endline();
					ble_reid_tx((uint8_t*) msg_tx_buffer, msg_tx_buffer_end);
					break;
				}

                case MSG_QUERY_ALL_DATA_IND:
				{
					uint32_t low_index, high_index;
					flash_get_all_indices(&low_index, &high_index);

					msg_tx_buffer_end = 0;
					msg_add_sync();
					msg_add_byte(MSG_TYPE_RESPONSE);
					msg_add_byte(MSG_QUERY_ALL_DATA_IND);
					msg_add_delim();
					msg_tx_buffer_end += snprintf((uint8_t*)msg_tx_buffer+msg_tx_buffer_end,MSG_TX_BUFFER_SIZE-msg_tx_buffer_end,"%u,%u",low_index,high_index);
					msg_add_endline();
					ble_reid_tx((uint8_t*) msg_tx_buffer, msg_tx_buffer_end);
					break;
				}
				
				case MSG_QUERY_PENDING_DATA_IND:
				{
					uint32_t low_index, high_index;
					flash_get_pending_indices(&low_index, &high_index);

					msg_tx_buffer_end = 0;
					msg_add_sync();
					msg_add_byte(MSG_TYPE_RESPONSE);
					msg_add_byte(MSG_QUERY_PENDING_DATA_IND);
					msg_add_delim();
					msg_tx_buffer_end += snprintf((uint8_t*)msg_tx_buffer+msg_tx_buffer_end,MSG_TX_BUFFER_SIZE-msg_tx_buffer_end,"%u,%u",low_index,high_index);
					msg_add_endline();
					ble_reid_tx((uint8_t*) msg_tx_buffer, msg_tx_buffer_end);
					break;
				}
				
				case MSG_QUERY_RECORD:
				{
					uint32_t index=0;
                    msg_rx_buffer[msg_rx_buffer_end] = 0;
                    msg_rx_buffer[MSG_RX_BUFFER_SIZE-1] = 0;
					if (sscanf((uint8_t*)msg_rx_buffer,"%u",&index)<1) {
						msg_tx_error(MSG_ERROR_BAD_QUERY);
						break;
					}

					reid_ble_summary_packet_t* record = NULL;
					record = flash_get_record(index);
					if (record == NULL) {
						msg_tx_error(MSG_ERROR_NO_DATA);
						break;
					}

					msg_tx_buffer_end = 0;
					msg_add_sync();
					msg_add_byte(MSG_TYPE_RESPONSE);
					msg_add_byte(MSG_QUERY_RECORD);
					msg_add_delim();
					memcpy((reid_ble_summary_packet_t*)(msg_tx_buffer+msg_tx_buffer_end), record, sizeof(reid_ble_summary_packet_t));
                    msg_tx_buffer_end += sizeof(reid_ble_summary_packet_t);
					msg_add_endline();
					ble_reid_tx((uint8_t*) msg_tx_buffer, msg_tx_buffer_end);
					break;
				}

				case MSG_QUERY_RECORD_MULTIPLE:
				{
					uint32_t low_index=0;
					uint32_t high_index=0;
					uint32_t sent_packets=0;
                    msg_rx_buffer[msg_rx_buffer_end] = 0;
                    msg_rx_buffer[MSG_RX_BUFFER_SIZE-1] = 0;
					if (sscanf((uint8_t*)msg_rx_buffer,"%u,%u",&low_index,&high_index)<2) {
						msg_tx_error(MSG_ERROR_BAD_QUERY);
						break;
					}
										
					if (low_index > high_index) {
						uint32_t temp = low_index;
						low_index = high_index;
						high_index = temp;
					}

					if ((high_index - low_index) > MSG_MAX_RECORDS_PER_REQUEST) {
						msg_tx_error(MSG_ERROR_BAD_QUERY);
						break;
					}

					reid_ble_summary_packet_t* record = NULL;
					for (uint32_t i = low_index; i <= high_index; ++i) {
						record = flash_get_record(i);
						if (record != NULL) {
                        	msg_tx_buffer_end = 0;
							msg_add_sync();
							msg_add_byte(MSG_TYPE_RESPONSE);
							msg_add_byte(MSG_QUERY_RECORD_MULTIPLE);
							msg_add_delim();
							memcpy((reid_ble_summary_packet_t*)(msg_tx_buffer+msg_tx_buffer_end), record, sizeof(reid_ble_summary_packet_t));
							msg_tx_buffer_end += sizeof(reid_ble_summary_packet_t);
							msg_add_endline();
							ble_reid_tx((uint8_t*) msg_tx_buffer, msg_tx_buffer_end);
							++sent_packets;
						}
					}

					if (sent_packets == 0) msg_tx_error(MSG_ERROR_NO_DATA);
					break;
				}
				
				case MSG_QUERY_LAST_DATA:
				{
					msg_tx_buffer_end = 0;
					msg_add_sync();
					msg_add_byte(MSG_TYPE_RESPONSE);
					msg_add_byte(MSG_QUERY_LAST_DATA);
					msg_add_delim();
					memcpy((reid_ble_packet_t*)(msg_tx_buffer+msg_tx_buffer_end), (reid_ble_packet_t*)&ble_data, sizeof(reid_ble_packet_t));
                    msg_tx_buffer_end += sizeof(reid_ble_packet_t);
					msg_add_endline();
					ble_reid_tx((uint8_t*) msg_tx_buffer, msg_tx_buffer_end);
					break;
				}
				
				case MSG_QUERY_FRESH_DATA:
				{
					//static reid_ble_packet_t fresh_data;
					//measure_sensors(&fresh_data,1);
					msg_tx_buffer_end = 0;
					msg_add_sync();
					msg_add_byte(MSG_TYPE_RESPONSE);
					msg_add_byte(MSG_QUERY_FRESH_DATA);
					msg_add_delim();
					//memcpy((reid_ble_packet_t*)(msg_tx_buffer+msg_tx_buffer_end), &fresh_data, sizeof(reid_ble_packet_t));
					memcpy((reid_ble_packet_t*)(msg_tx_buffer+msg_tx_buffer_end), (reid_ble_packet_t*)&ble_data, sizeof(reid_ble_packet_t));
                    msg_tx_buffer_end += sizeof(reid_ble_packet_t);
					msg_add_endline();
					ble_reid_tx((uint8_t*) msg_tx_buffer, msg_tx_buffer_end);
					break;
				}

				default:
				{
					msg_tx_error(MSG_ERROR_BAD_QUERY);
					break;
				}
			}
		}
		else if (msg_rx_cq == MSG_TYPE_COMMAND)
		{
			switch (msg_rx_type)
			{
				case MSG_COMMAND_REBOOT:
				{
					msg_tx_ack(MSG_COMMAND_REBOOT);
					system_wait_for_ms_no_bg(250);
					system_reboot();
					break;
				}

				case MSG_COMMAND_ERASEALL:
				{
					msg_tx_ack(MSG_COMMAND_ERASEALL);
					system_wait_for_ms_no_bg(250);
					flash_reset_all();
					break;
				}

				case MSG_COMMAND_SETTIME:
				{
					uint32_t newtime = 0;
					if (sscanf((uint8_t*)msg_rx_buffer,"%u",&newtime)<1) {
						msg_tx_error(MSG_ERROR_BAD_COMMAND);
						break;
					}
                    system_set_time(newtime);
                    msg_tx_ack(MSG_COMMAND_SETTIME);
					break;
				}

				case MSG_COMMAND_KEEPAWAKE:
				{
					uint32_t enable = 0;
					if (sscanf((uint8_t*)msg_rx_buffer,"%u",&enable)<1) {
						msg_tx_error(MSG_ERROR_BAD_COMMAND);
						break;
					}
					bench_keepawake = (enable != 0);
					msg_tx_ack(MSG_COMMAND_KEEPAWAKE);
					break;
				}

				case MSG_COMMAND_SESSION_ACTIVE:
				{
					uint32_t enable = 0;
					if (sscanf((uint8_t*)msg_rx_buffer,"%u",&enable)<1) {
						msg_tx_error(MSG_ERROR_BAD_COMMAND);
						break;
					}
					session_active = (enable != 0);
					msg_tx_ack(MSG_COMMAND_SESSION_ACTIVE);
					break;
				}

				case MSG_COMMAND_RECORD_SYNCED:
				{
					uint32_t index=0;
                    msg_rx_buffer[msg_rx_buffer_end] = 0;
                    msg_rx_buffer[MSG_RX_BUFFER_SIZE-1] = 0;
					if (sscanf((uint8_t*)msg_rx_buffer,"%u",&index)<1) {
						msg_tx_error(MSG_ERROR_BAD_COMMAND);
						break;
					}
					msg_tx_ack(MSG_COMMAND_RECORD_SYNCED);
					system_wait_for_ms_no_bg(250);
					flash_mark_record_synced(index);
					break;
				}
                
				case MSG_COMMAND_RECORD_SYNCED_MULTIPLE:
				{
					uint32_t low_index=0;
					uint32_t high_index=0;
					uint32_t sent_packets=0;
                    msg_rx_buffer[msg_rx_buffer_end] = 0;
                    msg_rx_buffer[MSG_RX_BUFFER_SIZE-1] = 0;
					if (sscanf((uint8_t*)msg_rx_buffer,"%u,%u",&low_index,&high_index)<2) {
						msg_tx_error(MSG_ERROR_BAD_COMMAND);
						break;
					}
					
					if (low_index > high_index) {
						uint32_t temp = low_index;
						low_index = high_index;
						high_index = temp;
					}

					if (/*(low_index == high_index)||*/((high_index - low_index) > MSG_MAX_RECORDS_PER_REQUEST))
					{
						msg_tx_error(MSG_ERROR_BAD_COMMAND);
						break;
					}

					msg_tx_ack(MSG_COMMAND_RECORD_SYNCED_MULTIPLE);
					system_wait_for_ms_no_bg(250);
					flash_mark_records_synced(low_index, high_index);
					break;
				}

				case MSG_COMMAND_SET_RATE:
				{
					uint32_t hz = 0;
					msg_rx_buffer[msg_rx_buffer_end] = 0;
					msg_rx_buffer[MSG_RX_BUFFER_SIZE-1] = 0;
					if (sscanf((uint8_t*)msg_rx_buffer,"%u",&hz)<1) {
						msg_tx_error(MSG_ERROR_BAD_COMMAND);
						break;
					}
					if (hz < MIN_STREAM_RATE_HZ) hz = MIN_STREAM_RATE_HZ;
					if (hz > MAX_STREAM_RATE_HZ) hz = MAX_STREAM_RATE_HZ;
					main_loop_period_ms = (uint16_t)(1000u / hz);
					msg_tx_ack(MSG_COMMAND_SET_RATE);
					break;
				}

				default:
				{
					msg_tx_error(MSG_ERROR_BAD_COMMAND);
					break;
				}
			}
		}
		msg_rx_enable();
	}
}

static inline void msg_add_sync(void)
{
	msg_add_byte(MSG_SYNC);
}

static inline void msg_add_delim(void)
{
	msg_add_byte(' ');
}

static inline void msg_add_endline(void)
{
	msg_add_byte('\r');
	msg_add_byte('\n');
}

static inline void msg_add_byte(uint8_t byte)
{
	if (msg_tx_buffer_end < MSG_TX_BUFFER_SIZE) msg_tx_buffer[msg_tx_buffer_end++] = byte;
}


void msg_rx_next_byte(uint8_t rx_byte)
{
	/*#ifdef SEND_EVERY_MEAS_OVER_BLE
	return;
	#endif*/

	if (msg_rx_state == RX_STATE_STOP) // Ignore the byte if we're in the stop state - don't want to overwrite existing message
	{
		return;
	}
	
	// State machine to process the incoming stream
	// In each state, if we see a byte we're not expecting we reset to RX_STATE_SYNC
	switch (msg_rx_state)
	{
		case RX_STATE_SYNC:
			if (rx_byte == MSG_SYNC)
			{
				msg_rx_state = RX_STATE_CQ;
			}
			return;
		
		case RX_STATE_CQ:
			if (ASCII_ISEQUAL_NOCASE(rx_byte,MSG_TYPE_QUERY))
			{
				msg_rx_cq = MSG_TYPE_QUERY;
				msg_rx_state = RX_STATE_TYPE;
			}
			else if (ASCII_ISEQUAL_NOCASE(rx_byte,MSG_TYPE_COMMAND))
			{
				msg_rx_cq = MSG_TYPE_COMMAND;
				msg_rx_state = RX_STATE_TYPE;
			}
			else
			{
				msg_tx_error(MSG_ERROR_PROTOCOL);
				msg_rx_resync();
			}
			return;
			
		case RX_STATE_TYPE:
			if (msg_rx_cq == MSG_TYPE_QUERY)
			{
				if 		(ASCII_ISEQUAL_NOCASE(rx_byte,MSG_QUERY_SYSINFO))			{msg_rx_type = MSG_QUERY_SYSINFO;			msg_rx_state = RX_STATE_STOP;		msg_rx_buffer_end = 0;}
				else if (ASCII_ISEQUAL_NOCASE(rx_byte,MSG_QUERY_BATTLEVEL))			{msg_rx_type = MSG_QUERY_BATTLEVEL;			msg_rx_state = RX_STATE_STOP;		msg_rx_buffer_end = 0;}
				else if (ASCII_ISEQUAL_NOCASE(rx_byte,MSG_QUERY_TIME))				{msg_rx_type = MSG_QUERY_TIME;				msg_rx_state = RX_STATE_STOP;		msg_rx_buffer_end = 0;}
				else if (ASCII_ISEQUAL_NOCASE(rx_byte,MSG_QUERY_ALL_DATA_IND))		{msg_rx_type = MSG_QUERY_ALL_DATA_IND;		msg_rx_state = RX_STATE_STOP;		msg_rx_buffer_end = 0;}
				else if (ASCII_ISEQUAL_NOCASE(rx_byte,MSG_QUERY_PENDING_DATA_IND))	{msg_rx_type = MSG_QUERY_PENDING_DATA_IND;	msg_rx_state = RX_STATE_STOP;		msg_rx_buffer_end = 0;}
				else if (ASCII_ISEQUAL_NOCASE(rx_byte,MSG_QUERY_RECORD))			{msg_rx_type = MSG_QUERY_RECORD;			msg_rx_state = RX_STATE_PAYLOAD;	msg_rx_buffer_end = 0;}
				else if (ASCII_ISEQUAL_NOCASE(rx_byte,MSG_QUERY_RECORD_MULTIPLE))	{msg_rx_type = MSG_QUERY_RECORD_MULTIPLE;	msg_rx_state = RX_STATE_PAYLOAD;	msg_rx_buffer_end = 0;}
				else if (ASCII_ISEQUAL_NOCASE(rx_byte,MSG_QUERY_LAST_DATA))			{msg_rx_type = MSG_QUERY_LAST_DATA;			msg_rx_state = RX_STATE_STOP;		msg_rx_buffer_end = 0;}
				else if (ASCII_ISEQUAL_NOCASE(rx_byte,MSG_QUERY_FRESH_DATA))		{msg_rx_type = MSG_QUERY_FRESH_DATA;		msg_rx_state = RX_STATE_STOP;		msg_rx_buffer_end = 0;}
				else																{msg_tx_error(MSG_ERROR_BAD_QUERY);	msg_rx_resync();}
			}
			else if (msg_rx_cq == MSG_TYPE_COMMAND)
			{
				if 		(ASCII_ISEQUAL_NOCASE(rx_byte,MSG_COMMAND_REBOOT))			{msg_rx_type = MSG_COMMAND_REBOOT;			msg_rx_state = RX_STATE_STOP;		msg_rx_buffer_end = 0;}
				else if (ASCII_ISEQUAL_NOCASE(rx_byte,MSG_COMMAND_ERASEALL))		{msg_rx_type = MSG_COMMAND_ERASEALL;		msg_rx_state = RX_STATE_STOP;		msg_rx_buffer_end = 0;}
				else if (ASCII_ISEQUAL_NOCASE(rx_byte,MSG_COMMAND_SETTIME))			{msg_rx_type = MSG_COMMAND_SETTIME;			msg_rx_state = RX_STATE_PAYLOAD;	msg_rx_buffer_end = 0;}
				else if (ASCII_ISEQUAL_NOCASE(rx_byte,MSG_COMMAND_RECORD_SYNCED))			{msg_rx_type = MSG_COMMAND_RECORD_SYNCED;			msg_rx_state = RX_STATE_PAYLOAD; msg_rx_buffer_end = 0;}
				else if (ASCII_ISEQUAL_NOCASE(rx_byte,MSG_COMMAND_RECORD_SYNCED_MULTIPLE))	{msg_rx_type = MSG_COMMAND_RECORD_SYNCED_MULTIPLE;	msg_rx_state = RX_STATE_PAYLOAD; msg_rx_buffer_end = 0;}
				else if (ASCII_ISEQUAL_NOCASE(rx_byte,MSG_COMMAND_SET_RATE))			{msg_rx_type = MSG_COMMAND_SET_RATE;		msg_rx_state = RX_STATE_PAYLOAD;	msg_rx_buffer_end = 0;}
				else																{msg_tx_error(MSG_ERROR_BAD_COMMAND);msg_rx_resync();}
			}
			else
			{
				msg_tx_error(MSG_ERROR_PROTOCOL);
				msg_rx_resync();
			}
			return;
			
		case RX_STATE_PAYLOAD:
			if ((rx_byte == MSG_END1)||(rx_byte == MSG_END2))
			{
				msg_rx_state = RX_STATE_STOP;
			}
			else if (msg_rx_buffer_end < MSG_RX_BUFFER_SIZE)
			{
				msg_rx_buffer[msg_rx_buffer_end++] = rx_byte;
			}
			else // otherwise, we've received all of the data that we possibly can
			{
				msg_tx_error(MSG_ERROR_PROTOCOL);
				msg_rx_state = RX_STATE_STOP;
			}
			return;
			
		default: // this following code is a "just in case" - it should never happen
			msg_rx_state = RX_STATE_STOP;
		case RX_STATE_STOP:
			return;
	}
}

uint8_t msg_rx_accept_data(void)
{
	return (msg_rx_state == RX_STATE_STOP) ? 0 : 1;
}

static inline void msg_rx_enable(void)
{
	safe_disable_interrupt();
	if (msg_rx_state == RX_STATE_STOP)
	{
		msg_rx_state = RX_STATE_SYNC;
		msg_rx_cq = 0;
		msg_rx_type = 0;
		msg_rx_buffer_end = 0;
	}
	safe_enable_interrupt();
}

static inline void msg_rx_resync(void)
{
	safe_disable_interrupt();
	msg_rx_state = RX_STATE_SYNC;
	msg_rx_cq = 0;
	msg_rx_type = 0;
	msg_rx_buffer_end = 0;
	safe_enable_interrupt();
}

static inline uint8_t msg_packet_received(void)
{
	return (msg_rx_state == RX_STATE_STOP) ? 1 : 0;
}




