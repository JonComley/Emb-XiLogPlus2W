/******************************************************************************
** File:	USB.c
**
** Notes:	
*/

/* CHANGES
** V2.40 061210 MA	HDW_EXT_SUPPLY_N line debounced to avoid USB glitches when using modem
**
** V2.56 240211 PB  Call COM_schedule_check_dflags instead of CMD_check_dirty_flags, in case we are doing a signal test
**
** V2.90 090811 PB  DEL150 - addition of external power connected alarm tx to code that detects external power disconnected
**                           added pending tx alarm flag so that alarm may be sent on the hour interval specified
**                           usb flags placed in a bitfield variable
**                           alarm text added as a fifth line to \CONFIG\ALMBATT.TXT, with default if not present 
**                           new function usb_send_disconnected_alarm(void)
**
** V2.39 171013 PB  bring up to date with Xilog+ V3.05
**					correction to usb_send_disconnected_alarm() - add send ftp low battery alarm
**
** V3.31 141113 PB  use CFS_open() != CFS_OPEN test to keep file system awake in USB_task() states READ_FILE and WRITE_FILE
**
** V3.32 201113 PB  revise use of CFS_open() in READ_FILE and WRITE_FILE states
*/

#include "Custom.h"
#include "Compiler.h"
#include "MDD File System/SD-SPI.h"
#include "MDD File System/FSIO.h"

#include "HardwareProfile.h"
#include "Str.h"
#include "Tim.h"
#include "Cfs.h"
#include "rtc.h"
#include "Log.h"
#include "pdu.h"
#include "Ana.h"
#include "Dig.h"
#include "cmd.h"
#include "slp.h"
#include "scf.h"
#include "msg.h"
#include "com.h"
#include "alm.h"

#include "usb_config.h"
#include "Usb/usb_device.h"                         // Required
#include "Usb/usb.h"
#include "MDD File System/FSIO.h"

#define extern
#include "Usb.h"
#undef extern

#ifdef HDW_ATEX
#define USB_ATEX_TIMEOUT_X20MS	(2 * 60 * 50)
#endif

// pending action required:
#define USB_NO_ACTION			0
#define USB_PENDING_READ		1
#define USB_PENDING_WRITE		2
#define USB_PENDING_DIR			3
#define USB_NUM_ACTIONS			4

#ifndef WIN32
#ifndef ECLIPSE
#pragma udata USB_VARS
#endif
#endif
FAR char usb_rx_buffer[256];
FAR char usb_tx_buffer[512];

#ifndef WIN32
#ifndef ECLIPSE
#pragma udata
#endif
#endif

BITFIELD usb_flags;

#define usb_prompt						usb_flags.b0
#define usb_pc_detected					usb_flags.b1
#define usb_pending_disconnect_alarm	usb_flags.b2
#define usb_pending_ext_pwr_connected	usb_flags.b3

int usb_action;

int usb_rx_index;
int usb_tx_index;
int usb_eof_index;
int usb_monitor_index;
int usb_timer_x20ms;
int usb_ext_pwr_debounce;

uint32 usb_file_pos;

USB_HANDLE USBOutHandle;
USB_HANDLE USBInHandle;

SearchRec usb_srch;

FAR char usb_path[80];
FAR char usb_monitor_buffer[512];

const uint8 usb_next_state[USB_NUM_ACTIONS] =
{
	USB_RX_COMMAND, USB_READ_FILE, USB_WRITE_FILE, USB_DIR
};

/******************************************************************************
** Function:	Add a string to the monitor buffer without adding any newline
**
** Notes:	
*/
void USB_monitor_prompt(char *s)
{
	if (!USB_echo)
		return;

	if (usb_monitor_buffer[0] == '\0')
		usb_monitor_index = 0;

	while (*s != '\0')
	{
		if (usb_monitor_index < sizeof(usb_monitor_buffer) - 2)		// allow space for \n\0
			usb_monitor_buffer[usb_monitor_index++] = *s++;
		else
			break;
	}

	usb_monitor_buffer[usb_monitor_index] = '\0';
}

/******************************************************************************
** Function:	Add a string to the monitor buffer, adding \r\n
**
** Notes:		Adds '\0' at the end
*/
void USB_monitor_string(char *s)
{
	USB_monitor_prompt(s);
	if (usb_monitor_buffer[usb_monitor_index - 1] != '\n')
		USB_monitor_prompt("\r\n");
}

/******************************************************************************
** Function:	Start a file read or write on the USB channel
**
** Notes:	
*/
void USB_transfer_file(char *path, char *filename, bool write)
{
	strncpy(usb_path, path, sizeof(usb_path));
	strncpy(usb_srch.filename, filename, sizeof(usb_srch.filename));
	usb_action = write ? USB_PENDING_WRITE : USB_PENDING_READ;
}

