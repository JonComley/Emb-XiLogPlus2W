/******************************************************************************
** File:	Pdu.c
**			P Boyce
**			8/7/09
**
** Notes:	PDU message coding
*/

/* CHANGES
** v2.36 261010 PB 	Code was never getting to footer of a file to retrieve total flow for F3 type FTP
**					now look for footer following last data line to be extracted from a file
**
** v2.39 231110 PB	Major rewrite of pdu_fetch_sms_data to cope with multiple headers in an sms file
**					New struct PDU_sms_header_type allows header contents to be compared
**					Removed pdu_extract_data_from_file
**
** v2.48 200111 PB  DEL76 Changes in PDU_schedule_all to implement rules for SMS types and compressed data
**					Only send compressed data SMS if both D1 and A1 are set to 0xFC
**					Do not send any SMS if either is 0xFC and other is not set to 0xFC
**					Do not send any SMS if a channel not D1 or A1 is set to 0xFC
**
** v2.49 240111 PB  DEL109 Add LOG_flush() call to PDU_task afer finding there is an SMS to send
**					Mods to pdu_fetch_sms_data and pdu_extract_header to use seconds
**					Previous code used whole minutes and caused offset problems in SMS data
**					Add rules in v2.48 for sending PDUs to PDU_increment_batch
**
** v2.49 250111 PB  Rework PDU_synchronise_batch:
**                  pdu_batch_count[x] = -1 for non-logging channels
**                                        0 at channel start if not in batch mode or no tx window set
**                                        0 - 95 at channel start if in batch mode after synchronisation to tx window
**                                        incremented every value enqueue point if in batch mode
**                                        not incremented if not in batch mode when enqueue a value
**                                        when counts over from 95 to 96 generates sms message and reset to 0
**                                        resynchronised if in batch mode with a tx window set at:
**                                            channel start - per channel
**                                            #MPS decode - all logging channels
**                                            #DT or #TC time change - all logging channels
**                                        set to -1 at power on and when a channel is stopped
**
** v2.50 030211 PB  In PDU_retrieve_data need to check for requested channel 4 of type FC from #RSD command
**                  If type FC in sms_header and channel 4, set channel to channel 0 and start again
**                  as #RSD request for channel d1a and channel a1 of type FC must result in the same thing
**                  i.e. combined data of flow and pressure from each channel
**
** v2.51 090211 PB  When set up time to retrieve sms data in PDU_rsd_retrieve.when to RTC_time_now, set seconds to zero.
**                  All sms retrieval times are in whole minutes.
**
** v2.55 230211 PB  Default value for totaliser is 0x03ffffffff. Mask off top 6 bits before adding totaliser units for SMS type F3
**
** V2.65 070411 PB added conditional compilation ifndef HDW_PRIMELOG_PLUS
**
** V3.03 231111 PB Waste Water - add code to handle derived data sms pdu tx
**								 remove unused functions pdu_set_xxx_config_defaults()
**
** V3.12 070612 PB				 ensure pdu uses correct messaging enable flag when sending event value logged data
**
** V3.17 091012 PB  bring up to date with Xilog+ V3.06 - use return value from CFS_open()
**
** V3.30 011113 PB  new code in PDU_time_for_batch() to deal with subchannel event value logging transmission enable flag and SMS types
**
** V4.00 220114 PB  if HDW_GPS disable all analogue calls and functions
*/

#include <float.h>

#include "Custom.h"
#include "Compiler.h"
#include "Str.h"
#include "HardwareProfile.h"
#include "MDD File System/FSIO.h"
#include "Cfs.h"
#include "Msg.h"
#include "rtc.h"
#include "tsync.h"
#include "Com.h"
#include "Usb.h"
#include "Log.h"
#include "Ana.h"
#include "Dig.h"
#include "Cal.h"
#include "Ser.h"
#include "Dop.h"

#define extern
#include "Pdu.h"
#undef extern
#include "Cmd.h"

#ifndef HDW_PRIMELOG_PLUS

#define PDU_DIG_MESSAGING	(DIG_MASK_MESSAGING_ENABLED | DIG_MASK_CHANNEL_ENABLED)

// States:
#define PDU_IDLE				0
#define PDU_WAIT_FOR_FLUSH		1

// local function prototypes
void pdu_fill_buffers(float fvalue, uint8 index_limit);
void pdu_convert_bytes_to_hex_in_message(uint8 * p_start, uint8 * p_finish);
bool pdu_createPDUheader(char * p_number);
bool pdu_createPDUbody(uint8 type);
bool pdu_encode_tenbit_data(bool type_is_f2);
bool pdu_encode_compressed_data(void);
void pdu_increment_file_buffer_pointer(void);
char * pdu_get_next_line(void);
bool pdu_parse_totaliser(void);
bool pdu_extract_data_from_line(float * p_put);
bool pdu_extract_header(PDU_sms_header_type * p_header);
void pdu_create_path_and_filename(RTC_type * p_when);
void pdu_extract_data_from_file(float * p_buffer, bool yesterday, uint8 data_index, uint8 data_index_limit, uint32 target_time_secs, uint32 data_time_secs, uint32 file_interval_secs);
bool pdu_fetch_sms_data(float * p_buffer, RTC_type * p_when);
void pdu_create(uint8 type, char * p_destination);

// local data
const uint8 pdu_channel_code[22] =
{
#ifdef HDW_RS485
	0x81, 0x82, 0x83, 0x84, 						// Doppler channels
	0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,		// normal analogue
	0x89, 0x29, 0x0A, 0x2A,							// derived digital - DS1
#else
	0x01, 0x21, 0x02, 0x22, 						// normal digital
	0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,		// normal analogue
	0x09, 0x29, 0x0A, 0x2A,							// derived digital - not used for waste water
#endif
	0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F		// derived analogue
};

uint8 pdu_state;
uint8 pdu_volume_units_enumerator;		// used in F3

FAR char * pdu_p_message;
FAR char * pdu_p_file;
FAR char   pdu_path_str[32];
FAR char   pdu_filename_str[16];
FAR uint16 pdu_sms_to_send_flags;
FAR uint16 pdu_derived_sms_to_send_flags;
FAR uint16 pdu_block_offset;
FAR uint8  pdu_block;
FAR long   pdu_file_seek_pos;

// memory for extracting data from file system to be sent in an SMS PDU transmission
FAR char  pdu_file_buffer[PDU_FILE_BUFFER_SIZE];		// buffer for reading raw data from a file
FAR char  pdu_line_buffer[PDU_LINE_BUFFER_SIZE];		// buffer for holding a line from a file
FAR float pdu_input_buffer[PDU_FDATA_BUFFER_SIZE];		// buffer for floating point data to be coded 10 bit or flow compressed
FAR float pdu_analogue_buffer[PDU_FDATA_BUFFER_SIZE];	// buffer for floating point analogue data to be coded pressure compressed
//FAR uint8 pdu_byte_buffer[PDU_BYTE_BUFFER_SIZE];		// temp buffer for parts of outgoing message in byte form
FAR char  pdu_hex_buffer[PDU_HEX_BUFFER_SIZE];			// buffer for outgoing message in hex form
FAR uint8 pdu_channel;									// the channel we are reporting
FAR uint32 pdu_time_stamp;								// time stamp of first logged data we are reporting, less logging interval, in seconds since 00:00:00,1/1/00
FAR RTC_type pdu_rtc_time_stamp;						// time stamp of first logged data we are reporting, less logging interval, in RTC_type format
FAR PDU_sms_header_type pdu_sms_header;					// holds parsed channel and timestamp and data of the valid file header
FAR PDU_sms_header_type pdu_new_sms_header;				// holds parsed channel and timestamp and data of a new file header
FAR uint8 pdu_totaliser[5];								// holds totaliser from file footer
FAR	short pdu_integer_block[96];						// holds 96 pdu integer values for F2,F3
FAR	uint8 pdu_flow_integer_block[96];					// holds 96 flow integers for combined file
FAR	uint8 pdu_pressure_integer_block[96];				// holds 96 pressure integers for combined file

/********************************************************************
 * local functions
 *******************************************************************/
/********************************************************************
 * Function:        void pdu_fill_buffers(float fvalue, uint8 index_limit)
 *
 * PreCondition:    None
 *
 * Input:           float value to be placed in buffers, extent of fill
 *
 * Output:          none
 *
 * Side Effects:    None
 *
 * Overview:        fills pdu_input_buffer and pdu_analogue_buffer with floating point value
 *					up to given limit
 *
 * Note:            None
 *******************************************************************/
void pdu_fill_buffers(float fvalue, uint8 index_limit)
{
	uint8  index = 0;
	do
	{
		pdu_input_buffer[index] = fvalue;
		pdu_analogue_buffer[index++] = fvalue;
	}
	while (index < index_limit);
}

/********************************************************************
 * Function:        void pdu_convert_bytes_to_hex_in_message(uint8 * p_start, uint8 * p_finish)
 *
 * PreCondition:    None
 *
 * Input:           start pointer and end pointer to bytes to be converted
 *
 * Output:          none
 *
 * Side Effects:    None
 *
 * Overview:        converts all bytes from p_start to p_finish - 1
 *					to hexadecimal characters in pdu hex message buffer
 *                  incrementing hex message pointer as it does so
 *					leaves hex message pointer at start of free message space
 *
 * Note:            None
 *******************************************************************/
void pdu_convert_bytes_to_hex_in_message(uint8 * p_start, uint8 * p_finish)
{
	uint8 b;
	char  c;

	// double check we will finish
	if (p_finish > p_start)
	{
		do
		{
			b = *p_start++;
			c = (char)(b >> 4);
			c += (c < 10) ? '0' : ('A' - 10);
			*pdu_p_message++ = c;
			c = (char)(b & 0x0f);
			c += (c < 10) ? '0' : ('A' - 10);
			*pdu_p_message++ = c;
		}
		while (p_start < p_finish);
	}
}

/********************************************************************
 * Function:        bool pdu_createPDUheader(uint8 * Number)
 *
 * PreCondition:    None
 *
 * Input:           pointer to destination telephone number
 *
 * Output:          false if header is too long (> 24 bytes)
 *
 * Side Effects:    None
 *
 * Overview:        enters PDU header into pdu output buffer
 *					leaves message pointer to start of user data space
 *
 * Note:            None
 *******************************************************************/
