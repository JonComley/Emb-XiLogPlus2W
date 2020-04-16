/******************************************************************************
** File:	Log.c	
**
** Notes:
** Two types of log: string events, entered only from mainloop level, and
** signed int32 values (= binary data).
** Binary values & events are enqueued when read, then entered into log buffer.
**
** Log files have path according to date & channel id, e.g.:
** \LOGDATA\D1A\2009\05\D1A_2705.TXT
** |-------|---|----|--|-----------|
**     A     B    C   D     E
**
** A = fixed path, B = channel id, C = year, D = month
** For SMS data, field A is \SMSDATA
** E = filename: channel id + '_' + day + month + ".TXT"
*/

/* CHANGES
** v2.36 281010 PB 	Removed input parameter of MDM_log_use as it is always called at midnight
**					Removed log_time_sec variable
**
** v2.38 031110 PB  Use ALM_com_mode_alarm_enable flag to control commission alarm generation
**
** v2.45 130111 PB use CFS_*** strings for fixed paths and names
**
** v2.51 090211 PB Log battery alarms to USE.TXT
**
** V2.65 070411 PB  conditional on HDW_PRIMELOG_PLUS, log queue sizes are 500
**
** V2.70 280411 PB  adjust length of PrimeLog log queues down a bit to leave 10% RAM free - queue size 475
**
** V2.77 160611 PB  new local functions log_create_enqueued_block_header() and log_create_block_header_parameters
**                  for extracting and creating uint32 value for new data type LOG_BLOCK_HEADER_PARAMS
**					this is enqueued directly after LOG_BLOCK_HEADER_TIMESTAMP so the block header parameters are
**                  valid for the time of enqueueing the header and the start of the block, 
**                  and not taken from the current settings when the header is actually written to the log or sms file
**
** V2.90 010811 PB  correction to V2.77 changes for sms files
**
** V3.03 171111	PB	Waste Water - add logging for derived data and derived sms data
**
** V3.04 221211 PB  Add logging for serial data and derived serial data
**
** V3.08 230212 PB  add channel S3 for doppler depth
**       280212 PB  ECO product - add fn LOG_set_pending_battery_test() for PWR_task to call
**								  remove redundant code from next day task
**								  pick up correct internal threshold default in log_battery_task_done() if an ECO product
**					Set internal_bat_logging_threshold to 4.5V for XilogPlus
**
** V3.09 270312 PB  Remove log_flags 7 and 8, log_battery_test_done(), LOG_set_pending_battery_test() and their use
**					this is all done in pwr.c as per V2.99 5th
** V3.13 120612 PB  Correction to LOG_create_block_header() for digital event value data
**
** V3.15 190612 PB  updates to align with Xilog+
**					come out of commissioning mode if in it at midnight
**
** V3.17 091012 PB  bring up to date with Xilog+ V3.06 - use return value of CFS_open in log_new_day_task(), LOG_task()
**
** V3.26 180613 PB  Remove CFS_FAILED_TO_OPEN state 
**
** V3.29 171013 PB  bring up to date with Xilog+ V3.05
**					use new log state LOG_BATT_DEAD instead of LOG_VOLTAGE_TOO_LOW
**					add send battery alarm if flag set to log_new_day_task()
**
** V3.31 141113 PB  add double check of file system open in log_write_to_file before calling FSfopen()
**
** V3.32 201113 PB  add to log_write_to_file() and LOG_enqueue_value() for control output logging
**
** V3.33 251113 PB  new code in LOG_enqueue_value() and log_write_to_file() for control outputs
**					new log_control_active_mask and log_control_write_mask
**					
** V4.00 220114 PB  if HDW_GPS disable all analogue calls and functions
**					add gps data to all ftp headers
**					trigger GPS automatic task at midnight
**
** V4.00 010514 PB  remove gps trigger at midnight
**
** V4.11 270814 PB  add GPS.TXT to headers if file exists if not a GPS product
*/

#include "float.h"
#include "custom.h"
#include "compiler.h"
#include "MDD File System/SD-SPI.h"
#include "MDD File System/FSIO.h"

#include "cfs.h"
#include "Str.h"
#include "Rtc.h"
#include "Dig.h"
#include "Ana.h"
#include "Slp.h"
#include "Pdu.h"
#include "ftp.h"
#include "Tim.h"
#include "msg.h"
#include "Com.h"
#include "Pwr.h"
#include "Alm.h"
#include "mdm.h"
#include "cmd.h"
#include "usb.h"
#include "cal.h"
#include "Ser.h"
#include "Dop.h"
#include "Cop.h"
#include "usb.h"
#include "gps.h"
#include "modbus.h"

#include "MDD File System/FSIO.h"

#define extern
#include "Log.h"
#undef extern

// Two queues: logging into active queue, writing the other to file.
// Could be increased to 475 for PrimeLog+, but would take a long time to dump queue contents to file,
// potentially compromising logging rate accuracy.
#define LOG_QUEUE_SIZE		128

// Values enqueued here, so file system can be updated before they are logged
// Not really a queue - just a holding buffer. 
typedef struct
{
	uint8 channel_number;
	uint8 data_type;
	int32 value;
} log_queue_type;

BITFIELD log_flags;

#define log_a_active				log_flags.b0	// ping-pong flag
#define log_queue_overflow			log_flags.b1
#define log_write_sms				log_flags.b2	// writing sms data
#define log_write_derived			log_flags.b3	// writing derived data
#define log_new_day					log_flags.b4	// get midnight measurements & write to file
#define log_close_old_day			log_flags.b5
#define log_pending_flush			log_flags.b6

int log_20ms_timer;

uint8 log_yr_bcd;
uint8 log_mth_bcd;
uint8 log_day_bcd;

int log_queue_tail;		// head always 0

// length of queue when flush to file begins:
int log_write_length;

// Masks for channels to be written to file system.
// channels 0..11 are normal logged channels, 16..27 are SMS data, bit 5 is set for derived data
// need to separate out into 4 different log paths
uint16 log_active_mask;
uint16 log_sms_active_mask;
uint16 log_derived_active_mask;
uint16 log_derived_sms_active_mask;
uint16 log_write_mask;
uint16 log_sms_write_mask;
uint16 log_derived_write_mask;
uint16 log_derived_sms_write_mask;
// masks for control output logging
uint16 log_control_active_mask;
uint16 log_control_write_mask;

FAR log_queue_type log_queue_a[LOG_QUEUE_SIZE + 1];		// overspill of 1 entry
FAR log_queue_type log_queue_b[LOG_QUEUE_SIZE + 1];		// overspill of 1 entry

FAR uint8 log_char_count[LOG_NUM_FUNCTIONS + LOG_SMS_MASK + LOG_DERIVED_MASK];

const char LOG_channel_id[LOG_NUM_FUNCTIONS + 11 + 3][4] =
{
	"ACT", 
#ifdef HDW_RS485
	"S1", "S2", "S3", "S4", "S5", "S6", "S7", "S8",
#else
	"D1A", "D1B", "D2A", "D2B", 
#endif
	"A1", "A2", "A3",
#ifdef HDW_RS485
	"DS1", "DS2", "DS3", "DS4",
#else
	"R1A", "R1B", "R2A", "R2B", 
#endif
	"DA1", "DA2", "DA3", "DA4", "DA5", "DA6", "DA7"
};