/******************************************************************************
** Function:		
**
** Notes:	
*/
void USB_dir(char *path)
{
	strncpy(usb_path, path, sizeof(usb_path));
	usb_action = USB_PENDING_DIR;
}

/******************************************************************************
** Function:	Poll for an OUT packet from host
**
** Notes:		set up to receive data at the right point in the buffer	
*/
void usb_rx(void)
{
	USBOutHandle = USBTransferOnePacket(USBGEN_EP_NUM, OUT_FROM_HOST,
										(BYTE *)&usb_rx_buffer[usb_rx_index], USBGEN_EP_SIZE);
#ifdef HDW_ATEX
	usb_timer_x20ms = USB_ATEX_TIMEOUT_X20MS;
#endif
}

/******************************************************************************
** Function:	send "external power disconnected" alarm
**
** Notes:	
*/
void usb_send_disconnected_alarm(void)
{
	int i;
	char * char_ptr;

	// send one off alarm
	i = sprintf(STR_buffer, "dALARM=%02X%02X%02X,%02X:%02X:%02X,%s,",
		RTC_now.day_bcd, RTC_now.mth_bcd, RTC_now.yr_bcd,
		RTC_now.hr_bcd, RTC_now.min_bcd, RTC_now.sec_bcd,
		COM_sitename);
	char_ptr = &STR_buffer[i];

	// get message from ALMBATT.TXT file - allow 256 characters
	if (CFS_read_line((char *)CFS_config_path, (char *)"ALMBATT.TXT", 5, char_ptr, 256) < 1)
	{
		// if no file concatenate default string
		*char_ptr = '\0';
		strcat(STR_buffer, "ExtDisconnect");
	}
	LOG_entry(STR_buffer);
	ALM_send_to_all_sms_numbers();
	if (COM_schedule.ftp_enable)
		ALM_send_ftp_alarm(ALM_FTP_BATTERY);
}

/******************************************************************************
** Function:	External power just disconnected
**
** Notes:	
*/
void usb_external_power_disconnected(void)
{
	if (CFS_open())									// keep awake while we log disconnect event
	{
		LOG_entry("External power disconnected");

		USB_state = USB_DISCONNECTED;

		ANA_ext_power_disconnected();

		// if logging
		if (LOG_state == LOG_LOGGING)
			usb_pending_disconnect_alarm = true;	// send ext disconnected alarm at next interval
		else
			USB_wakeup_time = SLP_NO_WAKEUP;		// cancel power test interval
	}
}

/******************************************************************************
** Function:	USB host just disconnected
**
** Notes:	
*/
void usb_host_disconnected(void)
{
	HDW_USB_BOOST_ON = false;

	USB_echo = false;					// stop stuff building up in monitor buffer
	usb_monitor_buffer[0] = '\0';
	usb_monitor_index = 0;

	if (USB_state >= USB_RX_COMMAND)	// USB was definitely connected
	{
		LOG_entry("USB disconnected");	// ...so file system already open
#ifdef HDW_PRIMELOG_PLUS
		CMD_check_dirty_flags();
#else
		COM_schedule_check_dflags();
#endif
	}

	// Do all the things USBDeviceTasks does if USB is disconnected,
	// but do them unconditionally.
	// Disable module & detach from bus
    U1CON = 0;             

    // Mask all USB interrupts              
    U1IE = 0;

	// Added by MA: disable module, pullup etc.
	U1CNFG1 = 0;
	U1CNFG2 = 0;
	U1PWRC = 0;

    //Move to the detached state                  
    USBDeviceState = DETACHED_STATE;

	USB_active = false;
	usb_pc_detected = false;
	SLP_set_required_clock_speed();

	USB_state = USB_DISCONNECTED;
	USB_wakeup_time = SLP_NO_WAKEUP;	// should be anyway

	ANA_ext_power_disconnected();
}

/******************************************************************************
** Function:	toggle ext power test line to check if ext batt still connected
**
** Notes:	
*/
void usb_toggle_battest(void)
{
	if (!HDW_BATTEST_EXT_ON)	// don't do it if line being toggled for analogue read			
	{
		HDW_BATTEST_EXT_ON = true;
		TIM_delay_ms(1);
		HDW_BATTEST_EXT_ON = false;
	}
}

/******************************************************************************
** Function:	Check for connection timeout
**
** Notes:	If never gets device address, must be external power, but less than 6V
*/
void usb_check_connection_timeout(void)
{
	if (TIM_20ms_tick)
	{
		if (usb_timer_x20ms != 0)
			usb_timer_x20ms--;
		else if (CFS_open())									// ensure we can log usage
		{
			usb_host_disconnected();							// sets USB_state = USB_DISCONNECTED
#ifdef HDW_ATEX
			LOG_entry("USB timeout");
#else
			LOG_entry("Low voltage external power connected");
#endif
			USB_state = USB_LOW_VOLTAGE_EXT_BATTERY;
		}
	}
}

