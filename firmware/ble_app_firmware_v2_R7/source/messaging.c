/*===========================================
//
// messaging.c
// Written by Alex Gilmour
// Copyright (c) 2024, CarbonCircuits
// All rights reserved.
//
//=========================================*/

#include "messaging.h"
#include <stdio.h>
#include <string.h>
#include "configure_firmware.h"
#include "battery.h"
#include "measure.h"
#include "power.h"
#include "drivers/ble_reid.h"
#include "drivers/flash.h"
#include "drivers/lsm6dsm.h"
#include "drivers/system.h"

// One row per packet the device accepts: its code, whether a payload follows,
// and the function that answers it.
typedef struct msg_handler_t
{
	uint8_t code;
	uint8_t has_payload;
	void (*handle)(uint8_t code);
} msg_handler_t;

typedef enum msg_rx_state_t
{
	RX_STATE_STOP		= 0,	// a complete packet is waiting for msg_process_packet()
	RX_STATE_SYNC		= 1,
	RX_STATE_CQ			= 2,
	RX_STATE_TYPE		= 3,
	RX_STATE_PAYLOAD	= 4
} msg_rx_state_t;

// Outgoing message
static volatile uint8_t		msg_tx_buffer[MSG_TX_BUFFER_SIZE];
static volatile uint32_t	msg_tx_buffer_end;

// Incoming message. Zero-initialised means "RX_STATE_STOP with no handler": the
// first idle step after boot therefore processes an empty packet, which does
// nothing except count as BLE activity (see power_note_command).
static volatile msg_rx_state_t	msg_rx_state;
static volatile uint8_t		msg_rx_cq;
static const msg_handler_t* volatile msg_rx_handler;
static volatile uint8_t		msg_rx_buffer[MSG_RX_BUFFER_SIZE];
static volatile uint16_t	msg_rx_buffer_end;

// ---- building and sending a packet -----------------------------------------

static inline void msg_add_byte(uint8_t byte)
{
	if (msg_tx_buffer_end < MSG_TX_BUFFER_SIZE) msg_tx_buffer[msg_tx_buffer_end++] = byte;
}

// Appends printf-formatted text.
#define msg_add_text(...)	(msg_tx_buffer_end += snprintf((char*)msg_tx_buffer+msg_tx_buffer_end, MSG_TX_BUFFER_SIZE-msg_tx_buffer_end, __VA_ARGS__))

static void msg_add_data(const void* data, uint16_t length)
{
	memcpy((void*)(msg_tx_buffer+msg_tx_buffer_end), data, length);
	msg_tx_buffer_end += length;
}

static void msg_begin(uint8_t type, uint8_t code)
{
	msg_tx_buffer_end = 0;
	msg_add_byte(MSG_SYNC);
	msg_add_byte(type);
	msg_add_byte(code);
}

static void msg_begin_response(uint8_t query)
{
	msg_begin(MSG_TYPE_RESPONSE, query);
	msg_add_byte(MSG_DELIM);
}

static void msg_send(void)
{
	msg_add_byte(MSG_END1);
	msg_add_byte(MSG_END2);
	ble_reid_tx((uint8_t*) msg_tx_buffer, msg_tx_buffer_end);
}

static void msg_tx_ack(uint8_t command)
{
	msg_begin(MSG_TYPE_ACK, command);
	msg_send();
}

static void msg_tx_error(uint8_t reason)
{
	msg_begin(MSG_TYPE_ERROR, reason);
	msg_send();
}

static void msg_tx_record(uint8_t query, const reid_ble_summary_packet_t* record)
{
	msg_begin_response(query);
	msg_add_data(record, sizeof(reid_ble_summary_packet_t));
	msg_send();
}

// ---- payload arguments -----------------------------------------------------

// Reads up to two unsigned decimal arguments from the payload ("97" or "5,12").
// Pass second = NULL to read one. Returns how many were found.
static int msg_payload_numbers(uint32_t* first, uint32_t* second)
{
	uint16_t end = msg_rx_buffer_end;
	if (end >= MSG_RX_BUFFER_SIZE) end = MSG_RX_BUFFER_SIZE-1;
	msg_rx_buffer[end] = 0;

	if (second == NULL) return sscanf((char*)msg_rx_buffer,"%u",first);
	return sscanf((char*)msg_rx_buffer,"%u,%u",first,second);
}