const char log_c_file[LOG_NUM_C_FILES + 1][6] =
{
	"act", "alm", "ana", "cal", "cfs", "cmd", "com", "cop", "dig", "dop", "dsk", "frm", "ftp", "gps", "log", 
	"main", "mdm", "msg", "pdu", "pwr", "rtc", "scf", "ser", "slp", "sns", "str", "tim", "tsync", "usb", "mod", "!!!"
};

// Conversion table for time enumeration byte to interval in seconds:
const uint32 LOG_interval_sec[25] =
{
	0,				// 0 -> off
	1L,				// 1 -> 1 sec
	2L,				// 2 -> 2 sec
	3L,				// 3 -> 3 sec
	5L,				// 4 -> 5 sec
	10L,			// 5 -> 10 sec
	15L,			// 6 -> 15 sec
	20L,			// 7 -> 20 sec
	30L,			// 8 -> 30 sec
	60L,			// 9 -> 1 minute
	2 * 60L,		// 10 -> 2 minutes
	3 * 60L,		// 11 -> 3 minutes
	5 * 60L,		// 12 -> 5 minutes
	10 * 60L,		// 13 -> 10 minutes
	15 * 60L,		// 14 -> 15 minutes
	20 * 60L,		// 15 -> 20 minutes
	30 * 60L,		// 16 -> 30 minutes
	60 * 60L,		// 17 -> 1 hour
	2 * 60 * 60L,	// 18 -> 2 hours
	3 * 60 * 60L,	// 19 -> 3 hours
	4 * 60 * 60L,	// 20 -> 4 hours
	6 * 60 * 60L,	// 21 -> 6 hours
	8 * 60 * 60L,	// 22 -> 8 hours
	12 * 60 * 60L,	// 23 -> 12 hours
	24 * 60 * 60L,	// 24 -> 24 hours
};

FAR char log_use_string[200];
FAR char log_gps_string[40];

/******************************************************************************
** Function:	Make an entry in the usage log
**
** Notes:	
*/
void LOG_entry(char * use_string)
{
	// create timestamp and use string
	sprintf(log_use_string, "%02x%02x%02x %02x:%02x:%02x %s\r\n",
				RTC_now.day_bcd, RTC_now.mth_bcd, RTC_now.yr_bcd,
				RTC_now.hr_bcd, RTC_now.min_bcd, RTC_now.sec_bcd,
				use_string);

	// write it to use log
	CFS_write_file((char *)CFS_activity_path, "USE.TXT", "a", log_use_string, strlen(log_use_string));
}

/******************************************************************************
** Function:	Get next log time
**
** Notes:		May return a value > 86400 if wrap not required.
**				If time enum disabled, returns FFFFFFFFH to prevent further wakeups
*/
void LOG_set_next_time(uint32 *p, uint8 time_enum, bool wrap_at_midnight)
{
	uint32 t;

	if ((time_enum == 0) || (time_enum > sizeof(LOG_interval_sec) / sizeof(LOG_interval_sec[0])))
	{
		*p = SLP_NO_WAKEUP;
		return;
	}
	// else:

	t = *p / LOG_interval_sec[time_enum];
	*p = LOG_interval_sec[time_enum] * (t + 1);

	if (wrap_at_midnight && (*p >= RTC_SEC_PER_DAY))
		*p -= RTC_SEC_PER_DAY;
}

/******************************************************************************
** Function:	Get timestamp for a log header
**
** Notes:		Returns one interval before time passed in, converted to BCD
*/
uint32 LOG_get_timestamp(uint32 t, uint8 time_enum)
{
	if (time_enum == 0)
		return t;
	
	if (time_enum > sizeof(LOG_interval_sec) / sizeof(LOG_interval_sec[0]))
		return SLP_NO_WAKEUP;

	t /= LOG_interval_sec[time_enum];	// t = number of intervals
	return RTC_sec_to_bcd((t > 0) ?
		LOG_interval_sec[time_enum] * (t - 1) : RTC_SEC_PER_DAY - LOG_interval_sec[time_enum]);
}

/******************************************************************************
** Function:	Set flag so log will flush when channel tasks complete
**
** Notes:	
*/
void LOG_flush(void)
{
	log_pending_flush = true;
}

/******************************************************************************
** Function:	Start log flush immediately and switch logging queue buffer
**
** Notes:	
*/
void log_immediate_flush(void)
{
	if ((log_write_mask | log_sms_write_mask | log_derived_write_mask | log_derived_sms_write_mask | log_control_write_mask)!= 0x0000)
		return;																	// return if flush already in progress
																				// else:
	log_a_active = !log_a_active;
	log_write_length = log_queue_tail;
	log_queue_tail = 0;
	log_write_sms = false;														// normal logged data first	
	log_write_derived = false;
	log_write_mask = log_active_mask;
	log_active_mask = 0x0000;
	log_derived_write_mask = log_derived_active_mask;
	log_derived_active_mask = 0x0000;
	log_sms_write_mask = log_sms_active_mask;
	log_sms_active_mask = 0x0000;
	log_derived_sms_write_mask = log_derived_sms_active_mask;
	log_derived_sms_active_mask = 0x0000;
	log_control_write_mask = log_control_active_mask;							// control outputs
	log_control_active_mask = 0x0000;
}

/******************************************************************************
** Function:	Check whether a flush is in progress, or channels updating
**
** Notes:	
*/
bool LOG_busy(void)
{
	if (log_pending_flush || ((log_write_mask | log_sms_write_mask | log_derived_write_mask | log_derived_sms_write_mask | log_control_write_mask) != 0x0000))
		return true;
/*
#ifndef HDW_RS485	
	if (DIG_busy())
		return true;
#else
	if (DOP_busy())
		return true;
#endif

#ifndef HDW_GPS
	if (ANA_busy())
		return true;
#endif
*/	
	return false;
}

/******************************************************************************
** Function:	Enqueue one data item (timestamp for header, or logged data)
**
** Notes:	
*/
void log_enqueue(uint8 channel_number, uint8 data_type, int32 value)
{
	log_queue_type *p;

	if (log_queue_tail >= LOG_QUEUE_SIZE - 32)		// flush to file when queue nearly full
	{
		if (log_queue_tail >= LOG_QUEUE_SIZE - 2)	// queue full
		{
			log_immediate_flush();
			if (log_queue_tail != 0)				// we were unable to flush
			{
				log_queue_overflow = true;
				return;
			}
		}
		else
			LOG_flush();							// set pending flush
	}

	p = log_a_active ? &log_queue_a[log_queue_tail] : &log_queue_b[log_queue_tail];
	p->channel_number = channel_number;
	p->data_type = data_type;
	p->value = value;

	log_queue_tail++;
}

/******************************************************************************
** Function:	Compress one 32 bit floating point value (cast as int32) into 3 extended-ASCII chars
**
** Notes:		returns length of string (3)	
*/
int log_compress_value(char * buffer_p, uint32 value)
{
	uint32 mask21;

	mask21 = STR_float_32_to_21(value);

	// convert to ascii in buffer - little endian
	*buffer_p = (uint8)(mask21) & 0x7F;
	*buffer_p += (*buffer_p < 64) ? 48 : 112;
	buffer_p++; 
	mask21 >>= 7;
	*buffer_p = (uint8)(mask21) & 0x7F;
	*buffer_p += (*buffer_p < 64) ? 48 : 112;
	buffer_p++; 
	mask21 >>= 7;
	*buffer_p = (uint8)(mask21) & 0x7F;
	*buffer_p += (*buffer_p < 64) ? 48 : 112;
	
	return 3; 
}