/******************************************************************************
** Function:	USB comms task
**
** Notes: Truth table - RA2 = HDW_EXT_SUPPLY_N, RF8 = HDW_USB_MON
**	RA2	RF8	
**	0	0	After B11 pulse or F8 pulse, definitely high voltage ext battery connected
**	0	1	Transient state when high voltage ext power just connected.
**	1	0	Nothing connected
**	1	1	USB connected, or low voltage external battery
** NB after disconnecting ext power, must pulse B11 (HDW_BATTEST_EXT_ON) before
** RA2 will return high.
** In the transient state, USB_MON only goes low when USB peripheral turned off.
*/
void USB_task(void)
{
	int i;
	FSFILE *f;

	if (USB_wakeup_time <= RTC_time_sec)				// time to toggle ext power test line
	{
		if (usb_pending_disconnect_alarm)				// if alarm pending
		{
			if (!CFS_open())							// ensure we can put a message in the outbox
				return;
			// else
			usb_pending_disconnect_alarm = false;
			usb_send_disconnected_alarm();
			USB_wakeup_time = SLP_NO_WAKEUP;
		}
		// do it only if line being not toggled for analogue read, and external battery connected
		else if (USB_state == USB_EXT_BATTERY)
		{
			usb_toggle_battest();
			USB_wakeup_time = RTC_time_sec;
			LOG_set_next_time(&USB_wakeup_time, 17, false);	// interval = 1 hour
		}
	}

	if (usb_pending_ext_pwr_connected)
	{
		if (CFS_state == CFS_OPEN)
		{
			LOG_entry(usb_path);							// saves a whole extra buffer
			usb_pending_ext_pwr_connected = false;
			usb_path[0] = '\0';
		}
	}

	if (!HDW_EXT_SUPPLY_N)							// high voltage ext power detected
	{
		if (USB_state != USB_EXT_BATTERY)			// may need to go into external battery state
		{											// (NB may currently be in high-v ext battery, or USB)
			if (!USB_active)						// don't debounce the line
				usb_ext_pwr_debounce = 0;
			else if (TIM_20ms_tick && (usb_ext_pwr_debounce != 0))
				usb_ext_pwr_debounce--;

			if (usb_ext_pwr_debounce == 0)			// definitely high-v ext power
			{
				LOG_enqueue_value(LOG_ACTIVITY_INDEX, LOG_USB_FILE, __LINE__);
				if (USB_active || (USB_state >= USB_PWR_DETECTED))		// USB was previously powered or connected
					usb_host_disconnected();

				// LOG_entry("External power connected");
				usb_pending_ext_pwr_connected = true;
				sprintf(usb_path, "Ext power was connected at %02x:%02x:%02x",			// saves a whole extra buffer
					RTC_now.hr_bcd, RTC_now.min_bcd, RTC_now.sec_bcd);

				USB_state = USB_EXT_BATTERY;
				USB_wakeup_time = RTC_time_sec;											// set first wakeup time for checking if disconnected
				LOG_set_next_time(&USB_wakeup_time, 17, false);							// interval = 1 hour
				usb_pending_disconnect_alarm = false;
				return;
			}
			// else keep doing USB tasks in case this is a glitch
		}
		else										// high-v power detected, & in the right state
			return;									// don't do rest of state machine
	}
	else
		usb_ext_pwr_debounce = 10;
	
	// either nothing connected, USB connected, low voltage batt connected, or high-v ext pwr not yet debounced
	
	if (!HDW_USB_MON)								// Nothing connected
	{
		if (USB_state != USB_DISCONNECTED)			// disconnect whatever is connected
		{
			if (USB_state >= USB_PWR_DETECTED)		// USB was previously powered or connected
			{
				LOG_enqueue_value(LOG_ACTIVITY_INDEX, LOG_USB_FILE, __LINE__);
				usb_host_disconnected();
			}
			else									// ext battery or low voltage ext battery was previously connected
				usb_external_power_disconnected();
				// don't log event here, as state will only go disconnected when CFS ready.
		}

		return;
	}
	// else: from this point on, USB monitor is high.
	// This may be due to USB host connected, low-voltage ext power connected, or ext power just connected

	if (USB_state == USB_LOW_VOLTAGE_EXT_BATTERY)	// DON'T run USB peripheral
		return;
	// else:

	USBDeviceTasks();

	switch (USB_state)
	{
	case USB_DISCONNECTED:
		// exit if doing a script file (at power on)
		if (SCF_progress() != 100)
			break;

		if (!CFS_open())														// fire up SD card so we can log connected event
			break;																// exit if not ready or fails to open
		USBDeviceInit();
		USB_state = USB_PWR_DETECTED;
		HDW_USB_BOOST_ON = true;			// take our power from the USB line
		USB_active = true;
		SLP_set_required_clock_speed();
		usb_timer_x20ms = 50 * 8;	// 8 sec for PC to talk to us, else it's just ext power

		// Power detected on USB line - could be USB host or external power
		LOG_enqueue_value(LOG_ACTIVITY_INDEX, LOG_USB_FILE, __LINE__);

		LOG_flush();	// flush buffered data to file so PC can see it
		break;

	case USB_PWR_DETECTED:
		if (usb_pc_detected || (USBDeviceState >= CONFIGURED_STATE))	// something trying to enumerate us
		{
			LOG_enqueue_value(LOG_ACTIVITY_INDEX, LOG_USB_FILE, __LINE__);
			USB_state = USB_PC_DETECTED;
			usb_timer_x20ms = 6 * 60 * 50;		// give it 6 mins to install driver
		}
		else
			usb_check_connection_timeout();
		break;

	case USB_PC_DETECTED:	// A PC is talking to us, but hasn't enumerated us yet
		if (USBDeviceState >= CONFIGURED_STATE)		// have just gone configured
		{
			USB_monitor_prompt("USB connected");
			LOG_entry("USB connected");

			usb_rx_index = 0;
			usb_rx();
			USBInHandle = USBTransferOnePacket(USBGEN_EP_NUM, IN_TO_HOST, (BYTE *)usb_tx_buffer, 0);
			USB_state = USB_RX_COMMAND;
			usb_action = USB_NO_ACTION;
			USB_echo = false;						// always start USB session with echo off.
		}
		else
			usb_check_connection_timeout();
		break;

	case USB_RX_COMMAND:
		if (U1OTGSTATbits.LSTATE)					// lost USB
		{
			usb_host_disconnected();				// This code causes occasional USB drop-out.
			break;									// However, removing it causes USB lock-up.
		}

		if (!USBSuspendControl)
		{
			if (usb_monitor_index != 0)					// something to monitor
			{
				usb_tx_index = 0;
				USB_state = USB_MONITOR;
				break;
			}
			// else:
			
			if (usb_prompt && !USBInHandle->STAT.UOWN)	// tx prompt string
			{
				strcpy(usb_tx_buffer, "\r\n> ");

				USBInHandle = USBTransferOnePacket(USBGEN_EP_NUM, IN_TO_HOST, (BYTE *)usb_tx_buffer, 64);
				usb_rx_index = 0;						// reset command buffer
				usb_prompt = false;
			}

			if (!USBOutHandle->STAT.UOWN)				// packet received
			{
				if (usb_rx_buffer[0] == '\0')
					usb_prompt = true;
				else
				{
					i = usb_rx_index;
					usb_rx_index += USBOutHandle->count;

					for (; i < usb_rx_index; i++)
					{
						if ((usb_rx_buffer[i] == '\r') || (usb_rx_buffer[i] == '\n') || (usb_rx_buffer[i] == '\0'))
						{
							usb_rx_buffer[i] = '\0';
							usb_rx_index = sizeof(usb_rx_buffer);
							break;
						}
					}

					if (usb_rx_index > sizeof(usb_rx_buffer) - USBGEN_EP_SIZE)	// buffer full or end of command detected
					{
						usb_action = USB_NO_ACTION;
						CMD_schedule_parse(CMD_SOURCE_USB, usb_rx_buffer, usb_tx_buffer);
						usb_tx_index = 0;
						USB_state = USB_TX_RESPONSE;

						usb_rx_index = 0;					// ready for next command
					}
				}
				
				usb_rx();									// re-arm Rx endpoint
			}
		}
		
#ifdef HDW_ATEX
		usb_check_connection_timeout();						// time out if no USB comms
#endif
		break;

	case USB_TX_RESPONSE:
		if (CMD_busy(CMD_SOURCE_USB) || USBInHandle->STAT.UOWN || USBSuspendControl)	// cmd busy or USB tx in progress
			break;
		// else:

		USBInHandle = USBTransferOnePacket(USBGEN_EP_NUM, IN_TO_HOST, (BYTE *)&usb_tx_buffer[usb_tx_index], 64);

		// see if what we're transmitting has end-of-string in it:
		i = usb_tx_index + 64;
		for (; usb_tx_index < i; usb_tx_index++)
		{
			if (usb_tx_buffer[usb_tx_index] == '\0')
			{
				// check if any pending actions:
				usb_file_pos = 0;
				usb_eof_index = -1;
				usb_srch.searchname[0] = '\0';
				usb_tx_index = 0;
				if (usb_action >= USB_NUM_ACTIONS)	// safety check
					usb_action = USB_NO_ACTION; 
				USB_state = usb_next_state[usb_action];
				usb_prompt = (usb_action == USB_NO_ACTION);
				usb_action = USB_NO_ACTION;
				break;
			}
		}
		// else usb_tx_index += 64;
		break;

	case USB_READ_FILE:
		if (USBInHandle->STAT.UOWN || USBSuspendControl)				// USB tx busy
			break;
		// else:

#ifdef HDW_ATEX
		usb_timer_x20ms = USB_ATEX_TIMEOUT_X20MS;						// keep USB connection alive
#endif

		if (CFS_state == CFS_OPEN)											// if file system awake (it should be)
		{
			if ((usb_file_pos == 0) || (usb_tx_index > 511))				// get next sector of file
			{
				if ((usb_srch.filename[0] == '\0') || FSchdir(usb_path) != 0)	// can't set working directory
					usb_srch.filename[0] = '\0';
				else
				{
					f = FSfopen(usb_srch.filename, "r");
					if (f == NULL)											// can't open file
						usb_srch.filename[0] = '\0';
					else
					{
						// MUST close file from here on
						if (FSfseek(f, usb_file_pos, SEEK_SET) != 0)		// can't find next block
							usb_srch.filename[0] = '\0';
						else
						{
							FSfread(usb_tx_buffer, 512, 1, f);
							usb_tx_index = 0;
							usb_file_pos += 512;
							if (usb_file_pos > f->size)						// EOF somewhere in this block
							{
								usb_eof_index = f->size & 0x1FF;			// = 0 to 511
								usb_tx_buffer[usb_eof_index] = '\0';		// string-terminate the file
							}
						}
						CFS_close_file(f);
					}
				}
			}
		}
		else
			usb_srch.filename[0] = '\0';									// default zero length packet if file system died

		if (usb_srch.filename[0] == '\0')								// send a packet starting '\0'
		{
			usb_tx_buffer[0] = '\0';
			usb_tx_index  = 0;
			usb_eof_index = 0;
		}

		USBInHandle = USBTransferOnePacket(USBGEN_EP_NUM, IN_TO_HOST, (BYTE *)&usb_tx_buffer[usb_tx_index], 64);
		usb_tx_index += 64;
		if ((usb_eof_index >= 0) && (usb_tx_index > usb_eof_index))		// EOF sent
		{
			usb_srch.filename[0] = '\0';
			USB_state = USB_RX_COMMAND;
			usb_prompt = true;
		}
		break;

	case USB_WRITE_FILE:
		if (USBOutHandle->STAT.UOWN || USBSuspendControl)	// receive busy
			break;
		// else:

		// Buffer incoming bytes in tx_buffer until whole sector to write
		i = 0;		// assume no EOF
		for (usb_rx_index = 0; usb_rx_index < USBGEN_EP_SIZE; usb_rx_index++)
		{
			if (usb_tx_index < sizeof(usb_tx_buffer))
				usb_tx_buffer[usb_tx_index] = usb_rx_buffer[usb_rx_index];

			if (usb_rx_buffer[usb_rx_index] == '\0')
			{
				i = 1;	// EOF encountered
				break;	// usb_tx_index = length to write ('\0' not written to file)
			}
			// else:

			usb_tx_index++;
		}

		// if end-of-file received & usb_tx_buffer has stuff to write, or
		// if whole sector received, append new sector
		if (((i > 0) && (usb_tx_index > 0)) || (usb_tx_index >= sizeof(usb_tx_buffer)))
		{
			i++;									// assume failure (i = 1 or 2)

			if (CFS_state == CFS_OPEN)					// only write if file system awake (it should be)
			{
				if (FSchdir(usb_path) == 0)				// working directory OK
				{
					f = FSfopen(usb_srch.filename, "a");
					if (f != NULL)
					{
						if (FSfwrite(usb_tx_buffer, usb_tx_index, 1, f) >= 1)
							i--;						// success (i = 0 or 1)
	
						CFS_close_file(f);
					}
				}
			}
			usb_tx_index = 0;						// ready for next sector
		}

		if (i > 0)									// EOF or abort
		{
			usb_prompt = (usb_rx_buffer[0] == '\0');
			USB_state = USB_RX_COMMAND;
		}

		usb_rx_index = 0;
		usb_rx();
		break;

	case USB_DIR:
		if (USBInHandle->STAT.UOWN || USBSuspendControl)	// USB tx busy
			break;
		// else:

		if (FSchdir(usb_path) != 0)					// can't CD
			usb_path[0] = '\0';
		else if (usb_srch.searchname[0] == '\0')	// haven't started search yet
		{
			if (FindFirst("*.*", ATTR_MASK & ~ATTR_VOLUME, &usb_srch) != 0)
				usb_path[0] = '\0';
		}
		else if (FindNext(&usb_srch) != 0)
			usb_path[0] = '\0';

		if (usb_path[0] != '\0')					// got a result
		{
			STR_print_file_timestamp(usb_srch.timestamp);
			sprintf(usb_tx_buffer, "%-15s %s %10ld %02X\r\n",
				usb_srch.filename, STR_buffer, usb_srch.filesize, usb_srch.attributes);
			USBInHandle = USBTransferOnePacket(USBGEN_EP_NUM, IN_TO_HOST, (BYTE *)usb_tx_buffer, 64);
		}
		else
		{
			usb_prompt = true;
			USB_state = USB_RX_COMMAND;
		}
		break;

	case USB_MONITOR:
		if (USBInHandle->STAT.UOWN || USBSuspendControl)	// USB tx in progress
			break;
		// else:

		USBInHandle = USBTransferOnePacket(USBGEN_EP_NUM, IN_TO_HOST, (BYTE *)&usb_monitor_buffer[usb_tx_index], 64);
		usb_tx_index += 64;
		if (usb_tx_index > usb_monitor_index)		// have sent the closing '\0'
		{
			usb_monitor_index = 0;
			usb_tx_index = 0;
			USB_state = USB_RX_COMMAND;
			usb_prompt = true;
		}
		break;

	case USB_EXT_BATTERY:
		if (HDW_EXT_SUPPLY_N)						// either USB or (more likely) low voltage ext power
			usb_external_power_disconnected();
		// else transient state while connecting ext battery
		break;

	// case USB_LOW_VOLTAGE_EXT_BATTERY:			// Already dealt with prior to switch
	//	break;

	default:
		LOG_enqueue_value(LOG_ACTIVITY_INDEX, LOG_USB_FILE, __LINE__);	// "assert"
		usb_host_disconnected();
		break;
	}
}