bool pdu_createPDUheader(char * p_number)
{
	uint8 * p_build_bytes = (uint8 *)STR_buffer;
//	uint8 * p_build_bytes = pdu_byte_buffer;
	uint8 * p_convert_bytes = (uint8 *)STR_buffer;
//	uint8 * p_convert_bytes = pdu_byte_buffer;
	uint8 ii;
	bool result = false;

	ii = *p_number;	// will need this below to determine PDU Address code
	if (ii == '+')	// international code
		p_number++;	// just get the number

	*p_build_bytes++ = 0x00;
	*p_build_bytes++ = TSYNC_pdu_parameter();
	*p_build_bytes++ = 0x00;
	*p_build_bytes++ = strlen(p_number);

	// set up PDU address definition byte
	if (ii == '+')	// international code
		*p_build_bytes++ = (uint8)0x91;
	else
		*p_build_bytes++ = (uint8)PDU_ADDRESS_TYPE;

	// Pack nibble-reversed phone no. into buffer
	ii = 0;
	while (*p_number != '\0')
	{
		if ((ii & 0x01) == 0x00)
			*p_build_bytes = *p_number - '0';
		else
			*p_build_bytes++ |= (*p_number - '0') << 4;

		p_number++;
		ii++;
	}
	if ((ii & 0x01) != 0x00)
		*p_build_bytes++ |= 0xF0;

	*p_build_bytes++ = 0x00;					// TP_PID protocol identifier
	*p_build_bytes++ = 0x04;					// SMS data coding scheme (8 bit)
	*p_build_bytes++ = 0xb4;					// message validity time
	*p_build_bytes++ = PDU_USER_DATA_LENGTH;	// data length in octets

	if ((p_build_bytes - p_convert_bytes) <= PDU_MAX_HEADER_BYTES)
	{
		result = true;

		// convert built bytes to hex in hex message buffer
		pdu_convert_bytes_to_hex_in_message(p_convert_bytes, p_build_bytes);
	}

	return result;
}

/********************************************************************
 * Function:        void pdu_createPDUbody(uint8 type)
 *
 * PreCondition:    None
 *
 * Input:           Type of PDU to create
 *
 * Output:          bool false if type not recognised
 *
 * Side Effects:    None
 *
 * Overview:
 *
 * Note:            creates required data body of 140 bytes
 *
 *******************************************************************/
bool pdu_createPDUbody(uint8 type)
{
	bool result = true;

	// choose type of pdu
	if (type == 0xf2)
		pdu_encode_tenbit_data(true);
	else if (type == 0xf3)
		pdu_encode_tenbit_data(false);
	else if (type == 0xfc)
		pdu_encode_compressed_data();
	else
		result = false;

	return result;
}

/********************************************************************
 * Function:        bool pdu_encode_tenbit_data(bool type_is_f2)
 *
 * PreCondition:    None
 *
 * Input:           type of tenbit sms tx  - true is 0xf2 or false is 0xf3
 *
 * Output:          true/false on success
 *					failure if no valid values found
 *
 * Side Effects:    None
 *
 * Overview:
 *
 * Note:            encodes data with 10 bit coding
 *
 *******************************************************************/
bool pdu_encode_tenbit_data(bool type_is_f2)
{
	uint8 * p_build_bytes = (uint8 *)STR_buffer;
	uint8 * p_convert_bytes = (uint8 *)STR_buffer;
//	uint8 * p_build_bytes = pdu_byte_buffer;
//	uint8 * p_convert_bytes = pdu_byte_buffer;
	float fvalue, fmin, fmax, fdiff, fscaling;
	short a;
	uint8 b;
	int index = 0;
	bool success = false;

	// ***************************************************** //
	// get difference
	fmin = 0;
	fmax = 0;
	// find minimum and maximum data
	do
	{
		fvalue = pdu_input_buffer[index];
		if (fvalue != FLT_MAX)
		{
			success = true;
			if (index == 0)
			{
				fmin = fvalue;
				fmax = fvalue;
			}
			else
			{
				if (fvalue < fmin)
					fmin = fvalue;

				if (fvalue > fmax)
					fmax = fvalue;
			}
		}
	}
	while (++index < 96);

	fdiff = fmax - fmin;

	// ***************************************************** //
	// calculate integers
	index = 0;

	if (fdiff != 0)
	{
		// get scaling factor
		fscaling = 1022/fdiff;
		// now process data into integers
		do
		{
			fvalue = pdu_input_buffer[index];
			if (fvalue == FLT_MAX)
				pdu_integer_block[index] = PDU_TENBIT_NO_DATA_VALUE;
			else
				pdu_integer_block[index] = (short)((fvalue - fmin) * fscaling);
		}
		while (++index < 96);
	}
	else
	{
		// potential divide by zero - set values to zero
		do
		{
			fvalue = pdu_input_buffer[index];
			if (fvalue == FLT_MAX)
				pdu_integer_block[index] = PDU_TENBIT_NO_DATA_VALUE;
			else
				pdu_integer_block[index] = 0;
		}
		while (++index < 96);
	}

	// ***************************************************** //
	// encode binary
	// store 12 byte header
	*p_build_bytes++ = type_is_f2 ? 0xf2 : 0xf3;					// message type code f3
	*p_build_bytes++ = pdu_channel_code[pdu_channel];				// channel code from working channel number (0 - 21)
	// store time stamp little endian
	memcpy(p_build_bytes, &pdu_time_stamp, 4);						// time stamp from precalculated time stamp
	p_build_bytes += 4;

	if (type_is_f2)
	{
		*p_build_bytes++ = pdu_sms_header.data.data_interval;		// interval enum from dig or ana config
		*p_build_bytes++ = pdu_sms_header.data.description_index;	// channel description enum from working channel config structure
		*p_build_bytes++ = pdu_sms_header.data.units_index;			// units enum from working working channel config structure
		*p_build_bytes++ = 0xff;									// unused
		*p_build_bytes++ = 0xff;									// unused
		*p_build_bytes++ = 0xff;									// unused
	}
	else	// message type F3
	{
		// interval enum from dig or ana config, plus volume units enumerator
		*p_build_bytes++ = (pdu_sms_header.data.data_interval & 0x0F) | (pdu_volume_units_enumerator << 4);
		
		// enter five byte totaliser, plus flow units enumeration
		pdu_totaliser[4] &= 0x03;									// mask first
		pdu_totaliser[4] |= (uint8)(pdu_sms_header.data.units_index << 2);
		memcpy(p_build_bytes, pdu_totaliser, 5);					// total from precalculated totaliser
		p_build_bytes += 5;
	}

	// store fmin little endian
	memcpy(p_build_bytes, &fmin, 4);

	p_build_bytes += 4;
	// store fmax little endian
	memcpy(p_build_bytes, &fmax, 4);

	p_build_bytes += 4;
	// store 96 10 bit values in 120 bytes big endian as per spec
	index = 0;
	do
	{
		a = pdu_integer_block[index++] & 0x3ff;				// get integer 0
		b = (uint8)((a & 0x03fc)>> 2);
		*p_build_bytes = b;								// write byte 0
		p_build_bytes++;
		b = (uint8)((a & 0x0003) << 6);
		a = pdu_integer_block[index++] & 0x3ff;				// get integer 1
		b |= (uint8)((a & 0x03f0) >> 4);
		*p_build_bytes = b;								// write byte 1
		p_build_bytes++;
		b = (uint8)((a & 0x000f) << 4);
		a = pdu_integer_block[index++] & 0x3ff;				// get integer 2
		b |= (uint8)((a & 0x03c0) >> 6);
		*p_build_bytes = b;								// write byte 2
		p_build_bytes++;
		b = (uint8)((a & 0x003f) << 2);
		a = pdu_integer_block[index++] & 0x3ff;				// get integer 3
		b |= (uint8)((a & 0x0300) >> 8);
		*p_build_bytes = b;								// write byte 3
		p_build_bytes++;
		b = (uint8)(a & 0x00ff);
		*p_build_bytes = b;								// write byte 4
		p_build_bytes++;
	}
	while (index < 96);

	// convert built bytes to hex in hex message buffer
	pdu_convert_bytes_to_hex_in_message(p_convert_bytes, p_build_bytes);

	return success;
}

/********************************************************************
 * Function:        bool pdu_encode_compressed_data(void)
 *
 * PreCondition:    None
 *
 * Input:           none
 *
 * Output:          true/false on success
 *					failure if no valid values found
 *
 * Side Effects:    None
 *
 * Overview:
 *
 * Note:            encodes data with compressed coding
 *
 *******************************************************************/
