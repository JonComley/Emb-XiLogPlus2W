/******************************************************************************
** File:	ftp.c
**			P Boyce
**			9/7/09
**
** Notes:	FTP message coding
**
** V3.12 070612 PB	Waste Water - ensure ftp uses correct messaging enable flag when sending event value logged data
**
** V3.17 091012 PB  bring up to date with Xilog+ V3.06 - use return value from CFS_open()
**
** V3.26 180613 PB  Remove CFS_FAILED_TO_OPEN state 
**
** V3.30 011113 PB  correction to code in ftp_channel_messaging() to preserve ordinary event messaging
**
** V3.35 090114 PB  in FTP_activate_retrieval_info(), set seek pos to zero if not active
**		 100114 PB  don't clear ftp flags and seek pos when start a channel that is already active
**
** V3.36 140114 PB  FTP_reset_active_retrieval_info() to clear ftp flags, makes FTP_deactivate_retrieval_info() redundant
**
** V4.00 220114 PB  if HDW_GPS disable all analogue calls and functions
*/

#include "Custom.h"
#include "Compiler.h"
#include "Str.h"
#include "HardwareProfile.h"
#include "MDD File System/FSIO.h"
#include "Cfs.h"
#include "Msg.h"
#include "usb.h"
#include "rtc.h"
#include "Com.h"
#include "Log.h"
#include "Ana.h"
#include "Dig.h"
#include "Pdu.h"
#include "Cmd.h"
#include "Mdm.h"
#include "Cal.h"
#include "Ser.h"
#include "Dop.h"
#include "modbus.h"

#define extern
#include "ftp.h"
#undef extern

// Local FTP task states:
#define FTP_IDLE			0
#define FTP_TX_RESPONSE		1

uint8  ftp_state;
bool   ftp_to_send;
uint16 ftp_files_present;

FAR char ftp_rx_buffer[162];
FAR char ftp_tx_buffer[162];
FAR unsigned char ftp_input_string[18];
FAR unsigned char ftp_output_string[25];

//*******************************
// private functions
//*******************************

/******************************************************************************
** Function:	Encode 6-bit value into 8-bit ASCII
**
** Notes:		Output range ASCII @ - Z, a - z, / - 9
*/
unsigned char ftp_encode_sextet(unsigned char c6)
{
	if (c6 < 27) return c6 + 64;
	if (c6 < 53) return c6 + 70;
	return c6 - 6;
}

/******************************************************************************
** Function:	Encode 18 bytes into 24 chars
**
** Notes:		s is source array, t is target array
**				takes 3 bytes and encodes into 4 6 bit chars
*/
void ftp_encode_eighteen(unsigned char *s, unsigned char *t)
{ 
	unsigned long value;
	int i = 0;

	do
	{
		value = (unsigned long)*s++;
		i++;
		value |= (unsigned long)*s++ << 8;
		i++;
		value |= (unsigned long)*s++ << 16;
		i++;
		*t++ = ftp_encode_sextet(value & 0x3f);
		value >>= 6;
		*t++ = ftp_encode_sextet(value & 0x3f);
		value >>= 6;
		*t++ = ftp_encode_sextet(value & 0x3f);
		value >>= 6;
		*t++ = ftp_encode_sextet(value & 0x3f);
	}
	while (i < 18);
}

/******************************************************************************
** Function:	Decode 6-bit value from 8-bit ASCII
**
** Notes:		input range ASCII @ - Z, a - z, / - 9
*/
unsigned char ftp_decode_sextet(unsigned char c8)
{
	if (c8 < 47) return 0;
	if (c8 < 58) return c8 + 6;
	if (c8 < 64) return 0;
	if (c8 < 91) return c8 - 64;
	if (c8 < 97) return 0;
	if (c8 < 123) return c8 - 70;
	return 0;
}

/******************************************************************************
** Function:	Decode 24 char encrypted ASCII string into 18 bytes
**
** Notes:		converts each set of 4 chars into 3 bytes
*/
void ftp_decode_eighteen(unsigned char *s, unsigned char *t)
{
	unsigned long value;
	int j = 0;

	do
	{
		value = (unsigned long)(ftp_decode_sextet(*s++));
		value |= (unsigned long)(ftp_decode_sextet(*s++)) << 6;
		value |= (unsigned long)(ftp_decode_sextet(*s++)) << 12;
		value |= (unsigned long)(ftp_decode_sextet(*s++)) << 18;
		t[j++] = (unsigned char)(value & 0xff);
		value >>= 8;
		t[j++] = (unsigned char)(value & 0xff);
		value >>= 8;
		t[j++] = (unsigned char)(value & 0xff);
	}
	while (j < 18);
}