// ******************************************************************************************************
// ************** USB Callback Functions ****************************************************************
// ******************************************************************************************************
// The USB firmware stack will call the callback functions USBCBxxx() in response to certain USB related
// events.  For example, if the host PC is powering down, it will stop sending out Start of Frame (SOF)
// packets to your device.  In response to this, all USB devices are supposed to decrease their power
// consumption from the USB Vbus to <2.5mA each.  The USB module detects this condition (which according
// to the USB specifications is 3+ms of no bus activity/SOF packets) and then calls the USBCBSuspend()
// function.  You should modify these callback functions to take appropriate actions for each of these
// conditions.  For example, in the USBCBSuspend(), you may wish to add code that will decrease power
// consumption from Vbus to <2.5mA (such as by clock switching, turning off LEDs, putting the
// microcontroller to sleep, etc.).  Then, in the USBCBWakeFromSuspend() function, you may then wish to
// add code that undoes the power saving things done in the USBCBSuspend() function.

// The USBCBSendResume() function is special, in that the USB stack will not automatically call this
// function.  This function is meant to be called from the application firmware instead.  See the
// additional comments near the function.

/******************************************************************************
 * Function:        void USBCBSuspend(void)
 *
 * PreCondition:    None
 *
 * Input:           None
 *
 * Output:          None
 *
 * Side Effects:    None
 *
 * Overview:        Call back that is invoked when a USB suspend is detected
 *
 * Note:            None
 *****************************************************************************/