bool pdu_encode_compressed_data(void)
{
	uint8 * p_build_bytes = (uint8 *)STR_buffer;
	uint8 * p_convert_bytes = (uint8 *)STR_buffer;
//	uint8 * p_build_bytes = pdu_byte_buffer;
//	uint8 * p_convert_bytes = pdu_byte_buffer;
	float flow_fmin, flow_fmax, flow_fdiff;
	float pressure_fmin, pressure_fmax, pressure_fdiff;
	short flow_min_mantissa, flow_max_mantissa;
	short pressure_min_mantissa, pressure_max_mantissa;
	uint8 flow_exponent;
	uint8 pressure_exponent;

	// calc variables
	long value;
	float fmin, fmax, fscaling, fdiff;
	float fdivisor, frounding, fvalue;
	uint8 exponent, multiplier;
	short short_a;
	uint8 uint8_a, b;
	int index = 0;
	bool success = false;

	// ***************************************************** //
	// calculate flow difference
	// also checks for out of range data and clamps it to maximum or minimum
	flow_fmin = 0;
	flow_fmax = 0;
	// find minimum and maximum data
	do
	{
		fvalue = pdu_input_buffer[index];
		if (fvalue != FLT_MAX)
		{
			success = true;
			// check for out of range and correct
			if (fvalue > PDU_FLOW_MAX)
			{
				fvalue = PDU_FLOW_MAX;
				pdu_input_buffer[index] = fvalue;
			}
			else if (fvalue < PDU_FLOW_MIN)
			{
				fvalue = PDU_FLOW_MIN;
				pdu_input_buffer[index] = fvalue;
			}

			if (index == 0)
			{
				flow_fmin = fvalue;
				flow_fmax = fvalue;
			}
			else
			{
				if (fvalue < flow_fmin)
					flow_fmin = fvalue;
				if (fvalue > flow_fmax)
					flow_fmax = fvalue;
			}
		}
	}
	while (++index < 96);

	// get difference
	flow_fdiff = flow_fmax - flow_fmin;

	// ***************************************************** //
	// calculate pressure difference
	pressure_fmin = 0;
	pressure_fmax = 0;
	index = 0;
	// find minimum and maximum data
	do
	{
		fvalue = pdu_analogue_buffer[index];
		if (fvalue != FLT_MAX)
		{
			success = true;
			// check for out of range and correct
			if (fvalue > PDU_PRESSURE_MAX)
			{
				fvalue = PDU_PRESSURE_MAX;
				pdu_analogue_buffer[index] = fvalue;
			}
			else if (fvalue < PDU_PRESSURE_MIN)
			{
				fvalue = PDU_PRESSURE_MIN;
				pdu_analogue_buffer[index] = fvalue;
			}

			if (index == 0)
			{
				pressure_fmin = fvalue;
				pressure_fmax = fvalue;
			}
			else
			{
				if (fvalue < pressure_fmin)
					pressure_fmin = fvalue;
				if (fvalue > pressure_fmax)
					pressure_fmax = fvalue;
			}
		}
	}
	while (++index < 96);

	// get difference
	pressure_fdiff = pressure_fmax - pressure_fmin;

	// ***************************************************** //
	// calculate flow integers
	// calculate flow mantissas and exponent
	// ensure rounding takes place on all conversions from float to integer
	value = 0;
	fmin = flow_fmin * 512;
	fmax = flow_fmax * 512;

	value = (flow_fmax > -flow_fmin) ? (long)((flow_fmax * 512) + 0.5) : (long)((flow_fmin * -512) + 0.5);
	flow_exponent = 0;
	while ((value >= 0x0400) && (flow_exponent < 8))
	{
		value >>= 1;
		fmax /= 2;
		fmin /= 2;
		flow_exponent++;
	}
	// convert to integers and round by 1 if neccessary to preserve difference
	// this ensures recalculated mantissas always span a greater range than the data
	flow_min_mantissa = (fmin >= 0) ? (short)fmin : (short)(fmin - 1);
	flow_max_mantissa = (fmax >= 0) ? (short)(fmax + 1) : (short)fmax;

	// recalculate floating point values from integer values
	//this gives us the values the receiving machine will use
	exponent = flow_exponent;
	multiplier = 1;
	while (exponent > 0)
	{
		multiplier <<= 1;
		exponent--;
	}
	fscaling = (float)multiplier / (float)512;
	fmin = (float)flow_min_mantissa * fscaling;
	fmax = (float)flow_max_mantissa * fscaling;
	fdiff = fmax - fmin;

	// convert floating point values to integers with rounding
	index = 0;

	if ((long)((fdiff * 512) + 0.5) != 0)
	{
		// get divisor scaling factor
		fdivisor = fdiff/126;
		// get rounding factor
		frounding = fdivisor/2;
		// now process data into integers
		do
		{
			fvalue = pdu_input_buffer[index];
			if (fvalue == FLT_MAX)
				pdu_flow_integer_block[index] = PDU_C_FLOW_NO_DATA_VALUE;
			else if (fvalue >= 0)
				pdu_flow_integer_block[index] = (uint8)((fvalue - fmin + frounding) / fdivisor);
			else
				pdu_flow_integer_block[index] = (uint8)((fvalue - fmin - frounding) / fdivisor);
		}
		while (++index < 96);
	}
	else
	{
		// potential divide by zero - set values to zero
		do
		{
			fvalue = pdu_input_buffer[index];
			if (fvalue == FLT_MAX)
				pdu_flow_integer_block[index] = PDU_C_FLOW_NO_DATA_VALUE;
			else
				pdu_flow_integer_block[index] = 0;
		}
		while (++index < 96);
	}

	// ***************************************************** //
	// calculate pressure integers

	// calculate pressure mantissas and exponent
	// ensure rounding takes place on all conversions from float to integer
	// pressure is always positive
	fmin = pressure_fmin * 128;
	fmax = pressure_fmax * 128;
	value = (long)(fmax + 0.5);
	pressure_exponent = 0;
	while ((value >= 0x0100) && (pressure_exponent < 8))
	{
		value >>= 1;
		fmax /= 2;
		fmin /= 2;
		pressure_exponent++;
	}
	// convert to integers and round up max by 1
	// this ensures recalculated mantissas always span a greater range than the data
	pressure_min_mantissa = (short)fmin;
	pressure_max_mantissa = (short)(fmax + 1);

	// recalculate floating point values from integer values
	//this gives us the values the receiving machine will use
	exponent = pressure_exponent;
	multiplier = 1;
	while (exponent > 0)
	{
		multiplier <<= 1;
		exponent--;
	}

	fscaling = (float)multiplier / (float)128;
	fmin = (float)pressure_min_mantissa * fscaling;
	fmax = (float)pressure_max_mantissa * fscaling;
	fdiff = fmax - fmin;

	// convert floating point values to integers with rounding
	index = 0;

	if ((long)((fdiff * 128) + 0.5) != 0)
	{
		// get divisor scaling factor
		fdivisor = fdiff/14;
		frounding = fdivisor/2;
		// now process data into integers
		do
		{
			fvalue = pdu_analogue_buffer[index];
			if (fvalue == FLT_MAX)
				pdu_pressure_integer_block[index] = PDU_C_PRES_NO_DATA_VALUE;
			else
				pdu_pressure_integer_block[index] = (uint8)((fvalue - fmin + frounding) / fdivisor);
		}
		while (++index < 96);
	}
	else
	{
		// potential divide by zero - set values to zero
		do
		{
			fvalue = pdu_analogue_buffer[index];
			if (fvalue == FLT_MAX)
				pdu_pressure_integer_block[index] = PDU_C_PRES_NO_DATA_VALUE;
			else
				pdu_pressure_integer_block[index] = 0;
		}
		while (++index < 96);
	}

	// ***************************************************** //
	// encode binary

	// store 8 byte header
	index = 0;

	// store type 0xFC and timestamp
	// store type and top two bits of day
	// get time stamp days in binary from bcd
	uint8_a = ((pdu_rtc_time_stamp.day_bcd & 0xF0) >> 4) * 10;
	uint8_a += (pdu_rtc_time_stamp.day_bcd & 0x0F);
	b = (uint8_a & 0x18) >> 3;
	*p_build_bytes = 0xfc | b;						// write byte 1
	p_build_bytes++;
	b = (uint8_a & 0x07) << 5;
	uint8_a = ((pdu_rtc_time_stamp.mth_bcd & 0xF0) >> 4) * 10;
	uint8_a += (pdu_rtc_time_stamp.mth_bcd & 0x0F);
	b |= ((uint8_a & 0x0F) << 1);
	uint8_a = ((pdu_rtc_time_stamp.hr_bcd & 0xF0) >> 4) * 10;
	uint8_a += (pdu_rtc_time_stamp.hr_bcd & 0x0F);
	b |= ((uint8_a & 0x10) >> 4);
	*p_build_bytes = b;								// write byte 2
	p_build_bytes++;
	b = (uint8_a & 0x0F) << 4;
	short_a = flow_min_mantissa & 0x07ff;			// get minimum flow mantissa
	b |= (uint8)((short_a & 0x0780) >> 7);
	*p_build_bytes = b;								// write byte 3
	p_build_bytes++;
	b = (uint8)((short_a & 0x007f) << 1);
	short_a = flow_max_mantissa & 0x07ff;			// get maximum flow mantissa
	b |= (uint8)((short_a & 0x0400) >> 10);
	*p_build_bytes = b;								// write byte 4
	p_build_bytes++;
	b = (uint8)((short_a & 0x03fc) >> 2);
	*p_build_bytes = b;								// write byte 5
	p_build_bytes++;
	b = (uint8)((short_a & 0x0003) << 6);
	uint8_a = flow_exponent & 0x07;					// get flow exponent
	b |= uint8_a << 3;
	short_a = pressure_min_mantissa & 0x00ff;		// get minimum pressure mantissa
	b |= (uint8)((short_a & 0x00e0) >> 5);
	*p_build_bytes = b;								// write byte 6
	p_build_bytes++;
	b = (uint8)((short_a & 0x001f) << 3);
	short_a = pressure_max_mantissa & 0x00ff;		// get maximum pressure mantissa
	b |= (uint8)((short_a & 0x00e0) >> 5);
	*p_build_bytes = b;								// write byte 7
	p_build_bytes++;
	b = (uint8)((short_a & 0x001f) << 3);
	uint8_a = pressure_exponent & 0x07;				// get pressure exponent
	b |= uint8_a;
	*p_build_bytes = b;								// write byte 8
	p_build_bytes++;

	// write 96 7-bit flow values into 84 bytes
	do
	{
		uint8_a = pdu_flow_integer_block[index++] & 0x7f;		// get flow 0 + 8*n
		b = uint8_a << 1;
		uint8_a = pdu_flow_integer_block[index++] & 0x7f;		// get flow 1 + 8*n
		b |= (uint8_a & 0x40) >> 6;
		*p_build_bytes = b;									// write byte 9 + 7*n
		p_build_bytes++;
		b = (uint8_a & 0x3f) << 2;
		uint8_a = pdu_flow_integer_block[index++] & 0x7f;		// get flow 2 + 8*n
		b |= (uint8_a & 0x60) >> 5;
		*p_build_bytes = b;									// write byte 10 + 7*n
		p_build_bytes++;
		b = (uint8_a & 0x1f) << 3;
		uint8_a = pdu_flow_integer_block[index++] & 0x7f;		// get flow 3 + 8*n
		b |= (uint8_a & 0x70) >> 4;
		*p_build_bytes = b;									// write byte 11 + 7*n
		p_build_bytes++;
		b = (uint8_a & 0x0f) << 4;
		uint8_a = pdu_flow_integer_block[index++] & 0x7f;		// get flow 4 + 8*n
		b |= (uint8_a & 0x78) >> 3;
		*p_build_bytes = b;									// write byte 12 + 7*n
		p_build_bytes++;
		b = (uint8_a & 0x07) << 5;
		uint8_a = pdu_flow_integer_block[index++] & 0x7f;		// get flow 5 + 8*n
		b |= (uint8_a & 0x7c) >> 2;
		*p_build_bytes = b;									// write byte 13 + 7*n
		p_build_bytes++;
		b = (uint8_a & 0x03) << 6;
		uint8_a = pdu_flow_integer_block[index++] & 0x7f;		// get flow 6 + 8*n
		b |= (uint8_a & 0x7e) >> 1;
		*p_build_bytes = b;									// write byte 14 + 7*n
		p_build_bytes++;
		b = (uint8_a & 0x01) << 7;
		uint8_a = pdu_flow_integer_block[index++] & 0x7f;		// get flow 7 + 8*n
		b |= uint8_a;
		*p_build_bytes = b;									// write byte 15 + 7*n
		p_build_bytes++;
	}
	while (index < 96);

	// write 96 4-bit pressure values into 48 bytes
	index = 0;
	do
	{
		uint8_a = pdu_pressure_integer_block[index++] & 0x0f;	// get pressure 0 + 2*n
		b = uint8_a << 4;
		uint8_a = pdu_pressure_integer_block[index++] & 0x0f;	// get pressure 1 + 2*n
		b |= uint8_a;
		*p_build_bytes = b;									// write byte 91 + 2*n
		p_build_bytes++;
	}
	while (index < 96);

	// convert built bytes to hex in hex message buffer
	pdu_convert_bytes_to_hex_in_message(p_convert_bytes, p_build_bytes);

	return success;
}

