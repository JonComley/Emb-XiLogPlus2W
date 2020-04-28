/******************************************************************************
** File:	Gps.c	
**
** Notes:	GPS NMEA receive
*/

/* CHANGES
** V4.00 280114 PB  created
**
** V4.03 220414 PB	use baud rate calculation for UART3, not fixed value (this was wrong for disconnected operation)
**					run system clock at fast rate for communications
**
** V4.04 010514 PB  add config for time of day for gps fix
**                  add gps_wakeup_timer for time of day for gps fix and set it in task when day changes
**					check gps_wakeup_timer in task for GPS fix request
**
** V4.05 070514 PB	disable gps in freight mode
**
** V5.00 231014 PB	use hardware revision for choice of power control of GPS module
**
*/

#include <string.h>
#include <stdio.h>

#include "Custom.h"
#include "Compiler.h"
#include "HardwareProfile.h"
#include "MDD File System/SD-SPI.h"
#include "MDD File System/FSIO.h"
#include "Str.h"
#include "Slp.h"
#include "Tim.h"
#include "rtc.h"
#include "Msg.h"
#include "Com.h"
#include "Usb.h"
#include "Log.h"

#define extern
#include "gps.h"
#undef extern

#ifdef HDW_GPS

// status:
#define GPS_OFF			0
#define GPS_ON			1

// Define the baud rate constants
#define BAUDRATE3       9600UL
#define BRG_DIV3        4
#define BRGH3           1

#define GPS_MAX_CHAR	128

// define timeout in 20 msecond ticks
#define GPS_AUTO_TIMEOUT_X20MS	15000

/*****************************************************************************/

char     gps_input_char;
bool     gps_line_received;
uint8    gps_rx_index;
uint8	 gps_day_bcd;
uint16   gps_auto_timeout_ticks;
FAR char gps_rx_buffer[128];
FAR char gps_line_buffer[128];

//*****************************************************************************
// interrupt functions
//*****************************************************************************

/******************************************************************************
** Function:	GPS serial receive interrupt
**
** Notes:	
*/
void __attribute__((__interrupt__, no_auto_psv)) _U3RXInterrupt(void)
{
    gps_input_char = U3RXREG;
    _U3RXIF = false;
	gps_rx_buffer[gps_rx_index] = gps_input_char;
	if (gps_rx_index < 127)
		gps_rx_index++;
	gps_rx_buffer[gps_rx_index] = '\0';											// string terminate it
	if (gps_input_char == '\n')													// catch newline and buffer line
	{
		memcpy(gps_line_buffer, gps_rx_buffer, gps_rx_index + 1);
	    gps_line_received = true;												// tell task
		gps_rx_buffer[0] = '\0';
		gps_rx_index = 0;														// clear rx buffer
	}
}

//*****************************************************************************
// local functions
//*****************************************************************************

/******************************************************************************
** Function:	clear gps data
**
** Notes:	
*/
void GPS_clear_fix(void)
{
	GPS_time[0] = '\0';
	GPS_latitude[0] = '\0';
	GPS_NS[0] = '\0';
	GPS_longitude[0] = '\0';
	GPS_EW[0] = '\0';
	GPS_fix[0] = '\0';
}

/******************************************************************************
** Function:	extract value field
**
** Notes:		returns pointer to terminating character, ',' '*' or '\0'	
*/
char * gps_extract_value(char * in_ptr, char * out_ptr)
{
	while ((*in_ptr != ',') && (*in_ptr != '*') && (*in_ptr != '\0'))			// extract contents of field, watching for end of string
	{																			// comma field 2 = latitude
		*out_ptr = *in_ptr;
		out_ptr++;
		in_ptr++;
	};
	*out_ptr = '\0';															// terminate string
	return in_ptr;
}

/******************************************************************************
** Function:	extract single letter field
**
** Notes:		returns pointer to terminating character, ',' '*' or '\0'
*/
char * gps_extract_letter(char * in_ptr, char * out_ptr)
{
	if (*in_ptr != ',')															// if not empty field
	{
		*out_ptr = *in_ptr;														// get first character from field
		out_ptr++;
		*out_ptr = '\0';
	}
	while ((*in_ptr != ',') && (*in_ptr != '*') && (*in_ptr != '\0'))			// skip to next terminator, watching for end of string
		in_ptr++;	
	return in_ptr;
}