void USBCBSuspend(void)
{
	//Example power saving code.  Insert appropriate code here for the desired
	//application behavior.  If the microcontroller will be put to sleep, a
	//process similar to that shown below may be used:
	
	//ConfigureIOPinsForLowPower();
	//SaveStateOfAllInterruptEnableBits();
	//DisableAllInterruptEnableBits();
	//EnableOnlyTheInterruptsWhichWillBeUsedToWakeTheMicro();	//should enable at least USBActivityIF as a wake source
	//Sleep();
	//RestoreStateOfAllPreviouslySavedInterruptEnableBits();	//Preferrably, this should be done in the USBCBWakeFromSuspend() function instead.
	//RestoreIOPinsToNormal();									//Preferrably, this should be done in the USBCBWakeFromSuspend() function instead.

	//IMPORTANT NOTE: Do not clear the USBActivityIF (ACTVIF) bit here.  This bit is 
	//cleared inside the usb_device.c file.  Clearing USBActivityIF here will cause 
	//things to not work as intended.	
	

    #if defined(__C30__)
    #if 0
        U1EIR = 0xFFFF;
        U1IR = 0xFFFF;
        U1OTGIR = 0xFFFF;
        IFS5bits.USB1IF = 0;
        IEC5bits.USB1IE = 1;
        U1OTGIEbits.ACTVIE = 1;
        U1OTGIRbits.ACTVIF = 1;
        TRISA &= 0xFF3F;
        LATAbits.LATA6 = 1;
        Sleep();
        LATAbits.LATA6 = 0;
    #endif
    #endif
}