/******************************************************************************
** Function:	return state of an ftp channel's messaging enable flag
**
** Notes:		channels 0 to 4 are serial 1 to 5
**				channels 5 to 10 are analogue channels 1 to 7
**				channels 11 to 14 are digital derived channels not used in Waste Water - return false if not RS485 serial port type
**				channels 15 to 21 are derived analogue channels 1 to 7
*/
bool ftp_channel_messaging(int ftp_channel)
{
#ifndef HDW_RS485
	int d_chan = ftp_channel / 2;
	int d_sub = ftp_channel % 2;
#endif

	char temp[20];
	sprintf(temp, "ftp ch %d", ftp_channel);
	USB_monitor_string(temp);

	if (ftp_channel < FTP_ANA_FTPR_INDEX)													// check normal digitals or serial port
	{
#ifndef HDW_RS485
		if ((DIG_config[d_chan].sensor_type & DIG_EVENT_MASK) != 0x00)						// if event sensor subchannel
		{
			if (DIG_config[d_chan].ec[d_sub].sensor_type != 0)								// if event value logging
				return ((DIG_config[d_chan].ec[d_sub].flags & DIG_MASK_MESSAGING_ENABLED) != 0);
			else																			// else if either channel event logging
				return ((DIG_config[d_chan].flags & DIG_MASK_MESSAGING_ENABLED) != 0);
		}
		else if ((DIG_config[d_chan].sensor_type & DIG_PULSE_MASK) != 0x00)					// if either channel pulse logging
			return ((DIG_config[d_chan].flags & DIG_MASK_MESSAGING_ENABLED) != 0);
#else
		if (MOD_tx_enable && (MOD_config.channel_enable_bits & (1<<ftp_channel)))
			return true;
		/*
		if (ftp_channel == 0)
			return ((DOP_config.flags & DOP_MASK_VELOCITY_MESSAGING_ENABLED) != 0);
		else if (ftp_channel == 1)
			return ((DOP_config.flags & DOP_MASK_TEMPERATURE_MESSAGING_ENABLED) != 0);
		else if (ftp_channel == 2)
			return ((DOP_config.flags & DOP_MASK_DEPTH_MESSAGING_ENABLED) != 0);
		*/
#endif
		else
			return false;
	}
#ifndef HDW_GPS
	else if (ftp_channel < FTP_DERIVED_BASE_INDEX)											// check normal analogues
	{
		return ((ANA_config[ftp_channel - FTP_ANA_FTPR_INDEX].flags & ANA_MASK_MESSAGING_ENABLED) != 0);
	}
#endif
	else if (ftp_channel < FTP_DERIVED_ANA_INDEX)
	{
#ifndef HDW_RS485
		return false;																		// return false for derived digitals
#else
		if (ftp_channel == 11)
			return ((DOP_config.flags & DOP_MASK_DERIVED_FLOW_MESSAGING_ENABLED) != 0);		// return derived flow flag for serial port
		else
			return false;
#endif
	}
#ifndef HDW_GPS
	else if (ftp_channel < FTP_NUM_FTPR_CHANNELS)											// check derived analogues
	{
		return ((ANA_config[ftp_channel - FTP_DERIVED_ANA_INDEX].flags & ANA_MASK_DERIVED_MESSAGING_ENABLED) != 0);
	}
#endif
	else return false;
}

/*********************************************************************************************************
 * Function:        void ftp_report_now(void)
 *
 * PreCondition:    None
 *
 * Input:           None
 *
 * Output:          None
 *
 * Side Effects:    None
 *
 * Overview:        create part data messages to update FTP data from all channels which have logged data
 *					called by FTP_task when it detects ftp_to_send flag set
 *
 * Note:            only need to do this if there are active or inactive channel(s)
 *					with new data to be sent
 *					The data to be sent may overlap from one day to the next
 *					and may bridge several days if there is a gap in the day 
 *					mask for the transmit window e.g. at a weekend
 *
 *					this function must create the messages required 
 * 					to bring the ftp transmissions up to date, one message per day
 *
 *					messages contain timestamp in the form of one date plus a time for each active channel 
 *					encoded in hex strings, e.g.:
 *					"090410 102000 102500" 
 *					in filename position
 *					and active channel flags plus a seek and end pos for each flag in filepath position
 *					encoded in hex strings, e.g.:
 *					"0005 00000000 0000FDE4 00000000 000037CD"
 *					in filepath position
 *
 *					Waste Water - must handle a channel and its derived channel
 *					creates one message for normal data, and a second message for derived data
 *					must run through twice - FTP_retrieve_info has sections for normal and derived data
 *					for waste water there are no derived channels for digital data
 *
 ********************************************************************************************************/
