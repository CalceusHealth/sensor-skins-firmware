/*===========================================
//
// messaging_defines.h
// Written by Alex Gilmour
// Copyright (c) 2024, CarbonCircuits
// All rights reserved.
//
//=========================================*/

#ifndef MESSAGING_DEFINES_H_
#define MESSAGING_DEFINES_H_


// numbers are the digit with 0x30 added
#define ASCII_NUM_OFFSET		0x30
#define ASCII_HEX_A_OFFSET		0x41

// lowercase are the same with 0x20 added
#define ASCII_LOWERCASE_OFFSET	0x20
#define ASCII_ISEQUAL_NOCASE(a,b)	((a == b)||(a == b+ASCII_LOWERCASE_OFFSET))
#define MSG_IS_NUMBER(a)		((a >= (uint8_t)'0') && (a <= (uint8_t)'9'))
#define MSG_IS_HEX(a)			(((a >= (uint8_t)'0') && (a <= (uint8_t)'9'))||((a >= (uint8_t)'A') && (a <= (uint8_t)'F'))||((a >= (uint8_t)'a') && (a <= (uint8_t)'f')))
#define MSG_IS_CHAR(a)			(((a >= (uint8_t)'A') && (a <= (uint8_t)'Z'))||((a >= (uint8_t)'a') && (a <= (uint8_t)'z')))

#define MSG_SYNC							';' // every packet starts with this

#define MSG_TYPE_QUERY						'Q' // query a data type from the slave
#define MSG_TYPE_RESPONSE					'R' // returned data from a query
#define MSG_TYPE_COMMAND					'C' // ask the slave to do something
#define MSG_TYPE_ACK						'A' // an acknowledge of the command
#define MSG_TYPE_ERROR						'E' // there was an issue with the packet

#define MSG_QUERY_SYSINFO					'I' // returns system information in ascii - usable for debug purposes
#define MSG_QUERY_BATTLEVEL					'B' // returns "percent mv state" for the battery
#define MSG_QUERY_TIME						'T' // returns the seconds timer of the device
#define MSG_QUERY_ALL_DATA_IND				'A' // returns the high and low index
#define MSG_QUERY_PENDING_DATA_IND			'P' // returns the high and low index for unsynced data
#define MSG_QUERY_RECORD					'R' // returns a record with the provided index; if the index does not exist, it will nack
#define MSG_QUERY_RECORD_MULTIPLE			'M' // returns all records within the given indices (if none exist, it will nack; if less than expected number of records exist, it will do nothing except send the records that do exist)
#define MSG_QUERY_LAST_DATA					'L' // returns the most recently measured data
#define MSG_QUERY_FRESH_DATA				'F' // performs a new measurement and returns this data

#define MSG_COMMAND_REBOOT					'R' // reboots the device
#define MSG_COMMAND_ERASEALL				'E' // erases all data
#define MSG_COMMAND_KEEPAWAKE				'K' // sets bench keep-awake mode with payload 0 or 1
#define MSG_COMMAND_SETTIME					'T' // sets time in seconds as a uint32_t e.g. as an epoch
#define MSG_COMMAND_SESSION_ACTIVE			'X' // sets explicit session active mode with payload 0 or 1
#define MSG_COMMAND_SET_RATE				'F' // sets the stream sample rate in Hz at runtime, e.g. ";CF 20" (NB 'R' is reboot)
#define MSG_COMMAND_RECORD_SYNCED			'S' // marks the record with the indicated index (if it exists) with the "synced" byte.
#define MSG_COMMAND_RECORD_SYNCED_MULTIPLE	'M' // marks all records within the given indices

#define MSG_ERROR_BAD_COMMAND				'C'
#define MSG_ERROR_BAD_QUERY					'Q'
#define MSG_ERROR_PROTOCOL					'M'
#define MSG_ERROR_NO_DATA					'N'
#define MSG_ERROR_UNKNOWN					'U'

#define MSG_DELIM							' '
#define MSG_END1							'\r' // either is okay to signal end of a packet, but both will be sent by the device
#define MSG_END2							'\n'

#endif // MESSAGING_DEFINES_H_