/******************************************************************************
 * Function:        void _USB1Interrupt(void)
 *
 * PreCondition:    None
 *
 * Input:           None
 *
 * Output:          None
 *
 * Side Effects:    None
 *
 * Overview:        This function is called when the USB interrupt bit is set
 *					In this example the interrupt is only used when the device
 *					goes to sleep when it receives a USB suspend command
 *
 * Note:            None
 *****************************************************************************/
#if 0
void __attribute__ ((interrupt)) _USB1Interrupt(void)
{
    #if !defined(self_powered)
        if(U1OTGIRbits.ACTVIF)
        {
            LATAbits.LATA7 = 1;
        
            IEC5bits.USB1IE = 0;
            U1OTGIEbits.ACTVIE = 0;
            IFS5bits.USB1IF = 0;
        
            //USBClearInterruptFlag(USBActivityIFReg,USBActivityIFBitNum);
            USBClearInterruptFlag(USBIdleIFReg,USBIdleIFBitNum);
            //USBSuspendControl = 0;
            LATAbits.LATA7 = 0;
        }
    #endif
}
#endif

/******************************************************************************
 * Function:        void USBCBWakeFromSuspend(void)
 *
 * PreCondition:    None
 *
 * Input:           None
 *
 * Output:          None
 *
 * Side Effects:    None
 *
 * Overview:        The host may put USB peripheral devices in low power
 *					suspend mode (by "sending" 3+ms of idle).  Once in suspend
 *					mode, the host may wake the device back up by sending non-
 *					idle state signalling.
 *					
 *					This call back is invoked when a wakeup from USB suspend 
 *					is detected.
 *
 * Note:            None
 *****************************************************************************/