void  ftp_report_now(void)
{
	int     i, tp, tn;
	uint16	mask;
	uint16  files_present;
	uint16  files_today;
	RTC_type msg_timestamp;
	long	file_end_pos;
	int		derived_offset = 0;
	
	do																								// do this for normal and derived
	{
		files_present = 0;
		mask = 0x01;																				// find earliest active timestamp and set up files_present flag
		msg_timestamp.reg32[0] = RTC_now.reg32[0] & 0x00ffffff;										// remove day of week
		msg_timestamp.reg32[1] = RTC_now.reg32[1];
		for (i = derived_offset; i < FTP_NUM_CHANNELS + derived_offset; i++)						// for all possible channels
		{
			if ((FTP_channel_data_retrieve[i].flags == 0x03) && (ftp_channel_messaging(i)))			// if any channels are active, have data to send and messaging enabled
			{
				files_present |= mask;																// find earliest timestamp
				if (RTC_compare(&FTP_channel_data_retrieve[i].seek_time_stamp, &msg_timestamp) == -1)
				{
					msg_timestamp.reg32[0] = FTP_channel_data_retrieve[i].seek_time_stamp.reg32[0];
					msg_timestamp.reg32[1] = FTP_channel_data_retrieve[i].seek_time_stamp.reg32[1];
				}
			}
			mask <<= 1;
		}
		if (files_present != 0)
		{
			while (msg_timestamp.reg32[1] <= RTC_now.reg32[1])										// for each day from earliest timestamp up to and including today
			{
				mask = 0x01;																		// for each file present
				files_today = files_present;
				tp = 105;																			// build seek + end pos and timestamp hex strings in STR_buffer
				STR_buffer[tp] = 0;
				tn = 7;
				STR_buffer[tn] = 0;
				for (i = derived_offset; i < FTP_NUM_CHANNELS + derived_offset; i++)
				{
					if ((files_today & mask) == mask)
					{
						file_end_pos = FTP_find_file(i, &msg_timestamp);
						if (file_end_pos == 0)														// if empty file
						{
							files_today ^= mask;													// remove flag
						}
						else																		// else there is something in the file
						{
							tp += sprintf(&STR_buffer[tp], "%08lx %08lx ", 							// add seek pos and end pos to temp path
										 FTP_channel_data_retrieve[i].seek_pos, 
										 file_end_pos);
							tn += sprintf(&STR_buffer[tn], "%02x%02x%02x ", 						// add seek time stamp hrminsec to temp name
										 FTP_channel_data_retrieve[i].seek_time_stamp.hr_bcd, 
										 FTP_channel_data_retrieve[i].seek_time_stamp.min_bcd, 
										 FTP_channel_data_retrieve[i].seek_time_stamp.sec_bcd); 
						}
						if (msg_timestamp.reg32[1] != RTC_now.reg32[1])								// if not today
						{
							FTP_channel_data_retrieve[i].seek_pos = 0;								// new seek pos and seek time stamp are start of tomorrow's file
							RTC_get_next_day(&FTP_channel_data_retrieve[i].seek_time_stamp);
							FTP_channel_data_retrieve[i].seek_time_stamp.reg32[0] = 0;
						}
						else																		// else is today
						{
							FTP_channel_data_retrieve[i].seek_pos = file_end_pos + 1;				// new seek pos is file end pos + 1
																									// no update to seek time stamp as this will be updated 
																									// when next item is logged
							FTP_channel_data_retrieve[i].flags ^= 0x02;								// clear data to send flag
						}
					}
					mask <<= 1;
				}
				if (files_today != 0)																// if there are files today
				{																					// create FTP PART DATA message
					if (derived_offset != 0)														// if derived pass through
						files_today |= 0x8000;														// set top bit of files flags
					sprintf(&STR_buffer[100], "%04x", files_today);									// fill in flags at start of path
					STR_buffer[104] = 0x20;															// add space
					sprintf(STR_buffer, "%02x%02x%02x", 											// fill in date at start of name
							msg_timestamp.day_bcd, msg_timestamp.mth_bcd, msg_timestamp.yr_bcd);
					STR_buffer[6] = 0x20;															// add space
					MSG_send(MSG_TYPE_FTP_PART_DATA, &STR_buffer[100], STR_buffer);
					MSG_flush_outbox_buffer(true);													// immediate tx
				}
				
				if (!RTC_get_next_day(&msg_timestamp))
					break;

				msg_timestamp.reg32[0] = 0;
			}
		}
		derived_offset += FTP_NUM_CHANNELS;															// set up derived pass
	}
	while (derived_offset < FTP_NUM_FTPR_CHANNELS);													// finish after second (derived) pass
}

