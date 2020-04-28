/******************************************************************************
** File:	tsync.c - time syncronisation	
**
** Notes:
**
** changes
**
** V2.69 270411 PB  remove test code
**
** V2.71 030511 PB  Correction to rules used when "use mins and secs only" flag is set in TSYNC command 
**
** V2.74 240511 PB  DEL145 - Move "TSYNC_CORRECTING_TIME" actions out of state machine and run them in task with a correcting_time flag
**                           independent of state machine, which is now solely for getting a new time via sms or ftp.  
**       250511 PB  DEL143 - new function COM_initiate_gprs_nitz_tsync() called when gprs nitz time sync is required
**
** V3.00 220911 PB  Waste Water - call COP_recalc_wakeups() when time of day changes
**
** V3.31 131113 PB  initialise tsync_day_bcd & tsync_correction_day_bcd in TSYNC_init()
**
** V4.04 010514 PB  GPS - call GPS_recalc_wakeup() when time of day changes
**
*/

#include <string.h>
#include <stdio.h>
#include "Custom.h"
#include "Compiler.h"
#include "MDD File System/SD-SPI.h"
#include "MDD File System/FSIO.h"
#include "HardwareProfile.h"
#include "Str.h"
#include "rtc.h"
//#include "Usb.h"
#include "Msg.h"
#include "Com.h"
#include "Mdm.h"
#include "Cop.h"
#include "gps.h"
#include "Log.h"
#include "alm.h"
#include "Ana.h"
#include "Dig.h"
#include "ftp.h"
//#include "Mag.h"

#define extern
#include "tsync.h"
#undef extern

#define TSYNC_OFF					0
#define TSYNC_IDLE					1
#define TSYNC_GSM_REQUIRED			2
#define TSYNC_AWAITING_GSM_STATUS	3
#define TSYNC_NITZ_REQUIRED			4
#define TSYNC_AWAITING_NITZ_STATUS	5

bool     tsync_correcting_time;
uint16   tsync_interval;
uint16   tsync_remaining;
uint8    tsync_state;
uint8    tsync_protocol;
uint8    tsync_day_bcd;
uint8	 tsync_correction_day_bcd;
uint8    tsync_days;
int8     tsync_cal;
int      tsync_message_id;
uint32   tsync_submit_time_sec;
char *   tsync_input_ptr;

// local functions
/******************************************************************************
** Function:	Parse a 2-digit ASCII value to BCD
**
** Notes:		Because we are receiving expected date and time strings from the modem
**				we can assume data is two character numerals
**				and can accept any termination.
**              exit with tsync_input_ptr pointing to the character after separator. 
*/
uint8 tsync_parse_bcd(void)
{
	uint8 c;

	c = 0;
	c = (*tsync_input_ptr++ - '0') << 4;
	c += *tsync_input_ptr++ - '0';
	tsync_input_ptr++;

	return c;
}

/******************************************************************************
** Function:	convert bcd minutes and seconds to uint32 seconds
**
** Notes:		
*/
uint32 tsync_bcd_min_sec_to_seconds(uint8 min_bcd, uint8 sec_bcd)
{
	uint32 temp;

	temp = ((uint32)(min_bcd >> 4) * 10L) + (uint32)(min_bcd & 0x0F);
	return (temp * 60L) + ((uint32)(sec_bcd >> 4) * 10L) + (uint32)(sec_bcd & 0x0F);
}

// global functions

/******************************************************************************
** Function:	set tsync repeat interval
**
** Notes:		remaining days cannot exceed this
*/
void   TSYNC_set_interval(uint16 interval)
{
	tsync_interval = interval;
	if (tsync_remaining > interval)
		tsync_remaining = interval;
}

/******************************************************************************
** Function:	get tsync repeat interval
**
** Notes:		
*/
uint16  TSYNC_get_interval(void)
{
	return tsync_interval; 
}