void USBCBWakeFromSuspend(void)
{
	// If clock switching or other power savings measures were taken when
	// executing the USBCBSuspend() function, now would be a good time to
	// switch back to normal full power run mode conditions.  The host allows
	// a few milliseconds of wakeup time, after which the device must be 
	// fully back to normal, and capable of receiving and processing USB
	// packets.  In order to do this, the USB module must receive proper
	// clocking (IE: 48MHz clock must be available to SIE for full speed USB
	// operation).
}

/********************************************************************
 * Function:        void USBCB_SOF_Handler(void)
 *
 * PreCondition:    None
 *
 * Input:           None
 *
 * Output:          None
 *
 * Side Effects:    None
 *
 * Overview:        The USB host sends out a SOF packet to full-speed
 *                  devices every 1 ms. This interrupt may be useful
 *                  for isochronous pipes. End designers should
 *                  implement callback routine as necessary.
 *
 * Note:            None
 *******************************************************************/
void USBCB_SOF_Handler(void)
{
    // No need to clear UIRbits.SOFIF to 0 here.
    // Callback caller is already doing that.
}

/*******************************************************************
 * Function:        void USBCBErrorHandler(void)
 *
 * PreCondition:    None
 *
 * Input:           None
 *
 * Output:          None
 *
 * Side Effects:    None
 *
 * Overview:        The purpose of this callback is mainly for
 *                  debugging during development. Check UEIR to see
 *                  which error causes the interrupt.
 *
 * Note:            None
 *******************************************************************/
void USBCBErrorHandler(void)
{
    // No need to clear UEIR to 0 here.
    // Callback caller is already doing that.

	// Typically, user firmware does not need to do anything special
	// if a USB error occurs.  For example, if the host sends an OUT
	// packet to your device, but the packet gets corrupted (ex:
	// because of a bad connection, or the user unplugs the
	// USB cable during the transmission) this will typically set
	// one or more USB error interrupt flags.  Nothing specific
	// needs to be done however, since the SIE will automatically
	// send a "NAK" packet to the host.  In response to this, the
	// host will normally retry to send the packet again, and no
	// data loss occurs.  The system will typically recover
	// automatically, without the need for application firmware
	// intervention.
	
	// Nevertheless, this callback function is provided, such as
	// for debugging purposes.
}


/*******************************************************************
 * Function:        void USBCBCheckOtherReq(void)
 *
 * PreCondition:    None
 *
 * Input:           None
 *
 * Output:          None
 *
 * Side Effects:    None
 *
 * Overview:        When SETUP packets arrive from the host, some
 * 					firmware must process the request and respond
 *					appropriately to fulfill the request.  Some of
 *					the SETUP packets will be for standard
 *					USB "chapter 9" (as in, fulfilling chapter 9 of
 *					the official USB specifications) requests, while
 *					others may be specific to the USB device class
 *					that is being implemented.  For example, a HID
 *					class device needs to be able to respond to
 *					"GET REPORT" type of requests.  This
 *					is not a standard USB chapter 9 request, and 
 *					therefore not handled by usb_device.c.  Instead
 *					this request should be handled by class specific 
 *					firmware, such as that contained in usb_function_hid.c.
 *
 * Note:            None
 *******************************************************************/