//*******************************
// public functions
//*******************************

/********************************************************************
 * Function:        int FTP_encrypt_password(char * password_p)
 *
 * PreCondition:    None
 *
 * Input:           Pointer to password string
 *
 * Output:          length of encrypted string
 *
 * Side Effects:    None
 *
 * Overview:        encrypts up to 16 chars of password into 24 chars
 *
 * Note:            None
 *******************************************************************/
int FTP_encrypt_password(char * password_p)
{
	char * p = password_p;
	int i = 0;
	unsigned char c;
	uint16	random_number;
	uint32	time_sec = RTC_time_sec;

	// get up to 16 bytes of password
	do
	{
		c = (unsigned char)*p++; 
		if (c != '\0') ftp_input_string[i++] = c;
	}
	while ((i < 16) && (c != '\0'));
	// add terminator
	ftp_input_string[i++] = '\0';
	// calculate random seed
	random_number = ((uint16)((time_sec >> 16) ^ time_sec) ^ TMR1) & 0x7fff;
	// seed random function
	srand(random_number);
	// pack string with random bytes
	while (i < 16)
	{
		ftp_input_string[i++] = (unsigned char)(rand() & 0xff);
	};
	// get encryption random byte
	c = (unsigned char)(rand() & 0xff);
	// encrypt 17 bytes in input string
	for (i = 0; i < 17; i++)
	{
		ftp_input_string[i] ^= c;
	}
	// put key into string
	ftp_input_string[17] = c;
	// encode 18 bytes into 24 chars
	ftp_encode_eighteen(ftp_input_string, ftp_output_string);
	ftp_output_string[24] = '\0';
	memcpy(password_p, ftp_output_string, 25);

	return 24;
}

/********************************************************************
 * Function:        bool FTP_get_logon_string(char * logon_string)
 *
 * PreCondition:    None
 *
 * Input:           Pointer to target logon string
 *
 * Output:          false if cannot be found 
 *
 * Side Effects:    None
 *
 * Overview:        extracts true ftp logon string from file memory
 *					and decrypts password from 24 encrypted characters
 *
 * Note:            None
 *******************************************************************/
bool FTP_get_logon_string(char * logon_string)
{
	char * p;
	char * q;
	int i;
	unsigned char c;

	if (!CFS_read_file((char *)CFS_config_path, (char *)FTP_logon_filename, logon_string, 128))
	{
		return false;
	}
	// get encrypted bytes into ftp output string
	// find second comma
	p = strstr(logon_string, ",");
	if (p == NULL) return false;
	p++;
	p = strstr(p, ",");
	if (p == NULL) return false;
	p++;
	// check we have 24 chars in encrypted part
	q = strstr(p, ",");
	if ((q == NULL)  || (((int)q - (int)p) != 24)) return false;
	memcpy(ftp_output_string, p, 24);
	// decode 24 chars into 18 bytes
	ftp_decode_eighteen(ftp_output_string, ftp_input_string);
	// decrypt using last char - this recovers a string with terminating \0
	c = ftp_input_string[17];
	for (i = 0; i < 17; i++)
	{
		ftp_input_string[i] ^= c;
	}
	// reconstitute ftp logon string
	// terminate logon string after second comma
	*p ='\0';
	// point to rest of string
	p+= 24;
	// insert password into logon string
	strcat(logon_string, (char *)ftp_input_string);
	strcat(logon_string, p);
	return true;
}