/******************************************************************************
** Function:	Code 28 bit value (int32) into 4 extended-ASCII chars
**
** Notes:		returns length of string (4)	
*/
int log_code_event_value(char * buffer_p, int32 value)
{
	uint8 character;

	// convert to ascii in buffer - little endian
	character = (uint8)(value) & 0x7F;
	character += (character < 64) ? 48 : 112;
	*buffer_p++ = character; 
	value >>= 7;
	character = (uint8)(value) & 0x7F;
	character += (character < 64) ? 48 : 112;
	*buffer_p++ = character; 
	value >>= 7;
	character = (uint8)(value) & 0x7F;
	character += (character < 64) ? 48 : 112;
	*buffer_p++ = character;
	value >>= 7;
	character = (uint8)(value) & 0x7F;
	character += (character < 64) ? 48 : 112;
	*buffer_p = character;
	return 4; 
}

/******************************************************************************
** Function:	Create the uint32 containing the channel parameters for enqueueing a block header
**
** Notes:		Must detect an event channel performing derived value measurement
**				from configuration
**				Must handle creating correct parameters for derived digital and analogue channels
**				channel_number is 1 for D1A, 2 for D1B, 3 for D2A, 4 for D2B	
**				and 5 to 11 for analogue channels	
**				Channel number has bit 4 set if enqueued value is for SMS log.
**				Channel number has bit 5 set if enqueued value is for derived data log.
**				Channel number has bits 4 and 5 set if enqueued value is for derived sms data log.
**
*/
uint32 log_create_block_header_params(uint8 channel_number)
{
	uint8 index, channel;
#ifndef HDW_GPS
	ANA_config_type * p_a_config;
#endif
#ifndef HDW_RS485
	DIG_config_type * p_config;
	DIG_sub_event_config_type * p_sub_config;
	uint8 sub_index;
#endif
	uint32 output = 0;
	bool   sms = ((channel_number & LOG_SMS_MASK) != 0);
	bool   derived = ((channel_number & LOG_DERIVED_MASK) != 0);

	channel = channel_number & 0x0F;
	if (channel > 0)																			// safety check
	{
		if (channel <= LOG_SERIAL_8_INDEX)
		{
			index = channel - LOG_SERIAL_1_INDEX;
			//p_a_config = &ANA_config[index];
			//if (sms)
			//	output = (uint32)(derived ? p_a_config->derived_sms_message_type : p_a_config->sms_message_type);
			output <<= 8;
			output |= (uint32)(MOD_config.interval);
			output <<= 8;
			output |= (uint32)(MOD_channel_description[index]);
			output <<= 8;
			output |= (uint32)(MOD_channel_units[index]);
		}
		else if (channel < LOG_ANALOGUE_1_INDEX)
		{
/*
#ifdef HDW_RS485
																								// serial doppler - only three valid
			if (!derived)
			{
				if (channel == 	LOG_SERIAL_1_INDEX)												// serial velocity
				{
					if (sms)
						output = (uint32)DOP_config.velocity_sms_message_type;
					output <<= 8;
					output |= (uint32)(sms ? DOP_config.sms_data_interval : DOP_config.log_interval);
					output <<= 8;
					output |= (uint32)(DOP_config.velocity_description_index);
					output <<= 8;
					output |= (uint32)(DOP_config.velocity_units_index);
				}
				else if (channel == LOG_SERIAL_2_INDEX)											// serial temperature
				{
					if (sms)
						output = (uint32)DOP_config.temperature_sms_message_type;
					output <<= 8;
					output |= (uint32)(sms ? DOP_config.sms_data_interval : DOP_config.log_interval);
					output <<= 8;
					output |= (uint32)(DOP_config.temperature_description_index);
					output <<= 8;
					output |= (uint32)(DOP_config.temperature_units_index);
				}
				else if (channel == LOG_SERIAL_3_INDEX)											// serial depth
				{
					if (sms)
						output = (uint32)DOP_config.depth_sms_message_type;
					output <<= 8;
					output |= (uint32)(sms ? DOP_config.sms_data_interval : DOP_config.log_interval);
					output <<= 8;
					output |= (uint32)(DOP_config.depth_description_index);
					output <<= 8;
					output |= (uint32)(DOP_config.depth_units_index);
				}
			}
			else
			{
				if (channel == 	LOG_SERIAL_1_INDEX)												// serial flow derived from velocity
				{
					if (sms)
						output = (uint32)DOP_config.flow_sms_message_type;
					output <<= 8;
					output |= (uint32)(sms ? DOP_config.sms_data_interval : DOP_config.log_interval);
					output <<= 8;
					output |= (uint32)(DOP_config.flow_description_index);
					output <<= 8;
					output |= (uint32)(DOP_config.flow_units_index);
				}
			}
#else
																								// digital
			index = (channel - LOG_DIGITAL_1A_INDEX) >> 1;
			sub_index = (channel - LOG_DIGITAL_1A_INDEX) - (index * 2);
			p_config = &DIG_config[index];
			p_sub_config = &p_config->ec[sub_index];
			if (((p_config->sensor_type &0xF0) > 0x00) && (p_sub_config->sensor_type > 0x00))	// if logging values from event inputs
			{
				if (sms)
					output = (uint32)p_sub_config->sms_message_type;
				output <<= 8;
				output |= (uint32)(sms ? p_sub_config->sms_data_interval : p_sub_config->log_interval);
				output <<= 8;
				output |= (uint32)(p_sub_config->description_index);
				output <<= 8;
				output |= (uint32)(p_sub_config->output_units_index);
			}
			else																				// logging pulse counts from SNS
			{
				if (sms)
					output = (uint32)p_config->sms_message_type;
				output <<= 8;
				output |= (uint32)(sms ? p_config->sms_data_interval : p_config->log_interval);
				output <<= 8;
				if (derived)
				{																				// set volume and units from totaliser
					output |= (uint32)(4);
					output <<= 8;
					output |= (uint32)(DIG_channel[index].sub[sub_index].totaliser.units_enumeration);
				}
				else
				{
					output |= (uint32)(p_config->description_index);
					output <<= 8;
					output |= (uint32)(p_config->units_index);
				}
			}
#endif
*/
/*
			index = channel - LOG_SERIAL_1_INDEX;
			//if (sms)
			//	output = (uint32)DOP_config.velocity_sms_message_type;
			output <<= 8;
			output |= (uint32)(MOD_config.interval);
			output <<= 8;
			output |= (uint32)(MOD_channel_description[index]);
			output <<= 8;
			output |= (uint32)(MOD_channel_units[index]);
*/
		}
#ifndef HDW_GPS

		else if (channel <= LOG_ANALOGUE_3_INDEX)
		{
																								// analogue
			index = channel - LOG_ANALOGUE_1_INDEX;
			p_a_config = &ANA_config[index];
			if (sms)
				output = (uint32)(derived ? p_a_config->derived_sms_message_type : p_a_config->sms_message_type);
			output <<= 8;
			output |= (uint32)(sms ? p_a_config->sms_data_interval : p_a_config->log_interval);
			output <<= 8;
			output |= (uint32)(derived ? p_a_config->derived_description_index : p_a_config->description_index);
			output <<= 8;
			output |= (uint32)(derived ? p_a_config->derived_units_index : p_a_config->units_index);
		}
#endif
																								// else should not be here
	}
	return output;
}