void USBCBCheckOtherReq(void)
{
	usb_pc_detected = true;
}//end


/*******************************************************************
 * Function:        void USBCBStdSetDscHandler(void)
 *
 * PreCondition:    None
 *
 * Input:           None
 *
 * Output:          None
 *
 * Side Effects:    None
 *
 * Overview:        The USBCBStdSetDscHandler() callback function is
 *					called when a SETUP, bRequest: SET_DESCRIPTOR request
 *					arrives.  Typically SET_DESCRIPTOR requests are
 *					not used in most applications, and it is
 *					optional to support this type of request.
 *
 * Note:            None
 *******************************************************************/
void USBCBStdSetDscHandler(void)
{
    // Must claim session ownership if supporting this request
}//end


/*******************************************************************
 * Function:        void USBCBInitEP(void)
 *
 * PreCondition:    None
 *
 * Input:           None
 *
 * Output:          None
 *
 * Side Effects:    None
 *
 * Overview:        This function is called when the device becomes
 *                  initialized, which occurs after the host sends a
 * 					SET_CONFIGURATION (wValue not = 0) request.  This 
 *					callback function should initialize the endpoints 
 *					for the device's usage according to the current 
 *					configuration.
 *
 * Note:            None
 *******************************************************************/
void USBCBInitEP(void)
{
    USBEnableEndpoint(USBGEN_EP_NUM,USB_OUT_ENABLED|USB_IN_ENABLED|USB_HANDSHAKE_ENABLED|USB_DISALLOW_SETUP);
    USBOutHandle = USBRxOnePacket(USBGEN_EP_NUM,(BYTE*)&usb_rx_buffer,USBGEN_EP_SIZE);
}

/********************************************************************
 * Function:        void USBCBSendResume(void)
 *
 * PreCondition:    None
 *
 * Input:           None
 *
 * Output:          None
 *
 * Side Effects:    None
 *
 * Overview:        The USB specifications allow some types of USB
 * 					peripheral devices to wake up a host PC (such
 *					as if it is in a low power suspend to RAM state).
 *					This can be a very useful feature in some
 *					USB applications, such as an Infrared remote
 *					control	receiver.  If a user presses the "power"
 *					button on a remote control, it is nice that the
 *					IR receiver can detect this signalling, and then
 *					send a USB "command" to the PC to wake up.
 *					
 *					The USBCBSendResume() "callback" function is used
 *					to send this special USB signalling which wakes 
 *					up the PC.  This function may be called by
 *					application firmware to wake up the PC.  This
 *					function should only be called when:
 *					
 *					1.  The USB driver used on the host PC supports
 *						the remote wakeup capability.
 *					2.  The USB configuration descriptor indicates
 *						the device is remote wakeup capable in the
 *						bmAttributes field.
 *					3.  The USB host PC is currently sleeping,
 *						and has previously sent your device a SET 
 *						FEATURE setup packet which "armed" the
 *						remote wakeup capability.   
 *
 *					This callback should send a RESUME signal that
 *                  has the period of 1-15ms.
 *
 * Note:            Interrupt vs. Polling
 *                  -Primary clock
 *                  -Secondary clock ***** MAKE NOTES ABOUT THIS *******
 *                   > Can switch to primary first by calling USBCBWakeFromSuspend()
 *
 *                  The modifiable section in this routine should be changed
 *                  to meet the application needs. Current implementation
 *                  temporary blocks other functions from executing for a
 *                  period of 1-13 ms depending on the core frequency.
 *
 *                  According to USB 2.0 specification section 7.1.7.7,
 *                  "The remote wakeup device must hold the resume signaling
 *                  for at lest 1 ms but for no more than 15 ms."
 *                  The idea here is to use a delay counter loop, using a
 *                  common value that would work over a wide range of core
 *                  frequencies.
 *                  That value selected is 1800. See table below:
 *                  ==========================================================
 *                  Core Freq(MHz)      MIP         RESUME Signal Period (ms)
 *                  ==========================================================
 *                      48              12          1.05
 *                       4              1           12.6
 *                  ==========================================================
 *                  * These timing could be incorrect when using code
 *                    optimization or extended instruction mode,
 *                    or when having other interrupts enabled.
 *                    Make sure to verify using the MPLAB SIM's Stopwatch
 *                    and verify the actual signal on an oscilloscope.
 *******************************************************************/
void USBCBSendResume(void)
{
    static WORD delay_count;
    
    USBResumeControl = 1;                // Start RESUME signaling
    
    delay_count = 1800U;                // Set RESUME line for 1-13 ms
    do
    {
        delay_count--;
    }while(delay_count);
    USBResumeControl = 0;
}