// Reads an "a,b" record index range into low <= high. Fails if it is malformed
// or spans more than MSG_MAX_RECORDS_PER_REQUEST.
static uint8_t msg_payload_range(uint32_t* low_index, uint32_t* high_index)
{
	if (msg_payload_numbers(low_index, high_index) < 2) return 0;

	if (*low_index > *high_index) {
		uint32_t temp = *low_index;
		*low_index = *high_index;
		*high_index = temp;
	}

	return ((*high_index - *low_index) <= MSG_MAX_RECORDS_PER_REQUEST);
}

// ---- queries ---------------------------------------------------------------

static void query_sysinfo(uint8_t code)
{
	uint16_t period_ms = power_frame_period_ms();

	msg_begin_response(code);
	// SEN-104: SESSION= exposes the ;CX latch so the app can verify its
	// reconcile-on-connect actually landed; RATE= is the derived Hz for
	// symmetry with the app's ;CF writes.
	msg_add_text(
		"UID=%08X%08X,VER=%u.%u.%u,STREAM=%s,LOOPMS=%u,MEASUS=%u,CAPUS=%u,SESSION=%u,RATE=%u",
		(unsigned int)(flash_sysdata.device_id >> 32),
		(unsigned int)(flash_sysdata.device_id & 0xFFFFFFFFu),
		(unsigned int)((flash_sysdata.device_version >> 16) & 0xFF),
		(unsigned int)((flash_sysdata.device_version >> 8) & 0xFF),
		(unsigned int)(flash_sysdata.device_version & 0xFF),
		"BINARY_V2",
		(unsigned int)period_ms,
		(unsigned int)measure_last_us,
		(unsigned int)measure_cap_us,
		(unsigned int)(power_session_active() ? 1 : 0),
		(unsigned int)(period_ms ? (1000u / period_ms) : 0)
	);
	msg_send();
}

static void query_battery(uint8_t code)
{
	// Take a sample now instead of reporting an average up to 5 s old. The main
	// loop is told to skip its next frame, and SEN-100: the wait is one
	// *runtime* loop period (;CF-settable), so it covers a loop slot.
	power_skip_next_frame();
	system_wait_for_ms_no_bg(power_frame_period_ms() + 10);
	battery_update();
	int32_t battery_raw = battery_read_raw_fresh();
	uint16_t battery_mv = battery_pack_voltage_mv();
	uint8_t battery_pct = battery_pack_charge();
	battery_state_t battery_state = battery_current_state();

	msg_begin_response(code);
	msg_add_text(
		"%u %u %c RAW=%ld",
		(unsigned int)battery_pct,
		(unsigned int)battery_mv,
		battery_state,
		(long)battery_raw
	);
	msg_send();
}

static void query_time(uint8_t code)
{
	msg_begin_response(code);
	msg_add_text("%u",system_time_sec());
	msg_send();
}

// QA: every stored record. QP: only those not yet marked synced.
static void query_record_indices(uint8_t code)
{
	uint32_t low_index, high_index;
	if (code == MSG_QUERY_ALL_DATA_IND) flash_get_all_indices(&low_index, &high_index);
	else flash_get_pending_indices(&low_index, &high_index);

	msg_begin_response(code);
	msg_add_text("%u,%u",low_index,high_index);
	msg_send();
}

static void query_record(uint8_t code)
{
	uint32_t index=0;
	if (msg_payload_numbers(&index, NULL)<1) {
		msg_tx_error(MSG_ERROR_BAD_QUERY);
		return;
	}

	reid_ble_summary_packet_t* record = flash_get_record(index);
	if (record == NULL) {
		msg_tx_error(MSG_ERROR_NO_DATA);
		return;
	}

	msg_tx_record(code, record);
}