/********************************************************************
 * Function:        void pdu_increment_file_buffer_pointer(void)
 *
 * PreCondition:    None
 *
 * Input:           none
 *
 * Output:          none
 *
 * Side Effects:    None
 *
 * Overview:        increments file buffer pointer
 *					if runs off end of block buffer, gets next block from file into block buffer and resets pointers
 *
 * Note:
 *
 *******************************************************************/
void pdu_increment_file_buffer_pointer(void)
{
	pdu_p_file++;
	pdu_block_offset++;
	if (pdu_block_offset >= PDU_FILE_BUFFER_SIZE)
	{
		// reset pointers
		pdu_block++;
		pdu_block_offset = 0;
		pdu_p_file = pdu_file_buffer;
		// get next block of file
		CFS_read_block(pdu_path_str, pdu_filename_str, pdu_file_buffer, pdu_file_seek_pos, PDU_FILE_BUFFER_SIZE);
		pdu_file_seek_pos += PDU_FILE_BUFFER_SIZE;
	}
}

/********************************************************************
 * Function:        bool  pdu_skip_empty_file_lines(void)
 *
 * PreCondition:    None
 *
 * Input:           none
 *
 * Output:          false if end of file found before finding non empty line
 *
 * Side Effects:    None
 *
 * Overview:        skips empty lines in the file 
 *					will call down new blocks of file until done
 *
 *******************************************************************/

bool pdu_skip_empty_file_lines(void)
{
	while ((*pdu_p_file == '\r') || (*pdu_p_file == '\n')) 
	{
		pdu_increment_file_buffer_pointer();
	};
	// if found end of file before valid data
	if (*pdu_p_file == '\0') 
		return false;
	// else
	return true;
}

/********************************************************************
 * Function:        char * pdu_get_next_line(void)
 *
 * PreCondition:    None
 *
 * Input:           none
 *
 * Output:          pointer to last character of line in line buffer
 *					null if end of file ('\0') char found before newline
 *
 * Side Effects:    None
 *
 * Overview:        gets next non-blank line from file buffer into line buffer
 *
 * Note:            a line goes from present block pointer to the next carriage return or until line buffer is full (line is truncated)
 *                  block pointer is left at first character of next line
 *
 *******************************************************************/

char * pdu_get_next_line(void)
{
	char * p_line = pdu_line_buffer;
	uint8  line_offset = 0;
	char   the_char;

	// skip cr or lf (skip empty line(s))
	if (pdu_skip_empty_file_lines() == false)
		return '\0';
	// else extract line with data in it
	do
	{
		the_char = *pdu_p_file;
		*p_line = the_char;
		p_line++;
		line_offset++;
		pdu_increment_file_buffer_pointer();
	}
	while ((the_char != '\n') && (the_char != '\0') && (line_offset < (PDU_LINE_BUFFER_SIZE - 1)));
	// terminate line
	*p_line = '\0';
	// if found end of file before newline
	if (the_char == '\0') 
		return 0;
	// else
	// if line buffer full before newline
	if (line_offset >= PDU_LINE_BUFFER_SIZE)
	{
		// move file pointer to next line
		do
		{
			the_char = *pdu_p_file;
			pdu_increment_file_buffer_pointer();
		}
		while((the_char != '\n') && (the_char != '\0'));
	}

	// if found end of file before newline - this last line did not have a cr/lf
	if (the_char == '\0') 
		return 0;
	// else
	return p_line;
}


/********************************************************************
 * Function:        pdu_parse_totaliser(void)
 *
 * PreCondition:    None
 *
 * Input:           none
 *
 * Output:          none
 *
 * Side Effects:    None
 *
 * Overview:        extract totaliser from field 5 of a file footer, if it exists
 *
 * Note:            converts up to ten digit ascii string into binary in pdu_totaliser array
 *
 *******************************************************************/
bool pdu_parse_totaliser(void)
{
	int i;
	long long int i64;
	char  input_char;
	char * p_line;
	char * p_line_end;

	// get the next valid footer from the file block
	p_line = pdu_line_buffer;
	do
	{
		p_line_end = pdu_get_next_line();
	} while ((p_line_end != NULL) && (*p_line != '*'));
	
	if (p_line_end == NULL)
		return false;											// cannae find it

	// skip four commas
	i = 0;
	while (i < 4)
	{
		input_char = *p_line++;
		if (input_char == ',')
			i++;
		else if ((input_char == '>') || (input_char == '\n') || (input_char == '\0'))
			break;
	}
	
	if (i != 4)
		return false;											// cannae do it
	// else:

	// p now points to first character after fourth comma.
	// NB this relies on totaliser being terminated by end-of-string or white space (\r, \n etc)
	if (sscanf(p_line, "%lld,%d", &i64, &i) < 1)
		return false;											// cannae do it

	// get result in pdu_totaliser as 34-bit integer, and in pdu_volume_units_enumerator as 4-bits
	pdu_volume_units_enumerator = i & 0x0F;
	i64 &= 0x03FFFFFFFFLL;
	memcpy(pdu_totaliser, &i64, sizeof(pdu_totaliser));
	return true;
}

/********************************************************************
 * Function:        bool pdu_extract_data_from_line(float * p_put)
 *
 * PreCondition:    None
 *
 * Input:           pointer to place for data
 *
 * Output:          true if successful, false if fault
 *
 * Side Effects:    None
 *
 * Overview:        extracts long values from string in line buffer
 *					and places as scaled floating point values in target buffer
 *
 * Note:
 *******************************************************************/
bool pdu_extract_data_from_line(float * p_put)
{
	char * p_line;
	char * p_line_end;
	float  file_value;

	// get the line from the file block
	p_line = pdu_line_buffer;
	p_line_end = pdu_get_next_line();
	// check finish condition
	if (*p_line == '\0')
		return false;

	// convert contents of line
	if (p_line_end == 0)								// empty line
		file_value = FLT_MAX;							// no data
	else if (sscanf(p_line, "%f", &file_value) != 1)	// not numeric data
		file_value = FLT_MAX;							// no data

	*p_put = file_value;

	return true;
}

/********************************************************************
 * Function:        bool pdu_extract_header(PDU_sms_header_type * p_header)
 *
 * PreCondition:    None
 *
 * Input:           none
 *
 * Output:          bool - false if no header or invalid header
 *
 * Side Effects:    None
 *
 * Overview:        extracts and parses 1 line header into config structures
 *					from the file buffer and line buffer
 *
 * Note:
 *
 *******************************************************************/
bool pdu_extract_header(PDU_sms_header_type * p_header)
{
	char * p_line;
	char * p_line_end;
	uint8  i;
	unsigned int v;

	// get first line of header into line buffer
	p_line = pdu_line_buffer;
	p_line_end = pdu_get_next_line();
	if (p_line_end == 0) 
		return false;

	// parse line 1 - channel, date and time of first reading
	// must start with '!'
	if (*p_line != '!') 
		return false;
	// skip '!'
	p_line++;
	if (CMD_parse_rsd(&(p_header->timestamp), p_line, p_line_end) != CMD_ERR_NONE) 
		return false;

	// extract data interval from header after third comma
	// skip 3 commas
	i = 0;
	while((i < 3) && (*p_line != '\0'))
	{
		if (*p_line == ',') i++;
		p_line++;
	};
	if (*p_line == '\0') 
		return false;			// trap short or faulty header
	// extract data interval
	if (sscanf(p_line, "%u", &v) != 1) 
		return false;
	// check for range
	if ((v == 0) || (v > 24)) 
		return false;
	p_header->data.data_interval = (uint8)v;

	// extract channel description index from header after next comma
	// skip a comma
	while((*p_line != ',') && (*p_line != '\0'))
	{
		p_line++;
	};
	if (*p_line == '\0') 
		return false;			// trap short or faulty header
	// skip ','
	p_line++;
	// extract channel description index
	if (sscanf(p_line, "%u", &v) != 1) 
		return false;
	p_header->data.description_index = (uint8)v;

	// extract units description index from header after next comma
	// skip a comma
	while((*p_line != ',') && (*p_line != '\0'))
	{
		p_line++;
	};
	if (*p_line == '\0') 
		return false;			// trap short or faulty header
	// skip ','
	p_line++;
	// extract units description index
	if (sscanf(p_line, "%u", &v) != 1) 
		return false;
	p_header->data.units_index = (uint8)v;

	// extract sms message type from header after next comma
	// skip a comma
	while((*p_line != ',') && (*p_line != '\0'))
	{
		p_line++;
	};
	if (*p_line == '\0') 
		return false;			// trap short or faulty header
	// skip ','
	p_line++;
	// extract sms message type
	if (sscanf(p_line, "%x", &v) != 1) 
		return false;
	if ((v & 0xff00) != 0x0000) 
		return false;
	p_header->data.message_type = (uint8)(v & 0x00ff);

	return true;
}

/********************************************************************
 * Function:        void pdu_create_path_and_filename(RTC_type * p_when)
 *
 * PreCondition:    None
 *
 * Input:           pointer to time+date structure
 *
 * Output:          none
 *
 * Side Effects:    None
 *
 * Overview:        creates path in pdu_path_str and filename in pdu_filename_str
 *					initialises file extraction pointers
 *
 * Note:
 *
 *******************************************************************/
void pdu_create_path_and_filename(RTC_type * p_when)
{
	// create path of required file
	strcpy(pdu_path_str, "\\SMSDATA\\");
	strcat(pdu_path_str, &LOG_channel_id[pdu_channel+1][0]);
	sprintf(STR_buffer, "\\20%02x\\%02x\\", p_when->yr_bcd, p_when->mth_bcd);
	strcat(pdu_path_str, STR_buffer);

	// create filename of required file
	sprintf(pdu_filename_str, "%s-%02x%02x.TXT", &LOG_channel_id[pdu_channel+1][0], p_when->day_bcd, p_when->mth_bcd);

	// set up file extraction pointers
	pdu_p_file = pdu_file_buffer;
	pdu_block_offset = 0;
	pdu_block = 0;
}

/********************************************************************
 * Function:        bool pdu_fetch_sms_data(float * p_buffer, RTC_type * p_when)
 *
 * PreCondition:    None
 *
 * Input:           pointer to destination buffer of floating point numbers
 *					pointer to time/date structure
 *
 * Output:          none
 *
 * Side Effects:    None
 *
 * Overview:        extracts the required sms data into the destination buffer from 1 or 2 files from current pdu channel
 *					for an rsd or sms transmission
 *
 * Note:
 *					the required time is the time the retrieved sms report file would have been transmitted
 *					the data required can be in two logged SMS files unless 00:00 is specifically requested
 *					if the #rsd time field was empty the time has already been defaulted to the current SMS transmission time
 *
 *					high level design between long comments 
 *
 *******************************************************************/