/******************************************************************************
** Function:	set remaining days to tsync
**
** Notes:		cannot exceed existing repeat interval
*/
void   TSYNC_set_remaining(uint16 remaining)
{
	if (remaining > tsync_interval)
		remaining = tsync_interval;

	tsync_remaining = remaining;
}

/******************************************************************************
** Function:	get remaining days before tsync
**
** Notes:		
*/
uint16  TSYNC_get_remaining(void)
{
	return tsync_remaining; 
}

/******************************************************************************
** Function:	act on parameters
**
** Notes:		called by reception of valid tsync= command
**              if on and interval > 0
**                  set to idle
**                  if remaining = 0
**                      immediate action is required
**                      set day and step count to trigger on next tx
**              if interval = 0 clear on flag
**              if off stop everything
*/
void   TSYNC_action(void)
{
	if (TSYNC_on)
	{
		if (tsync_interval > 0)
		{
			tsync_state = TSYNC_IDLE;
			// if immediate action is required
			if (tsync_remaining == 0)
			{
				// set tsync_day to be different to today to trigger immediate start
				tsync_day_bcd = 0x32;
			}
		}
		else
			TSYNC_on = false;
	}

	if (!TSYNC_on)
	{
		// stop tsync and any correction
		tsync_correcting_time = false;
		tsync_correction_day_bcd = RTC_now.day_bcd;
		// load zero into lower byte of RFGCAL
		RTC_set_correction(0);
		// switch off
		tsync_state = TSYNC_OFF;
}
}

/******************************************************************************
** Function:	get csmp parameters
**
** Notes:		puts current parameters for AT+CSMP in supplied string buffer
**              depending on tsync_state
**				parameter 1: 17 - no status report required
**							 49 - bit 5 set - status report required
**				parameter 2: validity period of request - default 5 weeks = 197
**														  user settable TBD phb
*/
void TSYNC_csmp_parameters(char * string_buffer)
{
	strcpy(string_buffer, (tsync_state == TSYNC_GSM_REQUIRED) ? "49,197" : "17");
}

/******************************************************************************
** Function:	get pdu parameter
**
** Notes:		returns current parameter for PDU octet 1
**              depending on tsync_state
**				0x11 (17) - no status report required
**				0x31 (49) - bit 5 set - status report required
*/
uint8 TSYNC_pdu_parameter(void)
{
	return (tsync_state == TSYNC_GSM_REQUIRED) ? 0x31 : 0x11;
}

/******************************************************************************
** Function:	parse message id
**
** Notes:		saves id and time now if tsync is required
**				pointer points to message id in buffer
*/
void TSYNC_parse_id(char * in_buffer)
{
	if (tsync_state == TSYNC_GSM_REQUIRED)
	{
		// save id if it parses
		if (sscanf(in_buffer, " %d", &tsync_message_id) == 1)
		{
			// save time now
			tsync_submit_time_sec = RTC_time_date_to_sec(&RTC_now);

			// advance task
			tsync_state = TSYNC_AWAITING_GSM_STATUS;
		}
		else						// fault in id message - retry with next outgoing message
			TSYNC_action();
	}
}