/********************************************************************
 * Function:        long   FTP_set_filename_and_path(uint8 channel, RTC_type * date_p)
 *
 * PreCondition:    None
 *
 * Input:           ftp channel number 0 - 10, pointer to date of file
 *
 * Output:          none
 *
 * Side Effects:    None
 *
 * Overview:        sets path and filename in global buffers
 *
 * Note:            
 *
 *******************************************************************/
void   FTP_set_filename_and_path(uint8 channel, RTC_type * date_p)
{
	// get path and filename from retrieve data
	// create path of required file
	strcpy(FTP_path_str, "\\LOGDATA\\");
	strcat(FTP_path_str, &LOG_channel_id[channel + 1][0]);
	// use FTP_filename_str temporarily
	sprintf(FTP_filename_str, "\\20%02x\\%02x", date_p->yr_bcd, date_p->mth_bcd);
	strcat(FTP_path_str, FTP_filename_str);
	// create filename of required file
	sprintf(FTP_filename_str, "%s-%02x%02x.TXT", &LOG_channel_id[channel + 1][0], date_p->day_bcd, date_p->mth_bcd);
}

/********************************************************************
 * Function:        long   FTP_find_file(uint8 channel, RTC_type * date_p)
 *
 * PreCondition:    None
 *
 * Input:           ftp channel number 0 - 10, pointer to date of file
 *
 * Output:          end_pos of (no of bytes in) file
 *
 * Side Effects:    None
 *
 * Overview:        find file - if present return end_pos
 *					sets path and filename in global buffers
 *
 * Note:            if return 0 there was a problem with the file, or it is empty
 *
 *******************************************************************/
long   FTP_find_file(uint8 channel, RTC_type * date_p)
{
	SearchRec ftp_srch;

	// get path and filename from retrieve data
	FTP_set_filename_and_path(channel, date_p);

	// if file present
	if ((CFS_state != CFS_OPEN) || (FSchdir(FTP_path_str) != 0))
	{
		return 0;
	}
	else
	{
		ftp_srch.attributes = ATTR_DIRECTORY;							// currently in a directory
		if (FindFirst(FTP_filename_str, ATTR_MASK, &ftp_srch) != 0)
		{
			return 0;
		}
		else
		{
			// set end_pos to file length - 1
			return (ftp_srch.filesize - 1);
		}
	}
}

/********************************************************************
 * Function:        uint16 FTP_flag_normal_files_present(RTC_type * date)
 *
 * PreCondition:    None
 *
 * Input:           pointer to RTC_type for inputting the date required
 *
 * Output:          11 bit binary flags, one for each channel with normal data
 *
 * Side Effects:    None
 *
 * Overview:        creates and reports flag register
 *					flag set if a channel has logged normal data for the given date
 *
 * Note:            This must attempt to recover ANY data present for a particular day
 *					So all possible channels are searched
 *******************************************************************/
uint16 FTP_flag_normal_files_present(RTC_type * date)
{
	int 	channel;
	uint16	mask = 0x01;
	uint16  files_present = 0;
	
	for (channel = 0; channel < FTP_DERIVED_BASE_INDEX; channel++)							// for each ftp channel of normal data
	{
		if (FTP_find_file(channel, date) > 0)
		{
			files_present |= mask;
		}
		mask <<= 1;
	}

	return files_present;
}

/********************************************************************
 * Function:        uint16 FTP_flag_derived_files_present(RTC_type * date)
 *
 * PreCondition:    None
 *
 * Input:           pointer to RTC_type for inputting the date required
 *
 * Output:          11 bit binary flags, one for each channel with derived_data
 *
 * Side Effects:    None
 *
 * Overview:        creates and reports flag register
 *					flag set if a channel has logged derived data for the given date
 *
 * Note:            This must attempt to recover ANY data present for a particular day
 *					So all possible channels are searched
 *******************************************************************/
uint16 FTP_flag_derived_files_present(RTC_type * date)
{
	int 	channel;
	uint16	mask = 0x01;
	uint16  files_present = 0;
	
	for (channel = FTP_DERIVED_BASE_INDEX; channel < FTP_NUM_FTPR_CHANNELS; channel++)		// for each ftp channel of derived data
	{
		if (FTP_find_file(channel, date) > 0)
		{
			files_present |= mask;
		}
		mask <<= 1;
	}

	return files_present;
}