bool pdu_fetch_sms_data(float * p_buffer, RTC_type * p_when)
{
	uint32 file_interval_secs = 0;
	uint32 target_time_secs = 0;
	uint32 y_target_time_secs = 0;
	uint32 data_time_secs = 0;
	uint32 sms_time_secs = 0;
	long   temp_time_secs;
	uint32 sms_data_length_secs;
	bool   have_data = false;
	bool   look_for_next_header = false;
	bool   need_yesterday = true;	/* need yesterday's file (default until told different) */
	bool   first_line_of_file = true;
	bool   finished = false;
	uint8  data_index;
	uint8  y_data_index_limit = 0;
	char   peek;
	RTC_type yesterday;

	// fill totaliser with "no value" value
	data_index = 0;
	do
	{
		pdu_totaliser[data_index++] = 0xff;
	} while (data_index < 4);
	pdu_totaliser[4] = 0x03;

/*****************************************************************************************
	calculate sms time from Date and Time	
*****************************************************************************************/
	// calculate our required sms transmission time from the timestamp in the commanded retrieval data
	sms_time_secs = RTC_bcd_time_to_sec(p_when->hr_bcd, p_when->min_bcd, p_when->sec_bcd);
/*****************************************************************************************
	Find "today's" file using Channel and Date
*****************************************************************************************/
	// create path and filename of file
	pdu_create_path_and_filename(p_when);
	// there must be a file for the requested date as we need to extract the start time and interval
	// check we have a directory and file for the required channel
	if (CFS_file_exists(pdu_path_str, pdu_filename_str))
	{
		// get first block of file
		pdu_file_seek_pos = 0;
		if (!CFS_read_block(pdu_path_str, pdu_filename_str, pdu_file_buffer, pdu_file_seek_pos, PDU_FILE_BUFFER_SIZE))
			 return false;
		pdu_file_seek_pos += PDU_FILE_BUFFER_SIZE;
/*****************************************************************************************
		do
		{
			If next line is a header
			{	
*****************************************************************************************/
		do
		{
			// skip empty lines in file buffer
			if (!pdu_skip_empty_file_lines())					// found end of file
				break;
			peek = *pdu_p_file;
			if (peek == '!')
			{
/*****************************************************************************************
				if not first line of file
				{
					do NOT need yesterday's file
				}
*****************************************************************************************/
				if (!first_line_of_file)
					need_yesterday = false;
/*****************************************************************************************
				Extract header and save all parameters in the valid header structure
*****************************************************************************************/
				// extract header from file block
				if (pdu_extract_header(&pdu_sms_header) == false)
					return false;
/*****************************************************************************************
				data time = header time plus one interval
*****************************************************************************************/
				// can now calculate time of the first reading following the header
				// calculate the file interval
				file_interval_secs = LOG_interval_sec[pdu_sms_header.data.data_interval];
				data_time_secs = RTC_bcd_time_to_sec(pdu_sms_header.timestamp.when.hr_bcd, 
													 pdu_sms_header.timestamp.when.min_bcd, 
													 pdu_sms_header.timestamp.when.sec_bcd) + file_interval_secs;
/*****************************************************************************************
				if data wholly within today's file (i.e. sms_time > 95 * interval)
				{
					data index = 0
					target time = sms time - (95 * interval)
					do NOT need yesterday's file
				}
				else data overlaps into yesterday
				{
					calculate yesterday's target and limits
					data_index = yesterday's data limit plus 1
					target time = interval
					set all previous data to NO VALUE up to data index
				}
			}
*****************************************************************************************/
				// calculate the length of the sms data in seconds
				sms_data_length_secs = 95L * file_interval_secs;
				// if sms transmission time is greater than the length
				if (sms_time_secs > sms_data_length_secs)
				{
					// set data index for today's file start to zero as it is all in today's file
					data_index = 0;
					// calculate our target first reading time in seconds after 00:00
					target_time_secs = sms_time_secs - sms_data_length_secs;
					need_yesterday = false;
				}
				else
				{
					// calculate target time for yesterday
					y_target_time_secs = RTC_SEC_PER_DAY - (sms_data_length_secs - sms_time_secs);
					// calculate data indices for yesterday's limit and today's file start
					y_data_index_limit = (uint8)((sms_data_length_secs - sms_time_secs)/file_interval_secs) + 1;
					data_index = y_data_index_limit;
					// target first reading time in seconds after 00:00 is one interval
					target_time_secs = file_interval_secs;
				}
			}
/*****************************************************************************************
			else if first line of file
			{
				finish - this file has no header
			}
*****************************************************************************************/
			else if (first_line_of_file)
				finished = true;
/*****************************************************************************************
			else
			{
				align and extract data and place in extracted data at data index++
			}
*****************************************************************************************/
			else
			{
				// work out whether we want it
/*****************************************************************************************
				if target time is equal to data time or within 1 interval in advance of it
				{
					extract data into buffer at data index
					increment index and times
					if now past midnight
					{
						parse totaliser if footer exists - finish
					}
				}
*****************************************************************************************/
				// that is: target time is equal to data time or within 1 interval in advance of it
				temp_time_secs = (long)target_time_secs - (long)data_time_secs;
				if ((temp_time_secs >= 0) && (temp_time_secs < (long)file_interval_secs))
				{
					// we want this data
					if (!pdu_extract_data_from_line(&p_buffer[data_index]))			// no more data
						finished = true;

					have_data = true;
					// increment stuff to get next target time etc.
					// do not adjust for midnight
					data_time_secs += file_interval_secs;
					target_time_secs += file_interval_secs;
					// if now past midnight
					if (data_time_secs > RTC_SEC_PER_DAY)
					{
						// parse next totaliser if footer exists
						pdu_parse_totaliser();
						// always finish after totaliser
						finished = true;
					}
					data_index++;
					// if done last extract, finish
					if (data_index >= PDU_FDATA_BUFFER_SIZE) 
						finished = true;
				}
/*****************************************************************************************
				else if data time > target time
				{
					advance target leaving gap in data
					set buffer to no data at data index
					increment index and target time
				}
*****************************************************************************************/
				// else if data time > target time need to advance target leaving gaps in the data until we reach data time
				else if (temp_time_secs < 0)
				{
					target_time_secs += file_interval_secs;
					// set this data to no value
					p_buffer[data_index] = FLT_MAX;
					data_index++;
					// if done last increment, finish
					if (data_index >= PDU_FDATA_BUFFER_SIZE) 
						finished = true;
				}
/*****************************************************************************************
				else data time < target time
				{
					advance through file
					get next line
					increment data time
				}
*****************************************************************************************/
				// else if target time > (data time + interval) need to advance through data until we reach target time
				else
				{
					if (pdu_get_next_line() == '\0')
						finished = true;
					data_time_secs += file_interval_secs;
				}
			}
/*****************************************************************************************
		}
		until reach limits or finish
	}
*****************************************************************************************/
			// now we have looked at first line of file
			first_line_of_file = false;
		}
		while (!finished);
	}
/*****************************************************************************************
	else have no file for today but there may be data yesterday
	{
		use current settings for yesterday's file
		calculate index, limit and times
		finish if data invalid
*****************************************************************************************/
	else	// do not have file today
	{
		// try for yesterday's file using current settings if they exist
		if (pdu_channel < PDU_AN0_CHANNEL_NUM)
		{
#ifndef HDW_RS485
			pdu_sms_header.data.data_interval = DIG_config[pdu_channel / 2].sms_data_interval;
			pdu_sms_header.data.message_type = DIG_config[pdu_channel / 2].sms_message_type; 
			pdu_sms_header.data.description_index = DIG_config[pdu_channel / 2].description_index; 
			pdu_sms_header.data.units_index = DIG_config[pdu_channel / 2].units_index; 
#endif
		}
#ifndef HDW_GPS
		else if (pdu_channel < PDU_NUM_NORMAL_CHANNELS)
		{
			pdu_sms_header.data.data_interval = ANA_config[pdu_channel - PDU_AN0_CHANNEL_NUM].sms_data_interval;
			pdu_sms_header.data.message_type = ANA_config[pdu_channel - PDU_AN0_CHANNEL_NUM].sms_message_type; 
			pdu_sms_header.data.description_index = ANA_config[pdu_channel - PDU_AN0_CHANNEL_NUM].description_index; 
			pdu_sms_header.data.units_index = ANA_config[pdu_channel - PDU_AN0_CHANNEL_NUM].units_index; 
		}
		else if (pdu_channel >= PDU_DERIVED_AN0_CHANNEL_NUM)
		{
			pdu_sms_header.data.data_interval = ANA_config[pdu_channel - PDU_NUM_NORMAL_CHANNELS].sms_data_interval;
			pdu_sms_header.data.message_type = ANA_config[pdu_channel - PDU_NUM_NORMAL_CHANNELS].derived_sms_message_type; 
			pdu_sms_header.data.description_index = ANA_config[pdu_channel - PDU_NUM_NORMAL_CHANNELS].derived_description_index; 
			pdu_sms_header.data.units_index = ANA_config[pdu_channel - PDU_NUM_NORMAL_CHANNELS].derived_units_index; 
		}
#endif
		if (pdu_sms_header.data.data_interval == 0)			// no point continuing
			return false;

		if ((pdu_sms_header.data.message_type != 0xF2) && (pdu_sms_header.data.message_type != 0xF3) && (pdu_sms_header.data.message_type != 0xFC))
			return false;

/*****************************************************************************************
		if data theoretically extends into yesterday
		{
			need yesterday's file
		}
	}
*****************************************************************************************/
		// calculate the file interval
		file_interval_secs = LOG_interval_sec[pdu_sms_header.data.data_interval];
		// calculate the length of the sms data in seconds
		sms_data_length_secs = 95L * file_interval_secs;
		// check that required data theoretically extends into yesterday
		if (sms_data_length_secs >= sms_time_secs)
		{
			// calculate y_target_time_secs and y_data_index_limit
			// calculate target time for yesterday
			y_target_time_secs = RTC_SEC_PER_DAY - (sms_data_length_secs - sms_time_secs);
			// calculate data indices for yesterday's limit
			y_data_index_limit = (uint8)((sms_data_length_secs - sms_time_secs)/file_interval_secs) + 1;
		}
		else					// no point continuing as all data is from today, for which we have no file
			return false;
	}

/*****************************************************************************************
	if we need yesterday's file
	{
*****************************************************************************************/
	if (need_yesterday == false)
		return true;

/*****************************************************************************************
	if find yesterday's file
	{
		data index = 0
*****************************************************************************************/
	first_line_of_file = true;
	// calculate path and filename for yesterday's file
	yesterday.day_bcd = p_when->day_bcd;
	yesterday.mth_bcd = p_when->mth_bcd;
	yesterday.yr_bcd = p_when->yr_bcd;
	// if there is no yesterday, return have_file_today
	if (RTC_get_previous_day(&yesterday) == false) return have_data;

	// create path and filename using new time/date in yesterday
	pdu_create_path_and_filename(&yesterday);
	// check we have a directory and file for the required channel
	// if we don't return have_file_today
	if (CFS_file_exists(pdu_path_str, pdu_filename_str) == false) return have_data;

	// get first block of file - if any faults with this file return have_file_today
	pdu_file_seek_pos = 0;
	if (CFS_read_block(pdu_path_str, pdu_filename_str, pdu_file_buffer, pdu_file_seek_pos, PDU_FILE_BUFFER_SIZE) == false) return have_data;
	pdu_file_seek_pos += PDU_FILE_BUFFER_SIZE;

	// set data index for yesterday's file start to zero as we are gathering the first part of the data
	data_index = 0;

/*****************************************************************************************
	do
	{
		if next line is a header
		{
*****************************************************************************************/
	finished = false;
	do
	{
		// skip empty lines in file buffer
		if (!pdu_skip_empty_file_lines())				// found end of file
			break;

		peek = *pdu_p_file;
		if (peek == '!')
		{
/*****************************************************************************************
			Extract header and save all parameters in a new header structure
*****************************************************************************************/
			// extract header from file block into new header data
			if (!pdu_extract_header(&pdu_new_sms_header))
				return have_data;

/*****************************************************************************************
			if  new header data matches valid header data
			{
				Calculate data time using Start Time and Interval
			}
			else
			{
				set "look for next header" flag
			}
		}
*****************************************************************************************/
			if (memcmp(&(pdu_sms_header.data), &(pdu_new_sms_header.data), sizeof(PDU_sms_header_data_type)) == 0)
			{
				// can now calculate time of the first reading following the header
				data_time_secs = RTC_bcd_time_to_sec(pdu_new_sms_header.timestamp.when.hr_bcd, 
													 pdu_new_sms_header.timestamp.when.min_bcd, 
													 pdu_new_sms_header.timestamp.when.sec_bcd) + file_interval_secs;
				look_for_next_header = false;
			}
			else
				look_for_next_header = true;
		}
/*****************************************************************************************
		else if first line of file
		{
			finish - this file has no header
		}
*****************************************************************************************/
		else if (first_line_of_file)		// go no further - this file has no header, but we may have data from today
			return have_data;
/*****************************************************************************************
		else if looking for next header
		{
			get next line
		}
*****************************************************************************************/
		else if (look_for_next_header)
		{
			if (pdu_get_next_line() == '\0')
				return have_data;
		}
/*****************************************************************************************
		else
		{
			align and extract data and place in extracted data at data index++
		}
*****************************************************************************************/
		else
		{
			// work out whether we want it
/*****************************************************************************************
			if target time is equal to data time or within 1 interval in advance of it
			{
				extract data into buffer at data index
				increment index and times
				if now past midnight
				{
					parse totaliser if footer exists - finish
				}
			}
*****************************************************************************************/
			// that is: target time is equal to data time or within 1 interval in advance of it
			temp_time_secs = (long)y_target_time_secs - (long)data_time_secs;
			if ((temp_time_secs >= 0) && (temp_time_secs < (long)file_interval_secs))
			{
				// we want this data
				if (pdu_extract_data_from_line(&p_buffer[data_index]) == false)
				{
					// no more data
					finished = true;
				}
				have_data = true;
				// increment stuff to get next target time etc.
				// do not adjust for midnight
				data_time_secs += file_interval_secs;
				y_target_time_secs += file_interval_secs;
				// if now past midnight
				if (data_time_secs > RTC_SEC_PER_DAY)
				{
					// parse next totaliser if footer exists
					pdu_parse_totaliser();
					// always finish after totaliser
					finished = true;
				}
				data_index++;
				// if done last extract, finish
				if (data_index >= PDU_FDATA_BUFFER_SIZE) 
					finished = true;
				// if done last increment, finish
				if (data_index >= y_data_index_limit) 
					finished = true;
			}
/*****************************************************************************************
			else if data time > target time
			{
				advance target leaving gap in data
				set buffer to no data at data index
				increment index and target time
			}
*****************************************************************************************/
			// else if data time > target time need to advance target leaving gaps in the data until we reach data time
			else if (temp_time_secs < 0)
			{
				y_target_time_secs += file_interval_secs;
				// set this data to no value
				p_buffer[data_index] = FLT_MAX;
				data_index++;
				// if done last increment, finish
				if (data_index >= y_data_index_limit) 
					return have_data;
			}
/*****************************************************************************************
			else data time < target time
			{
				advance through file
				get next line
				increment data time
			}
*****************************************************************************************/
			// else if target time > (data time + interval) need to advance through data until we reach target time
			else
			{
				if (pdu_get_next_line() == '\0')
					return have_data;
				data_time_secs += file_interval_secs;
			}
		}
/*****************************************************************************************
	}
	until reach limits or finish
}
*****************************************************************************************/
		// now we have looked at first line of file
		first_line_of_file = false;
	}
	while (!finished);	

	return have_data;
}