/******************************************************************************
** Function:	Enqueue value for logging
**
** Notes:		NB interrupt disable required if called from mainloop
**				Channel number has bit 4 set if enqueued value is for SMS log.
**				Channel number has bit 5 set if enqueued value is for derived data log.
**				Channel number has bits 4 and 5 set if enqueued value is for derived sms data log.
**				Returns true if value enqueued, false if suppressed due to log schedule
*/
bool LOG_enqueue_value(uint8 channel_number, uint8 data_type, int32 value)
{
	uint16 mask;
	uint32 timestamp;

	// NOTE: if called from interrupt, make sure RTC_now up-to-date TBD !!
	
	if (channel_number == LOG_ACTIVITY_INDEX)									// activity
	{
		// data_type is file index, value is line number (bits 17 to 31) and timestamp in seconds (bits 0 to 16)
		timestamp = RTC_time_sec & 0x0001ffff;
		timestamp |= ((value & 0x00007fff) << 17);
		log_enqueue(channel_number, data_type, timestamp);		
		log_active_mask |= 0x01;												// data in queue for activity
																				// if USB active need immediate flush
		if (USB_active)
			LOG_flush();														// write config as it is at time of logged value
	}
	else if ((channel_number & LOG_CONTROL_MASK) == LOG_CONTROL_MASK)				// control
	{
		// data_type is control type, value is type of control (bits 17 to 31) and timestamp in seconds (bits 0 to 16)
		timestamp = RTC_time_sec & 0x0001ffff;
		timestamp |= ((value & 0x00007fff) << 17);
		log_enqueue(channel_number, data_type, timestamp);		
		mask = 0x0001 << ((channel_number & 0x03) - 1);
		log_control_active_mask |= mask;										// data in queue for control output
																				// if USB active need immediate flush
		if (USB_active)
			LOG_flush();														// write config as it is at time of logged value
	}
	else																		// logged values
	{ 
		if (LOG_state != LOG_LOGGING)											// not logging
			return false;

		mask = 1 << (channel_number & 0x0F);
		if ((channel_number & LOG_SMS_MASK) == 0)
		{
			if ((channel_number & LOG_DERIVED_MASK) == 0)
				log_active_mask |= mask;										// normal data in queue for this channel
			else
				log_derived_active_mask |= mask;								// derived data in queue for this channel
		}
		else																	// else SMS data
		{
			if ((channel_number & LOG_DERIVED_MASK) == 0)
				log_sms_active_mask |= mask;									// sms data in queue for this channel
			else
				log_derived_sms_active_mask |= mask;							// derived sms data in queue for this channel
		}
	
		log_enqueue(channel_number, data_type, value);
		if (data_type == LOG_BLOCK_HEADER_TIMESTAMP)							// timestamp - need to enqueue channel parameters at time of enqueueing also
			log_enqueue(channel_number, LOG_BLOCK_HEADER_PARAMS, log_create_block_header_params(channel_number));

																				// if a header, or if USB active, need immediate flush
		if ((data_type == LOG_BLOCK_HEADER_TIMESTAMP) || (data_type == LOG_EVENT_HEADER) || USB_active)
			LOG_flush();														// write config as it is at time of logged value
	}

	return true;
}

/******************************************************************************
** Function:	Create an event file header
**
** Notes:		channel_index for event logging channel LOG_EVENT_1A_INDEX to LOG_EVENT_2B_INDEX
**
*/
int LOG_create_event_header(char * buffer_p, int channel_index, RTC_type * time_stamp_p)
{
	int len = 0;

	// channel name, date
	len = sprintf(buffer_p, "\r\n\"%s,%02X%02X%02X,",
		LOG_channel_id[channel_index - LOG_EVENT_1A_INDEX + 1], 
		time_stamp_p->day_bcd, time_stamp_p->mth_bcd, time_stamp_p->yr_bcd);

	// create event configure filename
	sprintf(log_use_string, "EVENT%s.TXT", (char *)&LOG_channel_id[channel_index - LOG_EVENT_1A_INDEX + 1]);
	// if event configure file exists
	if (CFS_read_line((char *)CFS_config_path, (char *)log_use_string, 1, &log_use_string[20], sizeof(log_use_string) - 30) > 0)
	{
		// add event configure file contents
		len += sprintf(&buffer_p[len], "%s", &log_use_string[20]);
	}
	// terminate
	len += sprintf(&buffer_p[len], "\r\n");

	return len;
}
 
/******************************************************************************
** Function:	Create an enqueued block or file header
**
** Notes:		gets parameters from a uint32 from the queued data
**
*/
int log_create_enqueued_block_header(char * buffer_p, int channel_index, RTC_type * time_stamp_p, uint32 params)
{ 
	uint8 sms_type, interval, description, units;
	int len = 0;

	// extract parameters
	units = (uint8)(params & 0x000000ff);
	params >>= 8;
	description = (uint8)(params & 0x000000ff);
	params >>= 8;
	interval = (uint8)(params & 0x000000ff);
	params >>= 8;
	sms_type = (uint8)(params & 0x000000ff);

	// channel name, date, time
	len = sprintf(buffer_p, "\r\n!%s,%02X%02X%02X,%02X:%02X:%02X,",
		LOG_channel_id[channel_index], 
		time_stamp_p->day_bcd, time_stamp_p->mth_bcd, time_stamp_p->yr_bcd,
		time_stamp_p->hr_bcd, time_stamp_p->min_bcd, time_stamp_p->sec_bcd);

	// add log interval, channel description, units enums
	len += sprintf(&buffer_p[len], "%u,%u,%u", interval, description, units); 
	if (sms_type != 0x00)
	{
		len += sprintf(&buffer_p[len], ",%02x", sms_type); 
	}
#ifdef HDW_GPS
	// for GPS product, add last data from on-board GPS module
	len += sprintf(&buffer_p[len], ",%s,%s,%s,%s,%s,%s",
		GPS_time, GPS_latitude, GPS_NS, GPS_longitude, GPS_EW, GPS_fix);

	if (GPS_config.truck_mode)		// only write this fix to file once in truck mode
		GPS_clear_fix();
//#else
//	// for normal product, add gps string, will be populated if GPS.TXT exists
//	len += sprintf(&buffer_p[len], ",%s", log_gps_string);
#endif
	len += sprintf(&buffer_p[len], "\r\n");

	return len;
}