/******************************************************************************
** Function:	Change the real-time clock accrding to what we received
**
** Notes:		.
*/
void tsync_change_rtc(void)
{
	RTC_type r;
	uint32 t_sec;
	int32 diff_sec;
	uint16 i;

	// Compute diff_sec: +ve if logger slow, -ve if fast
	// Parse SMS service centre timestamp, or time now, into r:
	r.yr_bcd = tsync_parse_bcd();
	r.mth_bcd = tsync_parse_bcd();
	r.day_bcd = tsync_parse_bcd();
	r.hr_bcd = tsync_parse_bcd();
	r.min_bcd = tsync_parse_bcd();
	r.sec_bcd = tsync_parse_bcd();

	// If SMS, difference is between parsed time & submission time, else between parsed time & logger time.
	if (TSYNC_use_mins_secs)						// only use mins & secs
	{
		diff_sec = (int32)tsync_bcd_min_sec_to_seconds(r.min_bcd, r.sec_bcd);
		diff_sec -= (tsync_state == TSYNC_AWAITING_GSM_STATUS) ?
			(tsync_submit_time_sec % 3600) : (RTC_time_sec % 3600);
		if (diff_sec >= 30 * 60)
			diff_sec -= 60 * 60;
		else if (diff_sec <= -30 * 60)
			diff_sec += 60 * 60;
	}
	else											// use whole date/time from NW, with offset applied
	{
		diff_sec = RTC_time_date_to_sec(&r);
		t_sec = RTC_bcd_time_to_sec(TSYNC_offset_hh, TSYNC_offset_mm, 0x00);	// get offset in seconds
		if (!TSYNC_offset_negative)
			diff_sec += t_sec;
		else
			diff_sec -= t_sec;
		diff_sec -= (tsync_state == TSYNC_AWAITING_GSM_STATUS) ?
			tsync_submit_time_sec : RTC_time_date_to_sec(&RTC_now);
	}

	if (diff_sec == 0L)
		tsync_state = TSYNC_IDLE;
	else if (abs(diff_sec) > TSYNC_threshold * 60)
	{
		// diff_sec too big for incremental change - set clock absolutely
		t_sec = RTC_time_date_to_sec(&RTC_now) + diff_sec;					// new time & date in sec
		i = (uint16)(t_sec / 86400);
		RTC_days_to_date_bcd(i, &r.day_bcd, &r.mth_bcd, &r.yr_bcd);
		RTC_set_date(r.day_bcd, r.mth_bcd, r.yr_bcd);
		diff_sec = RTC_sec_to_bcd(t_sec % 86400);
		RTC_set_time(BITS16TO23(diff_sec), BITS8TO15(diff_sec), BITS0TO7(diff_sec));

		TSYNC_change_clock();
	} 
	else			// incremental change to clock
	{
		// calculate the number of days to run correction for
		tsync_days = (diff_sec < 0) ? (uint8)(-diff_sec / 22L) + 1 : (uint8)(diff_sec / 22L) + 1;
		// calculate seconds per day required
		diff_sec /= (int32)tsync_days;
		// calculate correction value for CAL
		tsync_cal = (int8)((diff_sec * 32768L) / 5760L);
		tsync_correcting_time = true;
		// update time correction midnight detector to happen next midnight
		tsync_correction_day_bcd = RTC_now.day_bcd;
	}

	// successful parse - set up for next tsync
	tsync_remaining = tsync_interval;
	// update our midnight detector
	tsync_day_bcd = RTC_now.day_bcd;
	tsync_state = TSYNC_IDLE;
}

/******************************************************************************
** Function:	parse CMGR status message
**
** Notes:		extracts mr (message ref) and scts (arrival time) 
**				on entry, in_buffer pointer points to <mr>
**				whole of message up to and including the OK is present in the input buffer
**				if successful and tsync state awaiting status
**					compare times
**					if different
**						start correction
**				returns with tsync status IDLE if any fault
**				because we do not want to hang here - better to start again
**
**				Status message format is
**				+CMGR: <stat>,<f0>,<mr>,[<ra>],[<tora>],<scts>,<dt>,<st>
**				<data>
**				[... ]
**				OK
**
**				specifically - 
**				<f0> = 6
**				<mr> = message reference - which should be tested against remembered id of request msg
**				<scts> = service centre time stamp
**				format - "yy/MM/dd,hh:mm:ss+zz" where zz is time zone adjustment positive or negative multiples of 15 minutes
*/
void  TSYNC_parse_status(char * in_buffer)
{
	uint16   i;

	if (tsync_state != TSYNC_AWAITING_GSM_STATUS)
		return;

	// save id if it parses
	if (sscanf(in_buffer, " %ud", (int *)&i) != 1)
	{
		// fault action - retry with next outgoing message
		TSYNC_action();
		return;
	}
	
	if (i != tsync_message_id)						// if ids are not the same
		return;										// not my status - carry on waiting

	tsync_input_ptr = strstr(in_buffer, ",\"");		// find start of scts (,")
	if (!tsync_input_ptr)							// incomplete message action - retry with next outgoing message
	{
		TSYNC_action();
		return;
	}

	tsync_input_ptr += 2;							// skip over ,"
	tsync_change_rtc();
}