/********************************************************************
 * Function:        void pdu_create(uint8 type, char * p_destination)
 *
 * PreCondition:    None
 *
 * Input:           type of PDU - 0xf2, 0xf3 for tenbit, 0xfc for compressed
 *					pointer to destination phone number
 *
 * Output:          None
 *
 * Side Effects:    None
 *
 * Overview:        create a PDU message from floating point buffers and place it in the modem queue
 *
 * Note:            None
 *******************************************************************/
void pdu_create(uint8 type, char * p_destination)
{
	pdu_p_message = pdu_hex_buffer;
	if (pdu_createPDUheader(p_destination))
	{
		if (pdu_createPDUbody(type))
		{
			// add null
			*pdu_p_message++ = 0;
			// Flush message to outbox immediately, so outbox buffer can be re-used for reply
			MSG_send(MSG_TYPE_SMS_PDU, pdu_hex_buffer, p_destination);
			// do not initiate tx if in batch mode
			MSG_flush_outbox_buffer(!COM_schedule.batch_enable);
		}
	}
}

#endif

/********************************************************************
 * public functions
 *******************************************************************/

/******************************************************************************
** Function:	PDU schedule
**
** Notes:		schedule an sms to be transmitted for a particular channel (0 - 10)
**				or for a particular derived data channel (11 - 21)
*/
void PDU_schedule(uint8 channel)
{
#ifndef HDW_PRIMELOG_PLUS
	if (channel < PDU_NUM_NORMAL_CHANNELS)													// normal data - Channels 0-10:
		pdu_sms_to_send_flags |= (1 << channel) & _16BIT(_B00000111, _B11111111);
	else if (channel < PDU_NUM_SMSR_CHANNELS)												// derived data - Channels 11-21:
		pdu_derived_sms_to_send_flags |= (1 << (channel - 11)) & _16BIT(_B00000111, _B11111111);
#endif
}

/******************************************************************************
** Function:	is it time for a batch message
**
** Parameter:	channel: 0 - 3 digital, 4 - 10 analogue, 15 - 21 analogue derived
**                       0 - 1 doppler values, 11 derived doppler value 
**
** Return:		returns true if current enqueue time synchronises with a tx window start
**				or is in partial interval before
**
** Notes:		only if batch mode is enabled and there is a valid tx window
**              must check that sms type combinations are correct
**				called when an sms value is enqueued by dig, ana or dop
*/
bool PDU_time_for_batch(uint8 channel)
{
#ifndef HDW_PRIMELOG_PLUS

	uint32 elapsed_secs, window_start_sec, interval_sec, result;
	uint8  type;
#ifndef HDW_RS485
	uint8  d_chan, d_sub;
#endif
	bool   enabled = false;

	if (channel >= PDU_NUM_SMSR_CHANNELS)
		return enabled;

	if (!COM_schedule.ftp_enable && COM_schedule.batch_enable && (COM_schedule.tx_window.interval > 0))	// if batch mode and valid tx window
	{
		if (channel < PDU_AN0_CHANNEL_NUM)																// if a digital channel
		{
#ifndef HDW_RS485
			d_chan = channel / 2;
			d_sub = channel % 2;
																										// if event value logging sub channel
			if (((DIG_config[d_chan].sensor_type & DIG_EVENT_MASK) != 0x00) && 
				(DIG_config[d_chan].ec[d_sub].sensor_type != 0))
			{																							// get setup from config for sub channel
				type = DIG_config[d_chan].ec[d_sub].sms_message_type;									// only send allowed types and combination of types
				enabled = ((type == 0xF2) || (type == 0xF3));											// allow only channel type F2 or F3
				if ((DIG_config[d_chan].ec[d_sub].flags & DIG_MASK_MESSAGING_ENABLED) == 0)
					enabled = false;
				interval_sec = LOG_interval_sec[DIG_config[d_chan].ec[d_sub].sms_data_interval];
			}
			else																						// else
			{																							// get setup from main channel config
				type = DIG_config[d_chan].sms_message_type;												// only send allowed types and combination of types
				enabled = ((type == 0xF2) || (type == 0xF3));											// allow any channel type F2 or F3
#ifndef HDW_GPS
				if ((!enabled) && (d_chan == 0) && (type == 0xFC))										// only allow compressed if digital channel 1 
					enabled = (ANA_config[0].sms_message_type == 0xFC);									// and analogue channel 1 are both FC
#endif
				if ((DIG_config[d_chan].flags & DIG_MASK_MESSAGING_ENABLED) == 0)
					enabled = false;
				interval_sec = LOG_interval_sec[DIG_config[d_chan].sms_data_interval];
			}
#else																									// doppler serial sensor channels
/*
			if (channel == 0)
			{
				type = DOP_config.velocity_sms_message_type;
				enabled = ((type == 0xF2) || (type == 0xF3));											// allow any channel type F2 or F3
				if ((DOP_config.flags & DOP_MASK_VELOCITY_MESSAGING_ENABLED) == 0)						// check sms flag
					enabled = false;
			}
			else if (channel == 1)
			{
				type = DOP_config.temperature_sms_message_type;
				enabled = ((type == 0xF2) || (type == 0xF3));											// allow any channel type F2 or F3
				if ((DOP_config.flags & DOP_MASK_TEMPERATURE_MESSAGING_ENABLED) == 0)					// check sms flag
					enabled = false;
			}
			else if (channel == 2)
			{
				type = DOP_config.depth_sms_message_type;
				enabled = ((type == 0xF2) || (type == 0xF3));											// allow any channel type F2 or F3
				if ((DOP_config.flags & DOP_MASK_DEPTH_MESSAGING_ENABLED) == 0)							// check sms flag
					enabled = false;
			}
			interval_sec = LOG_interval_sec[DOP_config.sms_data_interval];
*/
#endif
		}
		else if ((channel < PDU_NUM_NORMAL_CHANNELS) || (channel >= PDU_DERIVED_AN0_CHANNEL_NUM))		// an analogue channel
		{
#ifndef HDW_GPS
			if (channel < PDU_NUM_NORMAL_CHANNELS)														// only send allowed types and combination of types
			{																							// normal data - channel 1 may compress with digital 1
				type = ANA_config[channel - PDU_AN0_CHANNEL_NUM].sms_message_type;
				enabled = ((type == 0xF2) || (type == 0xF3));											// allow any channel type F2 or F3
  #ifndef HDW_RS485
				if (enabled && ((channel - PDU_AN0_CHANNEL_NUM) == 0))									// except if channel 1, and digital channel 1 type is compressed
					enabled = (DIG_config[0].sms_message_type != 0xFC);
				if ((ANA_config[channel - PDU_AN0_CHANNEL_NUM].flags & ANA_MASK_MESSAGING_ENABLED) == 0)
					enabled = false;																	// check this data's messaging enabled flag
  #endif
			}
			else
			{																							// derived data
				type = ANA_config[channel - PDU_DERIVED_AN0_CHANNEL_NUM].derived_sms_message_type;
				enabled = ((type == 0xF2) || (type == 0xF3));											// allow any channel type F2 or F3
				if ((ANA_config[channel - PDU_DERIVED_AN0_CHANNEL_NUM].flags & ANA_MASK_DERIVED_MESSAGING_ENABLED) == 0)
					enabled = false;																	// check this data's messaging enabled flag
			}
			interval_sec = LOG_interval_sec[ANA_config[channel - 4].sms_data_interval];
#endif
		}
#ifdef HDW_RS485																						// doppler serial sensor derived channel
		else if (channel == 11)
		{
			/*
			type = DOP_config.flow_sms_message_type;
			enabled = ((type == 0xF2) || (type == 0xF3));												// allow any channel type F2 or F3
			if ((DOP_config.flags & DOP_MASK_DERIVED_FLOW_MESSAGING_ENABLED) == 0)						// check sms flag
				enabled = false;
			interval_sec = LOG_interval_sec[DOP_config.sms_data_interval];
			*/
		}
#endif
		if (!enabled)
			return false;

		window_start_sec =																				// else ok to calculate if synchronised with tx window start
			RTC_bcd_time_to_sec(COM_schedule.tx_window.start.hr_bcd, COM_schedule.tx_window.start.min_bcd, 0);

		if (RTC_time_sec < window_start_sec)															// get time in sec since start of window
			elapsed_secs = (RTC_SEC_PER_DAY + RTC_time_sec) - window_start_sec;
		else
			elapsed_secs = RTC_time_sec - window_start_sec;
																										// intervals since start of window is (elapsed_secs / interval_sec)
		result = (elapsed_secs / interval_sec) % PDU_LOG_INTERVALS;										// result = (intervals mod 96)
		if (elapsed_secs % interval_sec != 0)															// if partial interval before window start
		{
			result++;																					// add 1 so that sms batch count would reach 96 
			if (result >= 96) 																			// at reading immediately before or at window start
				result = 0;
		}
		if (result == 0)																				// return true if synchronises
			return true;
	}
#endif
	// else
	return false;
}