/******************************************************************************
** Function:	Create a block or file header
**
** Notes:		must get correct parameters - normal or derived
**				only called by ftp so does not touch sms stuff
**              channel index 0 - 10
**
*/
int LOG_create_block_header(char * buffer_p, int channel_index, RTC_type * time_stamp_p)
{ 
	int j, len = 0;
#ifndef HDW_RS485
	int k;
#endif

	len = sprintf(buffer_p, "\r\n!%s,%02X%02X%02X,%02X:%02X:%02X,",							// channel name, date, time
		LOG_channel_id[channel_index + 1], 
		time_stamp_p->day_bcd, time_stamp_p->mth_bcd, time_stamp_p->yr_bcd,
		time_stamp_p->hr_bcd, time_stamp_p->min_bcd, time_stamp_p->sec_bcd);
																							// add log interval, channel description, units enums
	if (channel_index >= FTP_NUM_FTPR_CHANNELS)												// if out of range
	{
		len = 0;																			// no header produced
	}
#ifndef HDW_GPS
	else if (channel_index >= FTP_DERIVED_ANA_INDEX)										// if derived analogue
	{
		j = channel_index - FTP_DERIVED_ANA_INDEX;
		len += sprintf(&buffer_p[len], "%u,%u,%u", 
					   ANA_config[j].log_interval, 
					   ANA_config[j].derived_description_index, 
					   ANA_config[j].derived_units_index); 
	}
#endif
	else if (channel_index >= FTP_DERIVED_BASE_INDEX)										// if derived digital or RS485
	{
/*
#ifndef HDW_RS485
		j = (channel_index - FTP_DERIVED_BASE_INDEX) >> 1;
		k = channel_index - (j * 2);						
		len += sprintf(&buffer_p[len], "%u,%u,%u", 
					   DIG_config[j].log_interval, 
					   4, 																	// 4 for volume (derived from flow)
					   DIG_channel[j].sub[k].totaliser.units_enumeration); 
#else
		if (channel_index == FTP_SER_DERIVED_FLOW_INDEX)									// RS485 derived flow
		{
			len += sprintf(&buffer_p[len], "%u,%u,%u", 
					   DOP_config.log_interval, 
					   DOP_config.flow_description_index, 
					   DOP_config.flow_units_index); 
		}
#endif
*/
	}
#ifndef HDW_GPS
	else if (channel_index >= FTP_ANA_FTPR_INDEX)											// if normal analogue
	{
		j = channel_index - FTP_ANA_FTPR_INDEX;
		len += sprintf(&buffer_p[len], "%u,%u,%u", 
					   ANA_config[j].log_interval, 
					   ANA_config[j].description_index, 
					   ANA_config[j].units_index); 
	}
#endif
	else																					// else normal digital or RS485
	{
		// modbus channel
		len += sprintf(&buffer_p[len], "%u,%u,%u",
						MOD_config.interval,
						MOD_channel_description[channel_index],
						MOD_channel_units[channel_index]);
/*
#ifdef HDW_RS485
		if (channel_index == FTP_SER_VELOCITY_INDEX)										// RS485
		{
			len += sprintf(&buffer_p[len], "%u,%u,%u", 
					   DOP_config.log_interval, 
					   DOP_config.velocity_description_index, 
					   DOP_config.velocity_units_index); 
		}
		else if (channel_index == FTP_SER_TEMPERATURE_INDEX)
		{
			len += sprintf(&buffer_p[len], "%u,%u,%u", 
					   DOP_config.log_interval, 
					   DOP_config.temperature_description_index, 
					   DOP_config.temperature_units_index); 
		}
		else if (channel_index == FTP_SER_DEPTH_INDEX)
		{
			len += sprintf(&buffer_p[len], "%u,%u,%u", 
					   DOP_config.log_interval, 
					   DOP_config.depth_description_index, 
					   DOP_config.depth_units_index); 
		}
#else
		j = channel_index / 2;
		k = channel_index % 2;
		if (((DIG_config[j].sensor_type & 0xf0) >= 0x10) && 								// if digital event channel logging values
			(DIG_config[j].ec[k].sensor_type > 0))
		{
			len += sprintf(&buffer_p[len], "%u,%u,%u", 
						   DIG_config[j].ec[k].log_interval, 
						   DIG_config[j].ec[k].description_index, 
						   DIG_config[j].ec[k].output_units_index); 
		}
		else																				// else normal digital
		{
			len += sprintf(&buffer_p[len], "%u,%u,%u", 
						   DIG_config[j].log_interval, 
						   DIG_config[j].description_index, 
						   DIG_config[j].units_index); 
		}
#endif
*/
#ifdef HDW_GPS
		// for GPS, add last data from on-board GPS module
		len += sprintf(&buffer_p[len], ",%s,%s,%s,%s,%s,%s",
			GPS_time, GPS_latitude, GPS_NS, GPS_longitude, GPS_EW, GPS_fix);
//#else
//		// for normal product, add gps string, will be populated if GPS.TXT exists
//	len += sprintf(&buffer_p[len], ",%s", log_gps_string);
#endif
	}
	len += sprintf(&buffer_p[len], "\r\n");

	return len;
}