/********************************************************************
 * Function:        void   FTP_update_retrieval_info(uint8 channel, RTC_type * time_stamp)
 *
 * PreCondition:    None
 *
 * Input:           ftp channel number 0 - 10, time stamp of enqueued value
 *
 * Output:          None
 *
 * Side Effects:    None
 *
 * Overview:        called from DIG or ANA when a value is enqueued to be logged
 *					updates last logged timestamp in retrieval data
 *
 * Note:            None
 *******************************************************************/
void   FTP_update_retrieval_info(uint8 channel, RTC_type * time_stamp)
{
	FTP_file_retrieve_type * p;

	p = &FTP_channel_data_retrieve[channel];
	// if no data sent yet
	if ((p->flags & 0x02) == 0)
	{
		// set data to be sent flag
		p->flags |= 0x02;					
		// set time stamp to time stamp of first item
		p->seek_time_stamp.reg32[0] = time_stamp->reg32[0];
		p->seek_time_stamp.reg32[1] = time_stamp->reg32[1];
	}
}

/********************************************************************
 * Function:        void   FTP_activate_retrieval_info(uint8 channel)
 *
 * PreCondition:    None
 *
 * Input:           ftp channel number 0 - 10
 *
 * Output:          None
 *
 * Side Effects:    None
 *
 * Overview:        set active, no data flags and seek position
 *
 * Note:            called from DIG or ANA when a channel is started
 *					seek time stamp is set when first value is logged
 *******************************************************************/
void   FTP_activate_retrieval_info(uint8 channel)
{
	FTP_file_retrieve_type * p;
	//char temp[10];

	p = &FTP_channel_data_retrieve[channel];
	// set seek pos to start of today's file if file is inactive
	if (p->flags == 0x00)
	{
		p->seek_pos = 0;
		// set flags to active, no data saved
		p->flags = 0x01;
	}
	//sprintf(temp, "$%02X", p->flags);
	//USB_monitor_string(temp);
}

/********************************************************************
 * Function:        void   FTP_reset_active_retrieval_info(void)
 *
 * PreCondition:    None
 *
 * Input:           None
 *
 * Output:          None
 *
 * Side Effects:    None
 *
 * Overview:        for all active channels
 *						clear data to send flag
 *						set seek pos to end of new date's file if it exists
 *
 * Note:            called when time is changed and #mps= is received
 *******************************************************************/
void   FTP_reset_active_retrieval_info(void)
{
	FTP_file_retrieve_type * p;
	uint8 channel;

	for (channel = 0; channel < FTP_NUM_FTPR_CHANNELS; channel++)
	{
		p = &FTP_channel_data_retrieve[channel];
		// clear data to send and channel active flags
		p->flags &= 0x00;						
		// set seek pos to end of current file plus 1 if it exists
		p->seek_pos = FTP_find_file(channel, &RTC_now);
		if (p->seek_pos != 0) p->seek_pos++;
	}
}

/********************************************************************
 * Function:        void  FTP_retrieve_data(uint16 file_flags, RTC_type * file_date_p)
 *
 * PreCondition:    None
 *
 * Input:           None
 *
 * Output:          None
 *
 * Side Effects:    None
 *
 * Overview:        sets up a tx message to send all logged data for a given date to the ftp server
 *					Called by cmd_rfd - forces transmission
 *
 * Note:            top bit of flags uint16 is set for derived data
 *******************************************************************/
void  FTP_retrieve_data(uint16 file_flags, RTC_type * file_date_p)
{
	// create FTP ALL DATA message
	sprintf(FTP_path_str, "%04x", file_flags);
	sprintf(FTP_filename_str, "%02x%02x%02x %02x%02x%02x", 
			file_date_p->day_bcd, file_date_p->mth_bcd, file_date_p->yr_bcd,
			file_date_p->hr_bcd, file_date_p->min_bcd, file_date_p->sec_bcd
			);
	MSG_send(MSG_TYPE_FTP_ALL_DATA, FTP_path_str, FTP_filename_str);
	MSG_flush_outbox_buffer(true);							// immediate tx
}

/********************************************************************
 * Function:        uint8 FTP_frd_send(char *path, char *filename)
 *
 * PreCondition:    None
 *
 * Input:           None
 *
 * Output:          CMD error code
 *
 * Side Effects:    None
 *
 * Overview:        sets up a tx message to send a file to the ftp server
 *					Called by cmd_frd or cmd_ftx
 *
 * Note:            None
 *******************************************************************/