/*
rules
Reference [1] describes the 8-bit signed value CAL (lower half of register RCFGCAL). 
The value in this register is multiplied by 4 and added to the RTCC counter once every minute (see section 29.3.9 of ref. [1]). 
This can be used to effectively advance or retard the clock by up to 22 seconds per day.
Suppose the logger receives a GSM or GPRS time sync which tells it that the time is 09:01:00, but the logger clock is 09:00:40. 
The logger clock is 20 seconds slow. We would like to correct the clock by this amount over the next day. 
This means we have to adjust the 32768Hz counter by 20 x 32768 ticks in one day, or by 455 ticks per minute. 
The value required in the CAL register is this number divided by 4, i.e. +114. 
Similarly, if the logger clock were found to be 20 seconds fast, the value required in CAL would be -114.

The implementation of the clock correction must be as follows:
1.	Establish the amount of time difference between the logger time and the GSM or GPRS time.
2.	Divide this difference in seconds by 22 to get the number of days over which the correction will be applied. 
	Compute the adjustment which will be required each day, e.g. 38 seconds over 2 days will require 19 seconds adjustment each day.
3.	At the next midnight boundary, calculate the value required for CAL, and write it to the RCFGCAL register. 
	NB the register can only be written when the RTCC is stopped, or �immediately after a second boundary�.
4.	Execute the correction over the required number of days, and reset CAL to 0 when the correction is complete.

The correction state machine should be terminated and the CAL value reset to 0 whenever the clock is set via a #DT command.
*/

/******************************************************************************
** Function:	parse NITZ reply message
**
** Notes:		extracts time of day from <yy/mm/dd,hh:mm:ss>
**				on entry, in_buffer pointer points to first digit of <yy>
**				whole of message up to and including the OK is present in the input buffer
**				if successful and tsync state awaiting status
**					compare parsed time with internal time
**					if different
**						start correction
**				returns with tsync status IDLE if any fault
**				because we do not want to hang here - better to start again
**
*/
void  TSYNC_parse_nitz_reply(char * in_buffer)
{
	tsync_input_ptr = in_buffer;
	tsync_change_rtc();
}

/******************************************************************************
** Function:	Format status report string
**
** Notes:		reports protocol, interval, status, last calculated days and cal
*/
void   TSYNC_format_status(char * str_ptr)
{
	sprintf(str_ptr, "%u,%u,%u,%u,%d", tsync_protocol, tsync_interval, tsync_state, tsync_days, tsync_cal);
}