/******************************************************************************
** Function:	Write enqueued values to file system from log_queue_a or _b
**
** Notes:		Example path for normal logged data:	\LOGDATA\D1A\2009\05
**														|-------|---|----|--|
**														    A     B    C   D
**				A = fixed path, B = channel id, C = year, D = month
**				For SMS data, field A is \SMSDATA
**				For control data, field A is \COPDATA 
**
**				Filenames include channel id, day & month, e.g. D1A-2705.TXT	NB changed from '_' to '-' 010909 PB
*/
void log_write_to_file(int channel_index)
{
	FSFILE *f;
	int len;
	int i;
	int file_index;
	int name_index = 0;
	uint32 j;
	log_queue_type *p;
	float fpn;
	RTC_type time_stamp;
																								// File system already opened by LOG_task.
	if ((LOG_state == LOG_BATT_DEAD) || (CFS_state != CFS_OPEN))
		return;

	// get contents of GPS.TXT if it exists
	//CFS_read_line((char *)CFS_config_path, (char *)CFS_gps_name, 1, log_gps_string, 40);

	p = log_a_active ? log_queue_b : log_queue_a;												// get pointer to the write queue, i.e. the one we're not logging into

	if (channel_index == LOG_ACTIVITY_INDEX)													// Generate full path to file where data will be written
	{
		sprintf(STR_buffer, "\\ACTIVITY\\20%02X\\%02X", log_yr_bcd, log_mth_bcd);				// generate path and check for its existence
		if (FSchdir(STR_buffer) != 0)				// can't cd
		{
			FSmkdir(STR_buffer);
			FSchdir(STR_buffer);
		}
		sprintf(STR_buffer, "ACT-%02X%02X.TXT", log_day_bcd, log_mth_bcd);						// Generate logged activity filename, e.g. ACT-2705.TXT:
	}
	else if ((channel_index & LOG_CONTROL_MASK) == LOG_CONTROL_MASK)
	{
		sprintf(STR_buffer, "\\COPDATA\\%u\\20%02X\\%02X", 										// generate path for control data and check for its existence
				channel_index & 0x03, log_yr_bcd, log_mth_bcd);
		if (FSchdir(STR_buffer) != 0)				// can't cd
		{
			FSmkdir(STR_buffer);
			FSchdir(STR_buffer);
		}
		sprintf(STR_buffer, "%u-%02X%02X.TXT", 													// Generate logged activity filename, e.g. 1-2705.TXT:
				channel_index & 0x03, log_day_bcd, log_mth_bcd);
	}
	else if ((channel_index >= LOG_EVENT_1A_INDEX) && (channel_index <= LOG_EVENT_2B_INDEX))
	{
		// pre-fetch event header in case we need it
		// have to do this here as it needs data from event configure file and we cannot have two files open at once
		time_stamp.yr_bcd = log_yr_bcd;															// pack header date into RTC_type
		time_stamp.mth_bcd = log_mth_bcd;
		time_stamp.day_bcd = log_day_bcd;
																								// create event header
		len = LOG_create_event_header(STR_buffer, channel_index, &time_stamp);
		strcpy(log_use_string, STR_buffer); 
																								// generate path for event data and check for its existence
		sprintf(STR_buffer, "\\LOGDATA\\%s\\20%02X\\%02X", 
				LOG_channel_id[channel_index - LOG_EVENT_1A_INDEX + 1], log_yr_bcd, log_mth_bcd);
		if (FSchdir(STR_buffer) != 0)															// can't cd
		{
			FSmkdir(STR_buffer);
			FSchdir(STR_buffer);
		}
		sprintf(STR_buffer, "%s-%02X%02X.TXT", 													// Generate logged event filename, e.g. D1A-2705.TXT:
				LOG_channel_id[channel_index - LOG_EVENT_1A_INDEX + 1], log_day_bcd, log_mth_bcd);
	}
	else
	{
																								// generate path for data and check for its existence
		name_index = log_write_derived ? channel_index + 11 : channel_index;
		if (log_write_sms)
			sprintf(STR_buffer, "\\%s\\%s\\20%02X\\%02X", "SMSDATA", LOG_channel_id[name_index], log_yr_bcd, log_mth_bcd);
		else
			sprintf(STR_buffer, "\\%s\\%s\\20%02X\\%02X", "LOGDATA", LOG_channel_id[name_index], log_yr_bcd, log_mth_bcd);

		if (FSchdir(STR_buffer) != 0)															// can't cd
		{
			FSmkdir(STR_buffer);
			FSchdir(STR_buffer);
		}
																								// Generate logged data filename, e.g. D1A-2705.TXT:
		sprintf(STR_buffer, "%s-%02X%02X.TXT", LOG_channel_id[name_index], log_day_bcd, log_mth_bcd);
	}

	f = FSfopen(STR_buffer, "a");
	if (f == NULL)
		return;																					// not a lot we can do
																								// else print values to STR_buffer, then dump STR_buffer to file:

	// NB no returns from here on, as we must close the file.
	len = 0;
	for (i = 0; i < log_write_length; i++)
	{
		file_index = channel_index;
		if (log_write_sms)																		// look for SMS data for this channel
			file_index |= LOG_SMS_MASK;
		if (log_write_derived)																	// look for DERIVED data for this channel
			file_index |= LOG_DERIVED_MASK;
																								// print no more than 20 chars at a time, without flushing to file first
																								// otherwise STR_buffer may overflow
		if (p[i].channel_number == file_index)
		{
			if (file_index == LOG_ACTIVITY_INDEX)
			{
				if (len > 0)																	// go back to start of STR_buffer
					FSfwrite(STR_buffer, len, 1, f);
				j = RTC_sec_to_bcd(p[i].value & 0x0001FFFF);									// decode timestamp
				len = sprintf(STR_buffer, "%02x%02x%02x ", BITS16TO23(j), BITS8TO15(j), BITS0TO7(j)); 
				if (p[i].data_type > LOG_NUM_C_FILES)
					p[i].data_type = LOG_NUM_C_FILES;			// decode file id
				len += sprintf(&STR_buffer[len], " %s ", log_c_file[p[i].data_type]);			// add to output string
																								// decode line number
				len += sprintf(&STR_buffer[len], "%ld\r\n", (p[i].value >> 17) & 0x00007FFF);	// add to output string
			}
			else if ((file_index & LOG_CONTROL_MASK) == LOG_CONTROL_MASK)						// if control logging
			{
				if (len > 0)																	// go back to start of STR_buffer
					FSfwrite(STR_buffer, len, 1, f);
				j = RTC_sec_to_bcd(p[i].value & 0x0001FFFF);									// decode timestamp
				len = sprintf(STR_buffer, "%02x:%02x:%02x,", BITS16TO23(j), BITS8TO15(j), BITS0TO7(j)); 
				len += COP_value_to_string((uint8)((p[i].value >> 17) & 0x0000FF));				// add letters decoded from value and "\r\n" 
			}
			else
			{
				switch (p[i].data_type)
				{
				case LOG_DATA_VALUE:
					if (log_write_sms)
					{
						fpn = *(float *)&p[i].value;											// write sms data uncompressed, one value per line
						len += sprintf(&STR_buffer[len], "%1.3G\r\n", (double)fpn);
					}
					else
					{
						len += log_compress_value(&STR_buffer[len], (uint32)p[i].value);		// convert value to compressed ASCII
						if (++log_char_count[file_index] >= 26)									// add CRLF every 26 from last header - separate counts for each channel
						{
							log_char_count[file_index] = 0;
							len += sprintf(&STR_buffer[len], "\r\n");
						}
					}
					break;

				case LOG_EVENT_TIMESTAMP:
					if (len > 0)																// go back to start of STR_buffer
						FSfwrite(STR_buffer, len, 1, f);
																								// encode event timestamp into 7 bit ASCII
																								// convert value to compressed ASCII
					len = log_code_event_value(STR_buffer, p[i].value);
					if (++log_char_count[file_index] >= 20)										// add CRLF every 20 from last header - separate counts for each channel
					{
						log_char_count[file_index] = 0;
						len += sprintf(&STR_buffer[len], "\r\n");
					}
					break;
	
				case LOG_EVENT_HEADER:
																								// write single line event header
					if (len > 0)																// go back to start of STR_buffer
						FSfwrite(STR_buffer, len, 1, f);
					strcpy(STR_buffer, log_use_string);											// get pre-fetched header
					len = strlen(STR_buffer);
					log_char_count[file_index] = 0;												// reset character counts for CRLF for file formatting
					break;
	
				case LOG_BLOCK_HEADER_TIMESTAMP:												// write single line data header
					if (len > 0)																// go back to start of STR_buffer
						FSfwrite(STR_buffer, len, 1, f);
					time_stamp.yr_bcd = log_yr_bcd;												// pack header date and time into RTC_type
					time_stamp.mth_bcd = log_mth_bcd;
					time_stamp.day_bcd = log_day_bcd;
					time_stamp.reg32[0] = p[i].value;
																								// get parameters from next queue item 
																								// (will always be block header parameters for this channel, 
																								// but better check)
					if ((p[i+1].data_type == LOG_BLOCK_HEADER_PARAMS) && (p[i+1].channel_number == file_index))
						j = p[i + 1].value;
					else
						j = log_create_block_header_params(file_index);							// get current parameters of channel
																								// create block header
					len = log_create_enqueued_block_header(STR_buffer, name_index, &time_stamp, j); 
					log_char_count[file_index] = 0;												// reset character counts for CRLF for file formatting
					break;
	

				case LOG_BLOCK_FOOTER:															// write single line data footer
					if (len > 0)																// go back to start of STR_buffer
						FSfwrite(STR_buffer, len, 1, f);
					len = 0;
																								// fetch block footer

					if ((channel_index >= LOG_SERIAL_1_INDEX) && (channel_index < LOG_ANALOGUE_1_INDEX))
					{
						uint8 channel = channel_index - LOG_SERIAL_1_INDEX;
						len = LOG_print_footer_min_max(STR_buffer, &MOD_channel_mins_time[channel], MOD_channel_mins[channel],
																	&MOD_channel_maxes_time[channel], MOD_channel_maxes[channel]);
						MOD_channel_mins[channel] = FLT_MAX;
						memset(&MOD_channel_mins_time[channel], 0, sizeof(RTC_hhmmss_type));
						MOD_channel_maxes[channel] = -FLT_MAX;
						memset(&MOD_channel_maxes_time[channel], 0, sizeof(RTC_hhmmss_type));
					}
/*
#ifndef HDW_RS485
					if ((channel_index >= LOG_DIGITAL_1A_INDEX) && (channel_index <= LOG_DIGITAL_2B_INDEX))
					{
						len = DIG_create_block_footer(STR_buffer, channel_index - LOG_DIGITAL_1A_INDEX, log_write_sms, log_write_derived); 
#else
					if ((channel_index >= LOG_SERIAL_1_INDEX) && (channel_index < LOG_ANALOGUE_1_INDEX))
					{
						len = DOP_create_block_footer(STR_buffer, channel_index - LOG_SERIAL_1_INDEX, log_write_derived); 
#endif

						// Add signal strength & batt volts to end of digital footer:
						len += sprintf(&STR_buffer[len], ",%4.2f,%4.2f,%d%%\r\n",
									(double)PWR_int_bat_volts, (double)PWR_ext_supply_volts, (COM_csq * 100) / 31);
					}
#ifndef HDW_GPS
					else if ((channel_index >= LOG_ANALOGUE_1_INDEX) && (channel_index <= LOG_ANALOGUE_7_INDEX))
						len = ANA_create_block_footer(STR_buffer, channel_index - LOG_ANALOGUE_1_INDEX, log_write_derived); 
#endif
*/
					log_char_count[file_index] = 0;												// reset character counts for CRLF for file formatting
					break;

#ifndef HDW_RS485
				case LOG_TOTALISER_TIMESTAMP:
					if (len > 0)																// go back to start of STR_buffer
					{
						FSfwrite(STR_buffer, len, 1, f);
						len = sprintf(STR_buffer, "\r\n");
					}
					else
						len = 0;

					log_char_count[file_index] = 0;
					
					j = RTC_sec_to_bcd(p[i].value & 0x0001FFFF);
					len += sprintf(&STR_buffer[len], "$%02x:%02x:%02x,", BITS16TO23(j), BITS8TO15(j), BITS0TO7(j));

					len += DIG_print_totaliser_int(&STR_buffer[len],
						&DIG_channel[(channel_index - 1) >> 1].sub[(channel_index - 1) & 1].totaliser.value_x10000);
					i = DIG_channel[(channel_index - 1) >> 1].sub[(channel_index - 1) & 1].totaliser.value_x10000 % 10000;
					len += sprintf(&STR_buffer[len], ".%04d\r\n", i);
					break;
#endif

				default:
					break;
				}
			}
			if (len > sizeof(STR_buffer) - 80)													// no more room in STR_buffer
			{
				FSfwrite(STR_buffer, len, 1, f);
				len = 0;
			}
		}
	}

	if (len > 0)																				// Write any remaining chars
		FSfwrite(STR_buffer, len, 1, f);

	CFS_close_file(f);
}