uint8 FTP_frd_send(char *path, char *filename)
{
	// if file does not exist
	if (CFS_file_exists(path, filename) == false)
	{
		return CMD_ERR_INVALID_FILENAME;
	}
	// else
	// place message in outbox buffer as a file request
	// this will not be deleted even if ftp is not enabled with COM_schedule.ftp_enable
	// but will be sent
	MSG_send(MSG_TYPE_FTP_FILE, path, filename);
	MSG_flush_outbox_buffer(true);							// immediate tx

	return CMD_ERR_NONE;
}

/********************************************************************
 * Function:        void  FTP_act_on_ftp_command(void)
 *
 * PreCondition:    None
 *
 * Input:           None
 *
 * Output:          none
 *
 * Side Effects:    None
 *
 * Overview:        send line in STR_buffer to command processor
 *					as an ftp command 
 *
 * Note:            None
 *******************************************************************/
void FTP_act_on_ftp_command(void)
{
	char * end_p;

	// double check and ensure is a string
	end_p = strstr(STR_buffer, "\r");
	if (end_p != NULL)
	{
		 *(end_p + 1) = '\0';
		strcpy(ftp_rx_buffer, STR_buffer);
		ftp_tx_buffer[0] = '\0';
		CMD_schedule_parse(CMD_SOURCE_FTP, ftp_rx_buffer, ftp_tx_buffer);
		ftp_state = FTP_TX_RESPONSE;
	}
}

/********************************************************************
 * Function:        uint8 FTP_set_logon(void)
 *
 * PreCondition:    None
 *
 * Input:           string containing ftp logon parameters in STR_buffer
 *
 * Output:          error code if file system fail
 *
 * Side Effects:    None
 *
 * Overview:        write contents of \CONFIG\ftplogon.txt
 *
 * Note:            None
 *******************************************************************/
uint8 FTP_set_logon(void)
{
	// create contents of file
	if (CFS_write_file((char *)CFS_config_path, (char *)FTP_logon_filename, "w", STR_buffer, strlen(STR_buffer)))
		return CMD_ERR_NONE;
	else
		return CMD_ERR_FILE_WRITE_FAILED;
}

/********************************************************************
 * Function:        void FTP_schedule(void)
 *
 * PreCondition:    None
 *
 * Input:           None
 *
 * Output:          None
 *
 * Side Effects:    None
 *
 * Overview:        schedule FTP transmission by setting ftp_to_send flag
 *
 * Note:            needs to flush the log
 *******************************************************************/
void FTP_schedule(void)
{
	LOG_flush();
	ftp_to_send = true;
}

/******************************************************************************
** Function:	FTP busy report
**
** Notes:
*/
bool FTP_busy(void)
{
	return ((ftp_state != FTP_IDLE) || ftp_to_send);
}

/******************************************************************************
** Function:	FTP task
**
** Notes:
*/
void FTP_task(void)
{
	switch (ftp_state)
	{
		case FTP_IDLE:
			if (ftp_to_send)
			{
				if (CFS_open() && !LOG_busy())										// waiting for file system or logging to complete
				{
					ftp_report_now();												// create messages for ftp tx
					ftp_to_send = false;
				}
			}
			break;

		case FTP_TX_RESPONSE:
			if (CMD_busy(CMD_SOURCE_FTP))											// cmd busy
				break;
																					// else cmd finished creating reply in ftp_tx_buffer
																					// so create reply message using name of incoming command
			MSG_send(MSG_TYPE_FTP_MSG, ftp_tx_buffer, COM_ftp_filename);
			MSG_flush_outbox_buffer(true);											// immediate tx
			ftp_state = FTP_IDLE;
			break;

		default:
			ftp_state = FTP_IDLE;
			break;
	}
}

/********************************************************************
 * Function:        void FTP_init(void)
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
void FTP_init(void)
{
	uint8 i;

	ftp_state = FTP_IDLE;
	ftp_to_send = false;
	for (i=0; i<FTP_NUM_FTPR_CHANNELS; i++)
	{
		FTP_channel_data_retrieve[i].flags = 0x00;
		FTP_channel_data_retrieve[i].seek_time_stamp.reg32[0] = 0;
		FTP_channel_data_retrieve[i].seek_time_stamp.reg32[1] = 0;
		FTP_channel_data_retrieve[i].seek_pos = 0;
	}
}

           