//*****************************************************************************
// global functions
//*****************************************************************************

/******************************************************************************
** Function:	recalculate wakeup time when time of day changes
**
** Notes:
*/
void GPS_recalc_wakeup(void)
{
	gps_day_bcd = RTC_now.day_bcd;												// set correct date
	GPS_wakeup_time = RTC_bcd_time_to_sec(GPS_config.trigger_time.hr_bcd,		// set trigger time
										  GPS_config.trigger_time.min_bcd, 
										  0);
	if (GPS_config.truck_mode || (GPS_wakeup_time < RTC_time_sec))
		GPS_wakeup_time = SLP_NO_WAKEUP;										// ensure will not wakeup if time is past
}

/******************************************************************************
** Function:	GPS can sleep
**
** Notes:		
*/
bool GPS_can_sleep(void)
{
	return !GPS_is_on;
}

/******************************************************************************
** Function:	turn GPS rx on
**
** Notes:	
*/
void GPS_on(void)
{
	uint32 clock;

	if (HDW_revision == 0)														// use hardware revision to choose route for GPS module power control
		HDW_CONTROL1_ON = 1;													// prototypes on issue 3 boards use control output 1
	else
		HDW_GPS_POWER_CTRL = 0;													// issue 4 boards use dedicated active low output
	gps_rx_index = NULL;

	if (!GPS_is_on)
		gps_auto_timeout_ticks = GPS_AUTO_TIMEOUT_X20MS;
	GPS_is_on = true;

	// Configure UART
	U3MODEbits.UARTEN = true;													// enable UART 3
	SLP_set_required_clock_speed();												// sets high speed clock
//	U3BRG = ((GetSystemClock() / 2) + (BRG_DIV3 / 2 * BAUDRATE3) / BRG_DIV3 / BAUDRATE3 - 1);
	clock = GetSystemClock();
	if (clock == 32000000)
		U3BRG = 415;
	else if (clock == 4000000)
		U3BRG = 25;
	else
		U3BRG = 7;
    U3MODE = 0;
    U3MODEbits.BRGH = BRGH3;
	U3MODEbits.UEN = 0;															// no flow control
    U3STA = 0;
    U3MODEbits.UARTEN = true;
    U3STAbits.UTXEN = false;
    _U3RXIF = 0;
	_U3RXIE = true;																// enable receive interrupt
																				// turn module power on
	GPS_clear_fix();
	LOG_enqueue_value(LOG_ACTIVITY_INDEX, LOG_GPS_FILE, __LINE__);				// GPS power on
}

/******************************************************************************
** Function:	turn GPS rx off
**
** Notes:		preserves received data
*/
void GPS_off(void)
{
	if (HDW_revision == 0)														// use hardware revision to choose route for GPS module power control
		HDW_CONTROL1_ON = 0;													// prototypes on issue 3 boards use control output 1
	else
		HDW_GPS_POWER_CTRL = 1;													// issue 4 boards use dedicated active low output
	gps_rx_index = NULL;
	gps_line_received = false;
	GPS_is_on = false;
	U3MODEbits.UARTEN = false;													// disable UART3
	SLP_set_required_clock_speed();
}