/********************************************************************
 * Function:        void PDU_schedule_all(void)
 *
 * PreCondition:    None
 *
 * Input:           None
 *
 * Output:          None
 *
 * Side Effects:    None
 *
 * Overview:        schedule SMS transmission for all configured channels
 *
 * Note:            only called by COM_schedule_control when not in batch mode
 *******************************************************************/
void PDU_schedule_all(void)
{
#ifndef HDW_PRIMELOG_PLUS
	uint8 i;
	uint8 c;
	bool  enabled;

#ifndef HDW_RS485
	DIG_config_type * p_d_config;
#endif
#ifndef HDW_GPS
	ANA_config_type * p_a_config;
#endif
	LOG_flush();

#ifndef HDW_RS485
	for (i = 0; i < CAL_build_info.num_digital_channels; i++)							// for each digital channel
	{
		p_d_config = &DIG_config[i];
		if ((p_d_config->sensor_type & DIG_EVENT_A_MASK) > DIG_EVENT_A_NONE)			// event sensor subchannel A
		{
			if (p_d_config->ec[0].sensor_type != 0)										// if event value logging channel A
			{
				if ((p_d_config->ec[0].flags & PDU_DIG_MESSAGING) == PDU_DIG_MESSAGING)
					PDU_schedule(i * 2);												// schedule sms for event logging sub channel A of a digital channel
			}
			if (p_d_config->ec[1].sensor_type != 0)										// if event value logging channel B
			{
				if ((p_d_config->ec[1].flags & PDU_DIG_MESSAGING) == PDU_DIG_MESSAGING)
					PDU_schedule((i * 2) + 1);											// schedule sms for event logging sub channel B of a digital channel
			}
		}
		else																			// else pulse counting channel
		{
			c = p_d_config->sms_message_type;											// only send allowed types and combination of types
			enabled = ((c == 0xF2) || (c == 0xF3));										// allow any channel type F2 or F3
  #ifndef HDW_GPS
			if ((!enabled) && (i == 0) && (c == 0xFC))									// only allow compressed if digital channel 1 and analogue channel 1 are both FC
				enabled = (ANA_config[0].sms_message_type == 0xFC);
  #endif
			if (((p_d_config->sensor_type & 0x07) > 0) &&								// if channel and messaging enabled, and flow transducer, and right kind of SMS:
				((p_d_config->flags & PDU_DIG_MESSAGING) == PDU_DIG_MESSAGING) &&
				enabled)
			{
				PDU_schedule(i * 2);													// schedule sms for sub channel A of a digital channel
				if (((p_d_config->flags & DIG_MASK_COMBINE_SUB_CHANNELS) == 0) &&		// if there is a sub channel B
					((p_d_config->sensor_type & 0x07) >= DIG_TYPE_FWD_A_FWD_B))
					PDU_schedule((i * 2) + 1);											// schedule sms for sub channel B of a digital channel
			}
		}
	}
#else
	/*
	c = DOP_config.velocity_sms_message_type;
	if (((c == 0xF2) || (c == 0xF3)) && ((DOP_config.flags & DOP_MASK_VELOCITY_MESSAGING_ENABLED) != 0))
		PDU_schedule(0);																// if enabled schedule velocity sms
	c = DOP_config.temperature_sms_message_type;
	if (((c == 0xF2) || (c == 0xF3)) && ((DOP_config.flags & DOP_MASK_TEMPERATURE_MESSAGING_ENABLED) != 0))
		PDU_schedule(1);																// if enabled schedule temperature sms
	c = DOP_config.depth_sms_message_type;
	if (((c == 0xF2) || (c == 0xF3)) && ((DOP_config.flags & DOP_MASK_DEPTH_MESSAGING_ENABLED) != 0))
		PDU_schedule(2);																// if enabled schedule depth sms
	c = DOP_config.flow_sms_message_type;
	if (((c == 0xF2) || (c == 0xF3)) && ((DOP_config.flags & DOP_MASK_DERIVED_FLOW_MESSAGING_ENABLED) != 0))
		PDU_schedule(11);																// if enabled schedule derived flow sms
	*/
#endif
#ifndef HDW_GPS
	for (i = 0; i < ANA_NUM_CHANNELS; i++)												// for each analogue channel
	{
		p_a_config = &ANA_config[i];
		if (!ANA_channel_exists(i))														// check channel exists
			return;																		// try normal data sms
		c = p_a_config->sms_message_type;												// only send allowed types and combination of types
		enabled = ((c == 0xF2) || (c == 0xF3));											// allow any channel type F2 or F3
  #ifndef HDW_RS485
		if (enabled && (i == 0))														// except if channel 1, and digital channel 1 type is compressed
			enabled = (DIG_config[0].sms_message_type != 0xFC);
  #endif
																						// if channel & messaging enabled, and SMS but not combined SMS
		if (((p_a_config->flags & (ANA_MASK_CHANNEL_ENABLED | ANA_MASK_MESSAGING_ENABLED)) ==
			 (ANA_MASK_CHANNEL_ENABLED | ANA_MASK_MESSAGING_ENABLED)) &&
			 enabled)
			PDU_schedule(i + PDU_AN0_CHANNEL_NUM);										// schedule sms for an analogue channel
																						// then try derived data sms
		c = p_a_config->derived_sms_message_type;										// only send allowed types and combination of types
		enabled = ((c == 0xF2) || (c == 0xF3));											// allow any channel type F2 or F3
																						// if channel & messaging enabled, and SMS but not combined SMS
		if (((p_a_config->flags & (ANA_MASK_DERIVED_DATA_ENABLED | ANA_MASK_DERIVED_MESSAGING_ENABLED)) ==
			 (ANA_MASK_DERIVED_DATA_ENABLED | ANA_MASK_DERIVED_MESSAGING_ENABLED)) &&
			 enabled)
			PDU_schedule(i + PDU_DERIVED_AN0_CHANNEL_NUM);								// schedule sms for a derived analogue channel
	}
#endif
#endif
}

/********************************************************************
 * Function:        void PDU_test_message(void)
 *
 * PreCondition:    None
 *
 * Input:           pointer to id text for test message, pointer to text phone number
 *
 * Output:          None
 *
 * Side Effects:    None
 *
 * Overview:        Send a variety of PDU test messages defined by an id string
 *
 * Note:            does not create a message if parameters are not recognised
 *					null phone number text string uses host numbers
 *******************************************************************/
void PDU_test_message(char * p_id, char * p_phone_number)
{
#ifndef HDW_PRIMELOG_PLUS
	bool create = false;
	uint8 type, offset;

	if (*p_id == '0')
	{
		type = 0xf2;
		create = true;
	}
	else if (*p_id == '1')
	{
		type = 0xf3;
		create = true;
	}
	else if (*p_id == '2')
	{
		type = 0xfc;
		create = true;
	}

	if (create)
	{
		// set config defaults
		pdu_sms_header.data.data_interval = 14;
		pdu_sms_header.data.message_type = type;
		// set totaliser to no value
		offset = 0;
		do
		{
			pdu_totaliser[offset++] = 0xff;
		} while (offset < 4);

		pdu_totaliser[4] = 0x03;
		// set dummy data in float value buffers
		offset = 0;
		while (offset < PDU_FDATA_BUFFER_SIZE)
		{
			pdu_input_buffer[offset] = (float)offset * 20;
			pdu_analogue_buffer[offset] = (float)offset / 5;
			offset++;
		}

		if ((p_phone_number[0] == '0') && (p_phone_number[1] == '\0'))
		{
			// send to host number(s)
			if (COM_host1[0] != '\0')
				pdu_create(type, COM_host1);
			if (COM_host2[0] != '\0')
				pdu_create(type, COM_host2);
			if (COM_host3[0] != '\0')
				pdu_create(type, COM_host3);
		}
		else if ((*p_phone_number == '0') || (*p_phone_number == '+'))			// send to given number
			pdu_create(type, p_phone_number);
	}
#endif
}