/******************************************************************************
** Function:	New day task
**
** Notes:		Execute whenever date needs to change
*/
void log_new_day_task(void)
{
	uint16 i;
	int j;
	char * char_ptr;

	if (!log_new_day)																			// prepare to close old day
	{
																								// start waking file system up - will need it when we log modem usage.
																								// NB when we do the version without a modem in: don't let the logger go back to sleep
																								// when log_new_day is true. This can happen if logger is programmed for event logging
																								// and no other logging.
		if (CFS_open())
		{
																								// Adjust sample times which are >= 86400
																								// DIG & ANA tasks will then enqueue logs for midnight samples
			for (i = 0; i < SLP_num_wakeup_sources; i++)
			{
				if ((*SLP_wakeup_times[i] != SLP_NO_WAKEUP) && (*SLP_wakeup_times[i] >= RTC_SEC_PER_DAY))
					*SLP_wakeup_times[i] -= RTC_SEC_PER_DAY;
			}
	
			log_new_day = true;
			log_close_old_day = true;
		}
	}
	else if (log_close_old_day)																	// check for measurements & pending writes complete
	{
		if (((log_write_mask | log_sms_write_mask | log_derived_write_mask | log_derived_sms_write_mask | log_control_write_mask) == 0x0000) && 
#ifndef HDW_RS485
			!DIG_busy() 
#else
			//!DOP_busy()
			1
#endif
#ifndef HDW_GPS
			&& !ANA_busy()
#endif
			)
		{
			log_immediate_flush();

			LOG_header_mask = 0x0000;															// New data block headers required for any new logged values
			LOG_derived_header_mask = 0x0000;
			LOG_sms_header_mask = 0x0000;
			LOG_derived_sms_header_mask = 0x0000;

			log_close_old_day = false;
		}
	}
	else if ((log_write_mask | log_sms_write_mask | log_derived_write_mask | log_derived_sms_write_mask | log_control_write_mask) == 0x0000)	// all writes to file done
	{
		if ((COM_commissioning_mode == 1) && ALM_com_mode_alarm_enable) 						// check on commissioning status and enable flag
		{
			j = sprintf(STR_buffer, "dALARM=%02X%02X%02X,%02X:%02X:%02X,%s,",					// if 1, send the CM=1 alarm to all alarm numbers
				RTC_now.day_bcd, RTC_now.mth_bcd, RTC_now.yr_bcd,
				RTC_now.hr_bcd, RTC_now.min_bcd, RTC_now.sec_bcd,
				COM_sitename);
			char_ptr = &STR_buffer[j];
			if (!CFS_open() || !CFS_read_file((char *)CFS_config_path, (char *)"ALMCM.TXT", char_ptr, 256))	// get message from ALMCM.TXT file - allow 256 characters
			{
				*char_ptr = '\0';																// if no file concatinate default string
				strcat(STR_buffer, "CM=1");
			}
			ALM_send_to_all_sms_numbers();
			COM_commissioning_mode = 0;															// force out of commissioning mode
			COM_cancel_com_mode();
		}

		if (CAL_build_info.modem)
		{
			if (!MDM_log_use())
				return;																			// wait for file system
		}
		
		PWR_set_pending_batt_test(true);														// Ensure batteries tested every day
		if ((PWR_measurement_flags & PWR_MASK_BATT_ALARM_SENT) != 0)
			PWR_tx_internal_batt_alarm();

		log_yr_bcd = RTC_now.yr_bcd;															// Generate date used in logging path & filename for today
		log_mth_bcd = RTC_now.mth_bcd;															// (also indicates that routine has run to completion)
		log_day_bcd = RTC_now.day_bcd;

		log_new_day = false;																	// midnight task done
	}
}

/******************************************************************************
** Function:	Set wakeup time according to logging state before going to sleep
**
** Notes:	
*/
void LOG_set_wakeup_time(void)
{
	LOG_wakeup_time = SLP_NO_WAKEUP;	// by default

	if (LOG_state <= LOG_STOPPED)
		return;
	// else:

	if (LOG_state == LOG_PRE_LOGGING)	// look for start time
	{
		if (RTC_start_stop_today(&LOG_config.start))
			LOG_wakeup_time = RTC_bcd_time_to_sec(LOG_config.start.hr_bcd, LOG_config.start.min_bcd, 0);
	}	// else logging - look for end time
	else if (RTC_start_stop_today(&LOG_config.stop))
		LOG_wakeup_time = RTC_bcd_time_to_sec(LOG_config.stop.hr_bcd, LOG_config.stop.min_bcd, 0);
}