/******************************************************************************
** Function:	GPS task
**
** Notes:		receive and extract NMEA GPS sentence contents
*/
void GPS_task(void)
{
	char * cptr;
	char * sptr;

	if (gps_day_bcd != RTC_now.day_bcd)											// date has changed (normally midnight)
		GPS_recalc_wakeup();													// recalc wakeup

	if (GPS_wakeup_time <= RTC_time_sec)										// Check for once a day trigger
	{
		if (COM_commissioning_mode != 2)										// if not in freight mode
			GPS_on();															// trigger automatic GPS task
		GPS_wakeup_time = SLP_NO_WAKEUP;										// ensure does not trigger again until date changes
	}

	if (GPS_is_on)
	{
		if (TIM_20ms_tick)
		{
			if (gps_auto_timeout_ticks > 0)
				gps_auto_timeout_ticks--;
			else
			{
				GPS_off();
				LOG_enqueue_value(LOG_ACTIVITY_INDEX,							// GPS time out 
								  LOG_GPS_FILE, 
								  __LINE__);

				if (GPS_config.truck_mode)										// new header required in D1A file
					LOG_header_mask &= ~(1 << LOG_DIGITAL_1A_INDEX);
			}
		}

		if (gps_line_received)													// monitor line received flag when on
		{
			gps_line_received = false;											// clear flag and process line

			USB_monitor_string(gps_line_buffer);

			cptr = strstr(gps_line_buffer, "$GPGGA,");
			if (cptr != NULL)													// if $GPGGA
			{																	// extract fix data
				cptr+= 7;														// skip first comma
				sptr = GPS_time;												// get time
				cptr = gps_extract_value(cptr, sptr);
				if (*cptr == '\0')												// exit if string incomplete 
					return;
				cptr++;															// skip comma at start of field
				sptr = GPS_latitude;											// get latitude
				cptr = gps_extract_value(cptr, sptr);
				if (*cptr == '\0')												// exit if string incomplete 
					return;
				cptr++;															// skip comma at start of field
				sptr = GPS_NS;													// get N or S
				cptr = gps_extract_letter(cptr, sptr);
				if (*cptr == '\0')												// exit if string incomplete 
					return;
				cptr++;															// skip comma at start of field
				sptr = GPS_longitude;											// get longitude
				cptr = gps_extract_value(cptr, sptr);
				if (*cptr == '\0')												// exit if string incomplete 
					return;
				cptr++;															// skip comma at start of field
				sptr = GPS_EW;													// get E or W
				cptr = gps_extract_letter(cptr, sptr);
				if (*cptr == '\0')												// exit if string incomplete 
					return;
				cptr++;															// skip comma at start of field
				if (*cptr != ',')												// if not empty field
				{
					sptr = GPS_fix;												// get Fix Quality, 0, 1 or 2, one character
					cptr = gps_extract_letter(cptr, sptr);
				}
				if ((GPS_latitude[0] == '\0') ||								// if not got a fix
                    (GPS_longitude[0] == '\0') ||
					(GPS_NS[0] == '\0') ||
					(GPS_EW[0] == '\0') ||
					(GPS_fix[0] == '\0') ||
					(GPS_fix[0] == '0'))
					return;														// exit
				else															// else got a fix
				{
					GPS_off();													// switch GPS module off
					LOG_enqueue_value(LOG_ACTIVITY_INDEX,						// GPS got a fix 
									  LOG_GPS_FILE, 
									  __LINE__);

					if (GPS_config.truck_mode)									// new header required in D1A file
						LOG_header_mask &= ~(1 << LOG_DIGITAL_1A_INDEX);

					if (!COM_schedule.ftp_enable)								// if ftp not enabled
					{
						sprintf(STR_buffer, "dGPS=0,%02x:%02x,%s,%s,%s,%s,%s,%s", // send sms text message to all host numbers
								GPS_config.trigger_time.hr_bcd, 
								GPS_config.trigger_time.min_bcd, 
								GPS_time, 
								GPS_latitude, 
								GPS_NS, 
								GPS_longitude, 
								GPS_EW, 
								GPS_fix);
						MSG_send(MSG_TYPE_SMS_TEXT, STR_buffer, COM_host1);
						MSG_flush_outbox_buffer(true);							// immediate tx
						MSG_send(MSG_TYPE_SMS_TEXT, STR_buffer, COM_host2);
						MSG_flush_outbox_buffer(true);							// immediate tx
						MSG_send(MSG_TYPE_SMS_TEXT, STR_buffer, COM_host3);
						MSG_flush_outbox_buffer(true);							// immediate tx
					}
				}
			}
		}
	}
}

/******************************************************************************
** Function:	GPS initialisation
**
** Notes:	
*/
void GPS_init(void)
{
	GPS_config.trigger_time.hr_bcd = 0x12;										// set default to midday
	GPS_config.trigger_time.min_bcd = 0x00;
	GPS_off();
	GPS_clear_fix();
	gps_day_bcd = RTC_now.day_bcd;

#ifdef HDW_GPS_TRUCK_MODE
	GPS_config.truck_mode = true;
#endif
}

#endif

// eof