/********************************************************************
 * Function:        uint8 PDU_retrieve_data(bool to_host)
 *
 * PreCondition:    None
 *
 * Input:           flag if message is to be sent to host, or to SMS sender
 *
 * Output:          error code for file system errors
 *
 * Side Effects:    None
 *
 * Overview:        send a pdu message containing data for
 *					a channel logged at a time and date
 *
 * Note:            Channel number and date and time of data is held in
 *					PDU_rsd_retrieve structure
 *					Type, scaling, units etc. per channel are held in the file header
 *					If type is 0xfc, and channel is digital 1,
 *					it will also extract analogue channel 1 into pdu_analogue_buffer
 *					for the generation of PDU type FC compressed data
 *******************************************************************/
uint8 PDU_retrieve_data(bool to_host)
{
#ifndef HDW_PRIMELOG_PLUS
	uint8  length_hr;
	uint8  time_stamp_hr;
	uint8 message_type;
	uint32 interval_sec;

	// set pdu channel number from retrieving channel
	pdu_channel = PDU_rsd_retrieve.channel;

	// limit check
	if (pdu_channel >= PDU_NUM_SMSR_CHANNELS)
		return CMD_ERR_INVALID_CHANNEL_NUMBER;

	// fill buffers with <unlogged> data value
	pdu_fill_buffers(FLT_MAX, PDU_FDATA_BUFFER_SIZE);

	// fetch the data for the given channel for this retrieval
	if (!pdu_fetch_sms_data(pdu_input_buffer, &PDU_rsd_retrieve.when))
 		return CMD_ERR_FILE_READ_LINE_FAILED;

	// check for channel 4 and FC type
	if ((pdu_channel == 4) && (pdu_sms_header.data.message_type == 0xfc))
	{
		// reset pdu_channel to 0 and start again
		pdu_channel = 0;
		// fill buffers with <unlogged> data value
		pdu_fill_buffers(FLT_MAX, PDU_FDATA_BUFFER_SIZE);
		// fetch the data for channel 0
		if (!pdu_fetch_sms_data(pdu_input_buffer, &PDU_rsd_retrieve.when))
	 		return CMD_ERR_FILE_READ_LINE_FAILED;
	}

	// now check for pdu type FC - only applies to digital channel 1 and analogue channel 1
	if ((pdu_sms_header.data.message_type == 0xfc) && (pdu_channel == 0))
	{
		// need to extract analogue channel 1 (channel = 4) data into pdu_analogue_buffer
		// set pdu channel number to 4
		pdu_channel = 4;
		// fetch the data for channel 4
		if (pdu_fetch_sms_data(pdu_analogue_buffer, &PDU_rsd_retrieve.when) == false)
		{
			return CMD_ERR_FILE_READ_LINE_FAILED;
		}
	}

	// calculate time stamps
	memcpy(&pdu_rtc_time_stamp, &PDU_rsd_retrieve.when, sizeof(RTC_type));
	// get interval in seconds
	interval_sec = LOG_interval_sec[pdu_sms_header.data.data_interval];
	// calculate length of file in hours - cannot be more than 24
	length_hr = (uint8)((96 * interval_sec) / 3600);
	if (length_hr > 24) length_hr = 24;
	// subtract length from time stamp
	// get time stamp hours in binary from bcd
	time_stamp_hr = ((pdu_rtc_time_stamp.hr_bcd & 0xF0) >> 4) * 10;
	time_stamp_hr += (pdu_rtc_time_stamp.hr_bcd & 0x0F);
	// compare
	if (length_hr > time_stamp_hr)
	{
		// if need yesterday
		 // set date to yesterday
		RTC_get_previous_day(&pdu_rtc_time_stamp);
		// calculate new hours in binary
		time_stamp_hr += (24 - length_hr);
	}
	else									// calculate new hours in binary
		time_stamp_hr -= length_hr;

	// set new hours in bcd in time stamp
	pdu_rtc_time_stamp.hr_bcd = RTC_bcd_from_value(time_stamp_hr); 

	// get retrieve time in seconds
	pdu_time_stamp = RTC_time_date_to_sec(&PDU_rsd_retrieve.when);
	// calculate exact time of previous reading (in whole intervals from 00:00:00)
	pdu_time_stamp = interval_sec * (pdu_time_stamp / interval_sec);
	// subtract 96 * interval
	pdu_time_stamp -= (96 * interval_sec);

	// send it in an sms pdu message using this channel's type
	message_type = pdu_sms_header.data.message_type;
	if ((message_type == 0xf2) || (message_type == 0xf3) || (message_type == 0xfc))
	{
		if (to_host)
		{
			if (COM_host1[0] != '\0')
				pdu_create(message_type, COM_host1);
			if (COM_host2[0] != '\0')
				pdu_create(message_type, COM_host2);
			if (COM_host3[0] != '\0')
				pdu_create(message_type, COM_host3);
		}
		else if (COM_sms_sender_number[0] != '\0')
			pdu_create(message_type, COM_sms_sender_number);
	}
#endif
	return CMD_ERR_NONE;
}

/******************************************************************************
** Function:	PDU busy
**
** Notes:		
*/
bool PDU_busy(void)
{
#ifndef HDW_PRIMELOG_PLUS
	return (pdu_sms_to_send_flags != 0);
#else
	return false;
#endif
}

/******************************************************************************
** Function:	PDU sms messaging task
**
** Notes:
*/
void PDU_task(void)
{
#ifndef HDW_PRIMELOG_PLUS
	uint16 mask;
	uint8 channel;
	
	switch (pdu_state)
	{
		case PDU_IDLE:
			if ((pdu_sms_to_send_flags != 0x0000) || (pdu_derived_sms_to_send_flags != 0x0000))		// any messages need generating
			{
				if (CFS_open())																// file system ready
				{
#ifndef HDW_RS485
					if (!DIG_busy() 												// wait for dig and ana not busy
#else
					//if (!DOP_busy()
					if(1
#endif
#ifndef HDW_GPS
						&& !ANA_busy()
#endif
					   )
					{
						LOG_flush();																// flush log queues to file and go and wait for completion
						pdu_state = PDU_WAIT_FOR_FLUSH;
					}
				}
			}
			break;

		case PDU_WAIT_FOR_FLUSH:
			if (!LOG_busy())															// waiting for log flush to complete
			{
				if (pdu_sms_to_send_flags != 0x0000)									// if normal sms to send
				{
					mask = 0x0001;														// find lowest normal channel with flag set
					for (channel = 0; channel < 11; channel++)
					{
						if ((pdu_sms_to_send_flags & mask) != 0)
						{
							// set up retrieval data
							PDU_rsd_retrieve.channel = channel;
							// get retrieval time
							PDU_rsd_retrieve.when.yr_bcd = RTC_now.yr_bcd;
							PDU_rsd_retrieve.when.mth_bcd = RTC_now.mth_bcd;
							PDU_rsd_retrieve.when.day_bcd = RTC_now.day_bcd;
							PDU_rsd_retrieve.when.hr_bcd = RTC_now.hr_bcd;
							PDU_rsd_retrieve.when.min_bcd = RTC_now.min_bcd;
							PDU_rsd_retrieve.when.sec_bcd = 0;							// always set at minute boundary
							// retrieve it
							PDU_retrieve_data(true);
							// remove flag
							pdu_sms_to_send_flags ^= mask;
							channel = 11;												// NB need to do this once through per enabled flag
						}
						else
							mask <<= 1;
					}
				
					pdu_sms_to_send_flags &= _16BIT(_B00000111, _B11111111);			// always ensure pdu_sms_to_send_flags has no bits set above max channel
					pdu_state = PDU_IDLE;
				}
				else if (pdu_derived_sms_to_send_flags != 0x0000)						// else if derived sms to send
				{
					mask = 0x0001;														// find lowest derived channel with flag set
					for (channel = 11; channel < 22; channel++)							// although this checks all derived channels digital is not used in waste water
					{
						if ((pdu_derived_sms_to_send_flags & mask) != 0)
						{
							// set up retrieval data
							PDU_rsd_retrieve.channel = channel;
							// get retrieval time
							PDU_rsd_retrieve.when.yr_bcd = RTC_now.yr_bcd;
							PDU_rsd_retrieve.when.mth_bcd = RTC_now.mth_bcd;
							PDU_rsd_retrieve.when.day_bcd = RTC_now.day_bcd;
							PDU_rsd_retrieve.when.hr_bcd = RTC_now.hr_bcd;
							PDU_rsd_retrieve.when.min_bcd = RTC_now.min_bcd;
							PDU_rsd_retrieve.when.sec_bcd = 0;							// always set at minute boundary
							// retrieve it
							PDU_retrieve_data(true);
							// remove flag
							pdu_derived_sms_to_send_flags ^= mask;
							channel = 22;												// NB need to do this once through per enabled flag
						}
						else
							mask <<= 1;
					}
				
					pdu_derived_sms_to_send_flags &= _16BIT(_B00000111, _B11111111);	// always ensure pdu_derived_sms_to_send_flags has no bits set above max channel
					pdu_state = PDU_IDLE;
				}
				else																	// catch all else
					pdu_state = PDU_IDLE;
			}
			break;

		default:
			pdu_state = PDU_IDLE;
			break;
	}
#endif
}

/********************************************************************
 * Function:        void PDU_init(void)
 *
 * PreCondition:    None
 *
 * Input:           None
 *
 * Output:          None
 *
 * Side Effects:    None
 *
 * Overview:        any initialisation required at start up
 *
 * Note:            None
 *******************************************************************/
void PDU_init(void)
{
#ifndef HDW_PRIMELOG_PLUS
	pdu_state = PDU_IDLE;
	pdu_p_message = pdu_hex_buffer;

	// set retrieval defaults
	PDU_rsd_retrieve.channel = 0;
	PDU_rsd_retrieve.when.yr_bcd = 0;
	PDU_rsd_retrieve.when.mth_bcd = 0;
	PDU_rsd_retrieve.when.day_bcd = 0;
	PDU_rsd_retrieve.when.hr_bcd = 0;
	PDU_rsd_retrieve.when.min_bcd = 0;
	PDU_rsd_retrieve.when.sec_bcd = 0;
#endif
}