static void query_record_multiple(uint8_t code)
{
	uint32_t low_index=0;
	uint32_t high_index=0;
	uint32_t sent_packets=0;
	if (!msg_payload_range(&low_index, &high_index)) {
		msg_tx_error(MSG_ERROR_BAD_QUERY);
		return;
	}

	for (uint32_t i = low_index; i <= high_index; ++i) {
		reid_ble_summary_packet_t* record = flash_get_record(i);
		if (record != NULL) {
			msg_tx_record(code, record);
			++sent_packets;
		}
	}

	if (sent_packets == 0) msg_tx_error(MSG_ERROR_NO_DATA);
}

// QL and QF: there is no on-demand measurement, so both return the last frame.
static void query_frame(uint8_t code)
{
	msg_begin_response(code);
	msg_add_data(measure_latest(), sizeof(reid_ble_packet_t));
	msg_send();
}

// IMU bring-up (SEN-54): identity check + fresh raw sample. WAI_OK=1 confirms
// WHO_AM_I matched; rotating the board should move AX/AY/AZ (gravity) and
// GX/GY/GZ (rotation) sanely.
static void query_imu(uint8_t code)
{
	int16_t whoami = lsm6dsm_whoami();
	lsm6dsm_update();

	msg_begin_response(code);
	msg_add_text(
		"WHOAMI=0x%02X,WAI_OK=%u,T=%d,AX=%d,AY=%d,AZ=%d,GX=%d,GY=%d,GZ=%d",
		(unsigned int)(whoami & 0xFF),
		(unsigned int)(whoami == LSM6DSM_WHO_AM_I_VALUE),
		(int)lsm6dsm_read_temp(),
		(int)lsm6dsm_read_ax(),
		(int)lsm6dsm_read_ay(),
		(int)lsm6dsm_read_az(),
		(int)lsm6dsm_read_gx(),
		(int)lsm6dsm_read_gy(),
		(int)lsm6dsm_read_gz()
	);
	msg_send();
}

static const msg_handler_t msg_queries[] =
{
	{ MSG_QUERY_SYSINFO,			0, query_sysinfo },
	{ MSG_QUERY_BATTLEVEL,			0, query_battery },
	{ MSG_QUERY_TIME,				0, query_time },
	{ MSG_QUERY_ALL_DATA_IND,		0, query_record_indices },
	{ MSG_QUERY_PENDING_DATA_IND,	0, query_record_indices },
	{ MSG_QUERY_RECORD,				1, query_record },
	{ MSG_QUERY_RECORD_MULTIPLE,	1, query_record_multiple },
	{ MSG_QUERY_LAST_DATA,			0, query_frame },
	{ MSG_QUERY_FRESH_DATA,			0, query_frame },
	{ MSG_QUERY_IMU,				0, query_imu },
};

// ---- commands --------------------------------------------------------------

static void command_reboot(uint8_t code)
{
	msg_tx_ack(code);
	system_wait_for_ms_no_bg(250);
	system_reboot();
}

static void command_erase_all(uint8_t code)
{
	msg_tx_ack(code);
	system_wait_for_ms_no_bg(250);
	flash_reset_all();
}

static void command_set_time(uint8_t code)
{
	uint32_t newtime = 0;
	if (msg_payload_numbers(&newtime, NULL)<1) {
		msg_tx_error(MSG_ERROR_BAD_COMMAND);
		return;
	}
	system_set_time(newtime);
	msg_tx_ack(code);
}

static void command_record_synced(uint8_t code)
{
	uint32_t index=0;
	if (msg_payload_numbers(&index, NULL)<1) {
		msg_tx_error(MSG_ERROR_BAD_COMMAND);
		return;
	}
	msg_tx_ack(code);
	system_wait_for_ms_no_bg(250);
	flash_mark_record_synced(index);
}

static void command_records_synced(uint8_t code)
{
	uint32_t low_index=0;
	uint32_t high_index=0;
	if (!msg_payload_range(&low_index, &high_index)) {
		msg_tx_error(MSG_ERROR_BAD_COMMAND);
		return;
	}

	msg_tx_ack(code);
	system_wait_for_ms_no_bg(250);
	flash_mark_records_synced(low_index, high_index);
}

static void command_set_rate(uint8_t code)
{
	uint32_t hz = 0;
	if (msg_payload_numbers(&hz, NULL)<1) {
		msg_tx_error(MSG_ERROR_BAD_COMMAND);
		return;
	}
	power_set_frame_rate_hz(hz);
	msg_tx_ack(code);
}