/******************************************************************************
** Function:	Logging task
**
** Notes:
*/
void LOG_task(void)
{
	int    channel_index;
	uint16 mask;

	if (RTC_start_stop_event(&LOG_config.stop))										// stop time in the past or now
	{
		if (LOG_state > LOG_STOPPED)												// need to stop now
		{
			if (!CFS_open())														// waiting for file system
				return;
#ifndef HDW_RS485
			if (!DIG_busy()
#else
			//if (!DOP_busy()
			if (1
#endif
#ifndef HDW_GPS
				 && !ANA_busy()
#endif
			   )
			{
				LOG_flush();														// only flush if can
				LOG_state = LOG_STOPPED;											// power down transducers:
#ifndef HDW_RS485
				DIG_start_stop_logging();
#endif
#ifndef HDW_GPS
				ANA_start_stop_logging();
#endif
			}
		}
	}
	else if (RTC_start_stop_event(&LOG_config.start))								// between start & stop logging times
	{
		if ((LOG_state != LOG_LOGGING) && (LOG_state != LOG_BATT_DEAD))				// need to start now
		{
			if (!CFS_open())														// waiting for file system
				return;
			LOG_state = LOG_LOGGING;												// set the state before powering transducers
			
			LOG_header_mask = 0x0000;												// New data block headers required for any new logged values
			LOG_derived_header_mask = 0x0000;
			LOG_sms_header_mask = 0x0000;
			LOG_derived_sms_header_mask = 0x0000;
#ifndef HDW_RS485
			DIG_start_stop_logging();
#endif
#ifndef HDW_GPS
			ANA_start_stop_logging();
#endif
			ALM_update_profile();													// trigger alarm profile fetch if can
		}
	}
	else if ((LOG_state != LOG_PRE_LOGGING) && (LOG_state != LOG_BATT_DEAD))		// go into pre-logging
	{
		if (!CFS_open())															// waiting for file system
			return;
		LOG_state = LOG_PRE_LOGGING;												// set the state before powering transducers
#ifndef HDW_RS485
		DIG_system_change_time_500ms = ((RTC_time_sec << 1) + RTC_half_sec);		// record time for potential start of system pumping
		DIG_start_stop_logging();
#endif
#ifndef HDW_GPS
		ANA_start_stop_logging();
#endif
		ALM_update_profile();														// trigger alarm profile fetch
	}

	if ((log_day_bcd != RTC_now.day_bcd) ||											// date has changed (normally midnight
		(log_mth_bcd != RTC_now.mth_bcd) || 										// but also when power up and put date in)
		(log_yr_bcd != RTC_now.yr_bcd))
	{
		log_new_day_task();															// does an immediate flush if required
	}

	if (log_pending_flush)
	{
		
#ifndef HDW_RS485
		if (!DIG_busy()
#else
		if (!SER_busy()
#endif
#ifndef HDW_GPS
			&& !ANA_busy()
#endif
		   )
		{
			log_pending_flush = false;
			log_immediate_flush();
		}
		
	}
																					// if no pending writes to file
	if ((log_write_mask | log_sms_write_mask | log_derived_write_mask | log_derived_sms_write_mask | log_control_write_mask) == 0x0000)
		return;
	if (!CFS_open())																// waiting for file system
		return;

	if (log_queue_overflow)
	{
		LOG_enqueue_value(LOG_ACTIVITY_INDEX, LOG_LOG_FILE, __LINE__);				// Log queue overflow
		log_queue_overflow = false;
	}

	mask = 0x0001;																	// find next log function which needs writing, & write it:
	for (channel_index = 0; channel_index < LOG_NUM_FUNCTIONS; channel_index++)
	{
		if ((log_write_mask & mask) != 0x0000)
		{
			log_write_to_file(channel_index);
			log_write_mask &= ~mask;
			break;
		}
		mask <<= 1;
	}
																					// if we've finished emptying the write queue
	if (log_write_mask == 0x0000)
	{
		if (log_sms_write_mask != 0x0000)											// if SMS to write
		{
			log_write_sms = true;													// do SMS data next
			log_write_derived = false;
			log_write_mask = log_sms_write_mask;
			log_sms_write_mask = 0x0000;											// don't do them again
		}
		else if (log_derived_write_mask != 0x0000)									// if derived data to write
		{
			log_write_derived = true;												// then do derived data
			log_write_sms = false;
			log_write_mask = log_derived_write_mask;
			log_derived_write_mask = 0x0000;										// don't do them again
		}
		else if (log_derived_sms_write_mask != 0x0000)								// if derived sms data to write
		{
			log_write_derived = true;												// then do derived SMS data
			log_write_sms = true;
			log_write_mask = log_derived_sms_write_mask;
			log_derived_sms_write_mask = 0x0000;									// don't do them again
		}
	}
																					// if all data logging is done
	if ((log_write_mask | log_sms_write_mask | log_derived_write_mask | log_derived_sms_write_mask) == 0x0000)
	{
		log_write_sms = false;														// clear sms and derived flags	
		log_write_derived = false;
		mask = 0x0001;																// find next control output log function which needs writing, & write it:
		for (channel_index = 1; channel_index <= LOG_NUM_CONTROL_CHANNELS; channel_index++)
		{
			if ((log_control_write_mask & mask) != 0x0000)
			{
				log_write_to_file(LOG_CONTROL_MASK | channel_index);
				log_control_write_mask &= ~mask;
				break;
			}
			mask <<= 1;
		}
	}
}

/******************************************************************************
** Function:	
**
** Notes:	
**
*/
void LOG_init(void)
{
	int i;

	LOG_state = LOG_STOPPED;

	// No wakeup for anything
	for (i = 0; i < SLP_num_wakeup_sources; i++)
		*(SLP_wakeup_times[i]) = SLP_NO_WAKEUP;

	// Set widest possible window for start-stop logging:
	memset(&LOG_config.start, 0, sizeof(LOG_config.start));
	LOG_config.start.day_bcd = 0x01;
	LOG_config.start.mth_bcd = 0x01;

	LOG_config.stop.day_bcd = 0x31;
	LOG_config.stop.mth_bcd = 0x12;
	LOG_config.stop.yr_bcd = 0x99;
	LOG_config.stop.hr_bcd = 0x23;
	LOG_config.stop.min_bcd = 0x59;

	// initialise time and date
	log_yr_bcd = RTC_now.yr_bcd;
	log_mth_bcd = RTC_now.mth_bcd;
	log_day_bcd = RTC_now.day_bcd;
}

/******************************************************************************
** Function:	Print footer min/max & timestamps
**
** Notes:		Returns no. of characters printed
*/
int LOG_print_footer_min_max(char * string, RTC_hhmmss_type *p_t1, float v1, RTC_hhmmss_type *p_t2, float v2)
{
	int32 value = *(int32 *)p_t1;																// min timestamp
	int len = sprintf(string, "\r\n*%02X:%02X:%02X,", BITS16TO23(value), BITS8TO15(value), BITS0TO7(value));
	len += STR_print_float(&string[len], v1);													// min value
	string[len++] = ',';
	string[len] = '\0';
	value = *(int32 *)p_t2;																		// max timestamp
	len += sprintf(&string[len], "%02X:%02X:%02X,", BITS16TO23(value), BITS8TO15(value), BITS0TO7(value));
	len += STR_print_float(&string[len], v2);													// max value

	return len;
}