/******************************************************************************
** Function:	Time synchronisation task
**
** Notes:		
*/
void TSYNC_task(void)
{
	// do time correction at all times
	if (tsync_correcting_time)
	{
		// within the time period of days to run the RTC adjustment
		// if at or after midnight
		if (tsync_correction_day_bcd != RTC_now.day_bcd)
		{
			// load lower byte of RFGCAL with tsync_cal
			RTC_set_correction(tsync_cal);
			// decrement tsync_days
			tsync_days--;
			// if tsync_days now zero
			if (tsync_days == 0)
			{
				// time adjustment has been completed
				// load zero into lower byte of RFGCAL
				RTC_set_correction(0);
				tsync_correcting_time = false;
			}
		}
	}

	switch (tsync_state)
	{
		case TSYNC_OFF:
			// not running
			break;

		case TSYNC_IDLE:
			// check for initiation of time synchronisation
			// if at or after midnight
			if (tsync_day_bcd != RTC_now.day_bcd)
			{
				// keep track with remaining days while > 0
				if (tsync_remaining > 0)
					tsync_remaining--;
				// if time is due
				if (tsync_remaining == 0)
				{
					// calculate protocol from MPS configuration
					if (COM_schedule.ftp_enable)
					{
						tsync_protocol = TSYNC_GPRS_NITZ;
						tsync_state = TSYNC_NITZ_REQUIRED;
					}
					else
					{
						tsync_protocol = TSYNC_GSM;
						tsync_state = TSYNC_GSM_REQUIRED;
					}
				}
			}
			break;

		case TSYNC_GSM_REQUIRED:
			// waiting for parsing of GSM message id
			break;

		case TSYNC_AWAITING_GSM_STATUS:
			// waiting for GSM status message
			break;

		case TSYNC_NITZ_REQUIRED:
			// initiate a GPRS time request using NITZ logon
			COM_initiate_gprs_nitz_tsync();
			tsync_state = TSYNC_AWAITING_NITZ_STATUS;
			break;

		case TSYNC_AWAITING_NITZ_STATUS:
			// waiting for NITZ status message
			break;

		default:
			tsync_state = TSYNC_OFF;
			break;
	}
	tsync_day_bcd = RTC_now.day_bcd;
	tsync_correction_day_bcd = RTC_now.day_bcd;
}

/******************************************************************************
** Function:	time sync initialisation
**
** Notes:		
*/
void TSYNC_init(void)
{
	tsync_state = TSYNC_OFF;
	tsync_protocol = TSYNC_NO_PROTOCOL;
	TSYNC_on = 0;
	tsync_interval = 30;
	tsync_remaining = 30;
	TSYNC_use_mins_secs = 1;
	TSYNC_threshold = 30; 
	tsync_correcting_time = false;
	tsync_day_bcd = RTC_now.day_bcd;
	tsync_correction_day_bcd = RTC_now.day_bcd;
}

/******************************************************************************
** Function:	Resynchronise everything when clock is changed
**
** Notes:		
*/
void TSYNC_change_clock(void)
{
	ALM_update_profile();																// trigger alarm profile fetch due to changed time
																						// need to synchronise wakeup time
	COM_recalc_wakeups();
	MDM_recalc_wakeup();
	COP_recalc_wakeups();
#ifdef HDW_1FM
	MAG_recalc_wakeups();
#endif
																						// New data block headers required for any new logged values
	LOG_header_mask = 0x0000;
	LOG_derived_header_mask = 0x0000;
	LOG_sms_header_mask = 0x0000;
	LOG_derived_sms_header_mask = 0x0000;

																						// resynchronise logging
	FTP_reset_active_retrieval_info();													// reset ftp partial file retrieval to time now
#ifndef HDW_GPS
	ANA_synchronise();
#else
	GPS_recalc_wakeup();
#endif
#ifndef HDW_RS485
	DIG_synchronise();
#endif
	MDM_preset_state_flag();															// preset modem state flag to preserve modem condition before time change
}

//*****************************************************************************
// Function:	
//
// Notes: 
//
bool TSYNC_set_offset(bool plus, uint8 hh_bcd, uint8 mm_bcd)
{
	if (hh_bcd > 0x12)
		return false;							// max offset = +/- 12 hrs

	// Step 1: convert current time back to GMT using existing offset:
	RTC_add_time(TSYNC_offset_negative, TSYNC_offset_hh, TSYNC_offset_mm, 0x00);

	// Step 2: set new offset & update time
	TSYNC_offset_negative = !plus;
	TSYNC_offset_hh = hh_bcd;
	TSYNC_offset_mm = mm_bcd;
	RTC_add_time(!TSYNC_offset_negative, TSYNC_offset_hh, TSYNC_offset_mm, 0x00);
	TSYNC_change_clock();

	return true;
}