#ifdef ENABLE_SESSION_COMMANDS
// CK: bench keep-awake. CX: a recording session is in progress.
static void command_set_latch(uint8_t code)
{
	uint32_t enable = 0;
	if (msg_payload_numbers(&enable, NULL)<1) {
		msg_tx_error(MSG_ERROR_BAD_COMMAND);
		return;
	}
	if (code == MSG_COMMAND_KEEPAWAKE) power_set_bench_keepawake(enable != 0);
	else power_set_session_active(enable != 0);
	msg_tx_ack(code);
}
#endif

static const msg_handler_t msg_commands[] =
{
	{ MSG_COMMAND_REBOOT,					0, command_reboot },
	{ MSG_COMMAND_ERASEALL,					0, command_erase_all },
	{ MSG_COMMAND_SETTIME,					1, command_set_time },
	{ MSG_COMMAND_RECORD_SYNCED,			1, command_record_synced },
	{ MSG_COMMAND_RECORD_SYNCED_MULTIPLE,	1, command_records_synced },
	{ MSG_COMMAND_SET_RATE,					1, command_set_rate },
#ifdef ENABLE_SESSION_COMMANDS
	{ MSG_COMMAND_KEEPAWAKE,				1, command_set_latch },
	{ MSG_COMMAND_SESSION_ACTIVE,			1, command_set_latch },
#endif
};

// ---- receiver --------------------------------------------------------------

static inline void msg_rx_reset(void)
{
	msg_rx_state = RX_STATE_SYNC;
	msg_rx_cq = 0;
	msg_rx_handler = NULL;
	msg_rx_buffer_end = 0;
}

// Abandons a partly received packet.
static inline void msg_rx_resync(void)
{
	safe_disable_interrupt();
	msg_rx_reset();
	safe_enable_interrupt();
}

void msg_init(void)
{
	msg_tx_buffer_end = 0;
	msg_rx_reset();
}

void msg_process_packet(void)
{
	// Re-entrancy guard: a handler that waits on a peripheral (e.g. the IMU
	// query's I2C transfers) runs the idle hook, which calls back into here
	// while RX state is still RX_STATE_STOP -- without this the same packet would
	// be processed and answered recursively. Main-context only, so a plain flag
	// is sufficient.
	static volatile uint8_t msg_processing = 0;
	if (msg_processing) return;

	if (msg_rx_state != RX_STATE_STOP) return; // nothing received yet

	msg_processing = 1;
	power_note_command();
	const msg_handler_t* handler = msg_rx_handler;
	if (handler != NULL) handler->handle(handler->code);

	// re-arm the receiver
	safe_disable_interrupt();
	if (msg_rx_state == RX_STATE_STOP) msg_rx_reset();
	safe_enable_interrupt();
	msg_processing = 0;
}

void msg_rx_next_byte(uint8_t rx_byte)
{
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
		{
			const uint8_t is_query = (msg_rx_cq == MSG_TYPE_QUERY);
			const msg_handler_t* handlers = is_query ? msg_queries : msg_commands;
			const uint8_t num_handlers = is_query ? (sizeof(msg_queries)/sizeof(msg_queries[0])) : (sizeof(msg_commands)/sizeof(msg_commands[0]));

			for (uint8_t i = 0; i < num_handlers; ++i)
			{
				if (ASCII_ISEQUAL_NOCASE(rx_byte,handlers[i].code))
				{
					msg_rx_handler = &handlers[i];
					msg_rx_buffer_end = 0;
					msg_rx_state = handlers[i].has_payload ? RX_STATE_PAYLOAD : RX_STATE_STOP;
					return;
				}
			}
			msg_tx_error(is_query ? MSG_ERROR_BAD_QUERY : MSG_ERROR_BAD_COMMAND);
			msg_rx_resync();
			return;
		}

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

		case RX_STATE_STOP: // Ignore the byte - don't want to overwrite the packet waiting to be processed
			return;

		default: // this following code is a "just in case" - it should never happen
			msg_rx_state = RX_STATE_STOP;
			return;
	}
}
