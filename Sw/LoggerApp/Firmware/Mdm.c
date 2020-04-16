/******************************************************************************
** File:	Mdm.c	
**
** Notes:	Modem comms
*/

/* CHANGES
** v2.36 281010 PB 	Remove input parameter of MDM_log_use as it is always called at midnight
**
** v2.45 130111 PB  Add "AT+COPS=0\r" to modem script to switch to home network
**
** V2.50 030211 PB  DEL119 - adjustments to MDM_ready() to give it a parameter to indicate test or not
**                  if true, will not power cycle but just go to ON if fails to register
**                  modem does not need to be registered if doing SIGTEST or GSMTEST
**
** V2.56 240211 PB  Call COM_schedule_check_dflags instead of CMD_check_dirty_flags, in case we are doing a signal test
**
** V2.59 020311 PB  clear mdm_power_cycled flag in MDM_shut_down()
**
** V2.65 070411 PB  added conditional compilation ifndef HDW_PRIMELOG_PLUS
**
** V2.90 090811 PB  DEL152 - new modem ready state MDM_TEST_WAIT for a 10 second delay before ready for sigtst and nwtst
**
** V3.01 140612 PB  removed MDM_retry()
**
** V3.06 050912 PB  add test for modem off after a new day has started in MDM_power_off() - avoids negative modem on durations
**
** V3.26 180613 PB  Remove CFS_FAILED_TO_OPEN state 
**
** V3.28 030713 PB	DEL192 - if modem already off, do not update modem on time in MDM_power_off()
**
** V3.29 171013 PB  bring up to date with Xilog+ V3.05
**					use new log state LOG_BATT_DEAD instead of LOG_VOLTAGE_TOO_LOW
**					call PWR_set_pending_batt_test() when ignite modem
**
** V3.31 141113 PB  in MDM_task state MDM_CONFIG shut down if file system not open
*/

#include <string.h>
#include <stdio.h>

#include "custom.h"
#include "compiler.h"
#include "str.h"
#include "HardwareProfile.h"
#include "MDD File System/SD-SPI.h"
#include "MDD File System/FSIO.h"

#include "Cfs.h"
#include "Tim.h"
#include "Rtc.h"
#include "Slp.h"
#include "Usb.h"
#include "log.h"
#include "pdu.h"
#include "Ana.h"
#include "Dig.h"
#include "cmd.h"
#include "msg.h"
#include "com.h"
#include "pwr.h"

#define extern
#include "Mdm.h"
#undef extern

#ifndef HDW_PRIMELOG_PLUS

// Define the baud rate constants
#ifdef HDW_MODEM_GE864
#define BAUDRATE2       19200UL
#else								// GL865 / UL865
#define BAUDRATE2       115200UL
#endif

#define BRG_DIV2        4
#define BRGH2           1

//#define BAUDRATEREG2        (((GetSystemClock()/2)+(BRG_DIV2/2*BAUDRATE2))/BRG_DIV2/BAUDRATE2-1)
//#define BAUD_ACTUAL         ((GetSystemClock()/2)/BRG_DIV2/(BAUDRATEREG2+1))
//#define BAUD_ERROR          ((BAUD_ACTUAL > BAUDRATE2) ? BAUD_ACTUAL-BAUDRATE2 : BAUDRATE2-BAUD_ACTUAL)
//#define BAUD_ERROR_PRECENT  ((BAUD_ERROR*100+BAUDRATE2/2)/BAUDRATE2)

#if (BAUD_ERROR_PRECENT > 3)
    #error UART frequency error is worse than 3%
#elif (BAUD_ERROR_PRECENT > 2)
    #warning UART frequency error is worse than 2%
#endif

#define MDM_POWER_CYCLE_TIME_X20MS	(5 * 50)
#define MDM_SIGN_ON_TIME_X20MS		(40 * 50)

#define MDM_CMD_TIMEOUT_X20MS	150		// 3s universal timeout for all commands
#define MDM_CMD_DELAY_X20MS		10		// allow 200ms between commands

// no echo, standby using DTR
#ifndef HDW_ATEX
#define MDM_INIT_COMMANDS_LENGTH	2
const char * const mdm_init_commands[MDM_INIT_COMMANDS_LENGTH] =
{
	"ATE0\r", "AT+CFUN=5\r"
};
#else	// ATEX logger
#define MDM_INIT_COMMANDS_LENGTH	3
const char * const mdm_init_commands[MDM_INIT_COMMANDS_LENGTH] =
{
	"ATE0\r", "AT+CFUN=5\r", "AT#MSCLASS=1,1"
};
#endif

/*****************************************************************************/

bool mdm_power_cycled;

char mdm_input_char;

uint16 mdm_rx_index;

uint32 mdm_total_use;
uint32 mdm_time_on;

int mdm_20ms_timer;

int mdm_line_number;	// used for scripts, and as index into mdm_init_commands

uint8 mdm_wait_count;
uint8 mdm_previous_state_flag;

char *mdm_monitor_ptr;

const char mdm_config_script[] = "mdmcfg.ats";
const char mdm_config_script_done[] = "mdmdone.ats";

FAR RTC_type mdm_log_time;

/******************************************************************************/
// local prototypes

void mdm_on(void);
void mdm_ignite(void);

/******************************************************************************
** Function:	Modem serial receive interrupt
**
** Notes:	
*/
void __attribute__((__interrupt__, no_auto_psv)) _U2RXInterrupt(void)
{
    mdm_input_char = U2RXREG;
    _U2RXIF = false;

	MDM_rx_buffer[mdm_rx_index] = mdm_input_char;
	if (mdm_rx_index < sizeof(MDM_rx_buffer) - 1)
		mdm_rx_index++;
	MDM_rx_buffer[mdm_rx_index] = '\0';				// string terminate it
}
#endif
/******************************************************************************
** Function:	Change in time within same day action
**
** Notes:		
*/
void MDM_change_time(uint32 old_time_sec, uint32 new_time_sec)
{
#ifndef HDW_PRIMELOG_PLUS
	// if modem on
	if (HDW_MODEM_PWR_ON_N == false)
	{
		// total use += old time - time on
		mdm_total_use += (old_time_sec - mdm_time_on);
		// time on = new time
		mdm_time_on = new_time_sec;
	}
#endif
}

/******************************************************************************
** Function:	recalculate wakeup time if active
**
** Notes:		called when wakeup times have to recalculated after time change
**				MDM_wakeup_time is never set to more than 8 seconds after time now
*/
void MDM_recalc_wakeup(void)
{
#ifndef HDW_PRIMELOG_PLUS
	// if modem time is in use
	if (MDM_wakeup_time != SLP_NO_WAKEUP)
	{
		// ensure we wakeup soon (wait time may be extended - worst case 16 seconds)
		MDM_wakeup_time = RTC_time_sec + 8;
	}
#endif
}

/******************************************************************************
** Function:	preset state flag to modem condition
**
** Notes:		called by #dt or #tc to remember state of modem before a time and date change
**				bit 0 is state of modem
**				bit 1 indicates already set 
*/
void MDM_preset_state_flag(void)
{
#ifndef HDW_PRIMELOG_PLUS
	// modem on if HW line low
	mdm_previous_state_flag = HDW_MODEM_PWR_ON_N ? 0x10 : 0x11;
#endif
}

/******************************************************************************
** Function:	log modem use in daily usage file
**
** Notes:		return false if file system not ready
**				this is called only at midnight so can use fixed value for time now
*/
bool MDM_log_use(void)
{
#ifndef HDW_PRIMELOG_PLUS
	int     len;

	if (LOG_state == LOG_BATT_DEAD)										// All writes to SD disabled if battery flat
		return true;													// pretend it succeeded

	if (!CFS_open())													// else waiting for file system
		return false;

	if ((mdm_previous_state_flag & 0x10) == 0x00)						// update state flag if not already set by a time change from #dt or #tc
		MDM_preset_state_flag();
	
	if ((mdm_previous_state_flag & 0x01) == 0x01)						// if modem was previously on
		mdm_total_use += (RTC_SEC_PER_DAY - mdm_time_on);				// tot up total seconds

	mdm_previous_state_flag = 0x00;										// clear state flag
	if (HDW_MODEM_PWR_ON_N == false)									// if modem is now on
	{
		mdm_time_on = 0;												// reset start time
		mdm_previous_state_flag = 0x01;									// set state flag
	}	

	sprintf(STR_buffer, "\\ACTIVITY\\20%02X\\%02X", 					// create path to daily use file
			mdm_log_time.yr_bcd, 
			mdm_log_time.mth_bcd);

	sprintf(&STR_buffer[128], "ACT-%02X%02X.TXT", 						// Generate logged activity filename, e.g. ACT-2705.TXT:
			mdm_log_time.day_bcd, 
			mdm_log_time.mth_bcd);

	len = sprintf(&STR_buffer[256], "modem %ld sec\r\n", mdm_total_use);
	CFS_write_file(STR_buffer, &STR_buffer[128], "a", &STR_buffer[256], len); 

	memcpy(&mdm_log_time, &RTC_now, sizeof(RTC_type));					// set use log time to new day
#endif
	return true;
}

#ifndef HDW_PRIMELOG_PLUS
/******************************************************************************
** Function:	Transmit string to modem, monitor it & log it
**
** Notes:	
*/
void mdm_tx_string(char *s)
{
	MDM_tx_ptr = s;
	USB_monitor_string(s);
}

/******************************************************************************
** Function:	clear modem receive buffer
**
** Notes:		
*/
void MDM_clear_rx_buffer(void)
{
	MDM_rx_buffer[0] = '\0';
	mdm_rx_index = 0;
}
#endif
/******************************************************************************
** Function:	Send a new command to modem
**
** Notes:		Must be terminated with '\r'. '\n' optional. Sets up retry
**				mechanism for 1 retry
*/
void MDM_send_cmd(char *p)
{
#ifndef HDW_PRIMELOG_PLUS
	mdm_tx_string(p);
	MDM_rx_buffer[0] = '\0';
	mdm_rx_index = 0;
	mdm_monitor_ptr = MDM_rx_buffer;
	
	MDM_retry_ptr = p;
	MDM_cmd_status = MDM_CMD_BUSY;
	MDM_cmd_timer_x20ms = MDM_CMD_TIMEOUT_X20MS;

	// ensure minimum delay before tx
	if (MDM_tx_delay_timer_x20ms < MDM_CMD_DELAY_X20MS)
		MDM_tx_delay_timer_x20ms = MDM_CMD_DELAY_X20MS;
#endif
}

/******************************************************************************
** Function:	Poll for response to modem command
**
** Notes:		Called as often as required.
**				Returns true if command in progress else false
*/
bool MDM_cmd_in_progress(void)
{
#ifndef HDW_PRIMELOG_PLUS
	char * p;

	if (MDM_cmd_status != MDM_CMD_BUSY)
		return false;

	do
	{
		p = strstr(mdm_monitor_ptr, "\r\n");
		if (p == NULL)										// no new line
			break;

		if (strncmp(mdm_monitor_ptr, "OK\r\n", 4) == 0)		// received "OK\r\n"
		{
			MDM_cmd_status = MDM_CMD_SUCCESS;
			MDM_tx_delay_timer_x20ms = MDM_CMD_DELAY_X20MS;	// delay before next cmd
			USB_monitor_string(MDM_rx_buffer);
			return false;									// not busy
		}
		else if (strstr(mdm_monitor_ptr, "ERROR"))			// error anywhere in received line
			MDM_cmd_timer_x20ms = 0;						// force immediate timeout

		mdm_monitor_ptr = p + 2;

	} while (true);

	if (MDM_cmd_timer_x20ms == 0)
	{
		USB_monitor_string(MDM_rx_buffer);
		if (MDM_retry_ptr == NULL)							// this was the second attempt
		{
			MDM_cmd_status = MDM_CMD_FAILED;				// finished but timed out
			return false;
		}
		// else

		MDM_tx_delay_timer_x20ms = MDM_CMD_DELAY_X20MS;		// delay before retry
		mdm_tx_string(MDM_retry_ptr);						// try again
		MDM_retry_ptr = NULL;								// but this is the last chance
		MDM_cmd_timer_x20ms = MDM_CMD_TIMEOUT_X20MS;
	}
#endif
	return true;											// still in progress
}

#ifndef HDW_PRIMELOG_PLUS
/******************************************************************************
** Function:	Execute configuration script and/or exec script
**
** Notes:		In Com.c, \Config\mdmcfg.ats gets executed once then renamed to mdmdone.ats.
**				
*/
void mdm_config(void)
{
	mdm_line_number = 1;
	MDM_state = MDM_CONFIG;
}

/******************************************************************************
** Function:	Do next line of a script file
**
** Notes:		Must have CFS_open on entry. Returns false when finished or error. 
**				Sets mdm_line_number as follows:
**				0 if file doesn't exist, else next line to be executed.
*/
bool mdm_do_next_line(const char *filename)
{
	int i;

	i = CFS_read_line((char *)CFS_config_path, (char *)filename, mdm_line_number++,
					  MDM_tx_buffer, sizeof(MDM_tx_buffer));
	
	if ((i > 0) && (i < sizeof(MDM_tx_buffer) - 1))	// got a line
	{
		// terminate command with \r
		MDM_tx_buffer[i++] = '\r';
		MDM_tx_buffer[i] = '\0';
		MDM_send_cmd(MDM_tx_buffer);
		MDM_cmd_timer_x20ms = 30 * 50;		// Give command 30s timeout
		return true;
	}
	// else									// no script, or finished config commands

	if (mdm_line_number == 2)				// we failed to get the first line
		mdm_line_number = 0;

	return false;
}

/******************************************************************************
** Function:	Power-cycle modem
**
** Notes:		Returns false if it's already tried
*/
bool mdm_power_cycle(void)
{
	MDM_power_off();
	if (mdm_power_cycled)
		return false;
	// else:

	mdm_20ms_timer = MDM_POWER_CYCLE_TIME_X20MS;
	MDM_state = MDM_POWER_CYCLE;
	return true;
}
#endif
/******************************************************************************
** Function:	Modem power-up, sign-on & power-down state machine
**
** Notes:		flag ignore_registration set true if sigtst or gsmtst calls it
**				modem should not power down if registration fails
**              as tests can continue without registration
*/
bool MDM_ready(bool ignore_registration)
{
#ifndef HDW_PRIMELOG_PLUS
	FSFILE *f;
	char *p;

	switch (MDM_state)
	{
	case MDM_ON:
		return true;																// ready!
		// break;

	case MDM_OFF:							// Batteries should have maximum time to recover before voltages read, so																	// read just before modem powers up
		if (!PWR_measure_in_progress())
			mdm_on();
		break;

	case MDM_IGNITE:
		if (mdm_20ms_timer == 0)
		{
			mdm_ignite();
			PWR_set_pending_batt_test(false);
		}
		break;

	case MDM_IGNITION:
		if (mdm_20ms_timer == 0)
		{
			HDW_MODEM_IGNITION = false;												// end ignition pulse
			MDM_state = MDM_CHECK_POWER;
			mdm_20ms_timer = 2 * 50;												// allow 2s to get PWRMON high
		}
		break;

	case MDM_CHECK_POWER:
		if (HDW_MODEM_2V8_MON)														// ignition successful
		{
			MDM_wakeup_time = RTC_time_sec + 6;										// go to sleep for 6s to allow modem to wake up
			MDM_state = MDM_INITIALISE;
		}
		else if (mdm_20ms_timer == 0)												// timeout - power cycle modem
		{
			if (!mdm_power_cycle())
			{
				LOG_enqueue_value(LOG_ACTIVITY_INDEX, LOG_MDM_FILE, __LINE__);		// Modem ignition failed
				MDM_state = MDM_FAIL_TO_REGISTER;
			}
		}
		break;

	case MDM_INITIALISE:
		if (MDM_wakeup_time <= RTC_time_sec)										// check sleep time has expired
		{
			if (CFS_open())
			{
				MDM_wakeup_time = SLP_NO_WAKEUP;
				mdm_line_number = 0;												// send initialisation commands:
				MDM_state = MDM_INITIALISING;
				MDM_cmd_status = MDM_CMD_SUCCESS;									// start command sequence
			}
		}
		break;

	case MDM_INITIALISING:
		// Ignore initialisation commands which can't be executed.
		if (!MDM_cmd_in_progress())
		{
			// ignore whether command result is success or not
			if (mdm_line_number < MDM_INIT_COMMANDS_LENGTH)							// send next command
			{
				MDM_send_cmd((char *)mdm_init_commands[mdm_line_number++]);
				MDM_tx_delay_timer_x20ms = 25;		// wait 500ms before Tx
				MDM_cmd_timer_x20ms = 5 * 50;		// then 5s timeout
				mdm_20ms_timer = 5 * 50;			// allow 5s to get reply
			}
			else		// Select network operator
			{
				// If NW ID is non-0, use specified NW
				// else if roaming enabled, force a roam
				// else use default NW
				if (COM_gsm_network_id != 0)		// use specified NW
				{
					sprintf(MDM_tx_buffer, "AT+COPS=1,2,%lu\r", COM_gsm_network_id);
					MDM_send_cmd(MDM_tx_buffer);
				}
				else								// use home NW or roam
					MDM_send_cmd("AT+COPS=0,2\r");	// ensure ID reported numerically
	
				MDM_tx_delay_timer_x20ms = 25;		// wait 500ms before Tx
				MDM_cmd_timer_x20ms = 5 * 50;		// then 5s timeout
				mdm_20ms_timer = 5 * 50;			// allow 5s to get reply
				MDM_state = MDM_SELECT_OPERATOR;
			}
		}
		break;

	case MDM_SELECT_OPERATOR:
		// Essential that AT+COPS is executed - however, sometimes gives an error, so ignore success/failure
		if (!MDM_cmd_in_progress())
		{
			if (!ignore_registration)
			{
				if (MDM_pin[0] == '\0')				// we have no PIN in logger
					mdm_config();
				else								// see if SIM needs a PIN
				{
					MDM_send_cmd("AT+CPIN?\r");
					MDM_state = MDM_CHECK_PIN;
				}
			}
			else									// ignore registration
			{
				mdm_20ms_timer = 10 * 50;			// set 10s wait for modem to register
				MDM_state = MDM_TEST_WAIT;
			}
		}
		break;

	case MDM_TEST_WAIT:
		if (mdm_20ms_timer == 0)					// wait here for timeout
		{
			MDM_state = MDM_ON;
			return true;
		}
		break;

	case MDM_CHECK_PIN:
		if (!MDM_cmd_in_progress())
		{
			if ((MDM_cmd_status == MDM_CMD_SUCCESS) && !strstr(MDM_rx_buffer, "READY"))	// PIN required
			{
				sprintf(MDM_tx_buffer, "AT+CPIN=%s\r", MDM_pin);
				MDM_send_cmd(MDM_tx_buffer);
				MDM_cmd_timer_x20ms = 20 * 50;				// PIN recognition takes a while
				MDM_retry_ptr = NULL;						// only one attempt
				MDM_state = MDM_SEND_PIN;
			}
			else	// no PIN required or command failed - see if we can sign on anyway
				mdm_config();
		}
		break;

	case MDM_SEND_PIN:
		if (!MDM_cmd_in_progress())
		{
			if (MDM_cmd_status == MDM_CMD_SUCCESS)			// got OK, so sign on now
				mdm_config();
			else											// only 1 attempt each time
			{
				//EVENT_LogEvent(eWrongSIMCardPin);
				MDM_power_off();
			}
		}
		break;

	case MDM_CONFIG:
		if (!MDM_cmd_in_progress())
		{
			if (!mdm_do_next_line(mdm_config_script))
			{
				if (mdm_line_number > 0)											// script must exist, so rename
				{
					if (CFS_state == CFS_OPEN)
					{																					// Working folder has already been set by cfs_open_file
						FSremove(mdm_config_script_done);								// Remove any old ones prior to renaming this
						f = FSfopen(mdm_config_script, "r");
						if (f != NULL)
						{
							FSrename(mdm_config_script_done, f);
							CFS_close_file(f);
						}
					}
				}

				mdm_line_number = 1;
				MDM_state = MDM_PRE_REG;
			}
		}
		break;

	case MDM_PRE_REG:
		if (!MDM_cmd_in_progress())
		{
			if (!mdm_do_next_line("prereg.ats"))
			{
				mdm_line_number = 0;
				MDM_state = MDM_SIGN_ON_WAIT;
				MDM_wakeup_time = RTC_time_sec + 8;									// go to sleep for 8 seconds

				// If roaming, allow longer to sign on
				mdm_wait_count = (COM_roaming_enabled && (COM_gsm_network_id == 0)) ? 30 : 6;

				MDM_cmd_timer_x20ms = 0;											// force immediate send of command
				MDM_rx_buffer[0] = '\0';											// suppress echo of previous "OK" if echo enabled
				MDM_cmd_status = MDM_CMD_SUCCESS;
			}
		}
		break;

	case MDM_SIGN_ON_WAIT:
		if (MDM_wakeup_time <= RTC_time_sec)										// check sleep time has expired
		{
			if (CFS_open())
			{
				MDM_wakeup_time = SLP_NO_WAKEUP;
				MDM_send_cmd("AT+CREG?\r");
				MDM_state = MDM_SIGNING_ON;
				mdm_20ms_timer = 50;
			}
		}
		break;

	case MDM_SIGNING_ON:
		if (!MDM_cmd_in_progress())
		{
			if (strstr(MDM_rx_buffer, "0,1") || strstr(MDM_rx_buffer, "0,5"))		// registered
			{
				mdm_line_number = 1;
				MDM_state = MDM_POST_REG;
				mdm_20ms_timer = 0;
			}
			else if (mdm_wait_count == 0)
			{
				if (ignore_registration)											// if test don't care if sign on or not
				{
					MDM_state = MDM_ON;
					return true;
				}
				else if (!mdm_power_cycle())
				{
					LOG_enqueue_value(LOG_ACTIVITY_INDEX, LOG_MDM_FILE, __LINE__);	// Modem sign-on failed
					if (COM_roaming_enabled)
						COM_gsm_network_id = 0;										// roam next time
					// else stay locked to the configured network, or the home network
					MDM_state = MDM_FAIL_TO_REGISTER;
				}
			}
			else if (mdm_20ms_timer == 0)	// keep retrying until we get it
			{
				MDM_state = MDM_SIGN_ON_WAIT;
				MDM_wakeup_time = RTC_time_sec + 8;	// go to sleep for 8 seconds
				mdm_wait_count--;
			}
		}
		break;

	case MDM_POST_REG:
		if (!MDM_cmd_in_progress())
		{
			if (!mdm_do_next_line("postreg.ats"))
			{
				if (COM_roaming_enabled && (COM_gsm_network_id == 0))
				{
					// read the current operator:
					mdm_20ms_timer = 0;					// send command
					mdm_wait_count = 3;					// 3 attempts to read operator
					MDM_state = MDM_GET_OPERATOR;
				}
				else									// no need to read operator
				{
					MDM_state = MDM_ON;
					return true;
				}
			}
		}
		break;

	case MDM_GET_OPERATOR:
		if (mdm_20ms_timer == 0)							// send command
		{
			if (mdm_wait_count > 0)
			{
				mdm_wait_count--;							// next attempt
				MDM_send_cmd("AT+COPS?\r");
				mdm_20ms_timer = 3 * 50;					// at 3s intervals
			}
			else		// can't read operator - leave as 0.
			{
				MDM_state = MDM_ON;
				return true;
			}
		}
		else if (!MDM_cmd_in_progress())
		{
			if (MDM_cmd_status == MDM_CMD_SUCCESS)
			{
				p = strchr(MDM_rx_buffer, '\"');
				if (p != NULL)
				{
					if (sscanf(++p, "%ld", &COM_gsm_network_id) != 1)
						COM_gsm_network_id = 0;							// try again when 20ms timer expires
				}

				if (COM_gsm_network_id != 0)
				{
					MDM_state = MDM_ON;
					return true;
				}
			}
		}
		break;

	case MDM_POWER_CYCLE:
		if (mdm_20ms_timer == 0)
		{
			mdm_on();
			mdm_power_cycled = true;
		}
		break;

	case MDM_SHUTTING_DOWN:
		if (mdm_20ms_timer == 0)
		{
			if (CFS_open())
			{
				COM_schedule_check_dflags();
				MDM_power_off();
				return true;
			}
		}
		break;

	case MDM_FAIL_TO_REGISTER:
		return true;
		break;

	default:
		MDM_power_off();
		LOG_enqueue_value(LOG_ACTIVITY_INDEX, LOG_MDM_FILE, __LINE__);	// fault assert
		break;
	}
#endif
	return false;	// busy by default
}

/******************************************************************************
** Function:	modem can sleep
**
** Notes:		true if modem state in INITIALISE or SIGN ON WAIT and wake up time is set
*/
bool MDM_can_sleep(void)
{
#ifndef HDW_PRIMELOG_PLUS
	return (((MDM_state == MDM_INITIALISE) || (MDM_state == MDM_SIGN_ON_WAIT)) && (MDM_wakeup_time != SLP_NO_WAKEUP));
#else
	return true;
#endif
}

/******************************************************************************
** Function:	Remove modem power
**
** Notes:		Leave modem & UART switched off
**				DEL192 - if modem already off, do not update modem on time
*/
void MDM_power_off(void)
{
#ifndef HDW_PRIMELOG_PLUS
	if (HDW_MODEM_PWR_ON_N == false)											// if modem power is on
	{																			// update modem on time
		if (mdm_time_on <= RTC_time_sec)										// test for positive modem on time
			mdm_total_use += (RTC_time_sec - mdm_time_on);
		else
			mdm_total_use += (RTC_SEC_PER_DAY + RTC_time_sec - mdm_time_on);	// this catches case where a new day has started
	}
	HDW_MODEM_DTR_N = false;
	HDW_MODEM_PWR_ON_N = true;													// turn modem off
	CNEN4bits.CN48IE = 0;														// disengage CN48 interrupt
	HDW_MODEM_IGNITION = false;
	SLP_set_required_clock_speed();
	USB_monitor_prompt("Modem power OFF");
	LOG_enqueue_value(LOG_ACTIVITY_INDEX, LOG_MDM_FILE, __LINE__);				// Modem power OFF
	U2MODEbits.UARTEN = false;
	MDM_tx_ptr = NULL;
	MDM_state = MDM_OFF;
#endif
}

/******************************************************************************
** Function:	Normal modem shutdown
**
** Notes:	
*/
void MDM_shutdown(void)
{
#ifndef HDW_PRIMELOG_PLUS
	HDW_MODEM_IGNITION = true;			// start shutdown pulse
	mdm_20ms_timer = 2 * 50;			// 2s shutdown pulse
	MDM_state = MDM_SHUTTING_DOWN;
	mdm_power_cycled = false;
#endif
}

#ifndef HDW_PRIMELOG_PLUS
/******************************************************************************
** Function:	Switch modem power on
**
** Notes:
*/
void mdm_on(void)
{
	HDW_MODEM_DTR_N = false;			// modem UART not in standby
	HDW_MODEM_PWR_ON_N = false;			// apply power
	// engage CN48 interrupt
	CNEN4bits.CN48IE = 1;
	mdm_20ms_timer = 3;					// 40-60ms delay
	MDM_state = MDM_IGNITE;				// before ignition pulse
}

/******************************************************************************
** Function:	Switch UART on, ignite modem, & start sequence to check registration
**
** Notes:
*/
void mdm_ignite(void)
{
	SLP_set_required_clock_speed();
	HDW_MODEM_IGNITION = true;			// start ignition pulse
	USB_monitor_prompt("Modem power ON");
	LOG_enqueue_value(LOG_ACTIVITY_INDEX, LOG_MDM_FILE, __LINE__);	// Modem power ON
	mdm_time_on = RTC_time_sec;

	mdm_20ms_timer = 52;				// 1s+ ignition pulse
	MDM_state = MDM_IGNITION;

	HDW_MODEM_DTR_N = false;			// modem UART not in standby

	// Configure UART
	U2BRG = (((GetSystemClock() / 2) + (BRG_DIV2 / 2 * BAUDRATE2)) / BRG_DIV2 / BAUDRATE2 - 1);
    U2MODE = 0;
    U2MODEbits.BRGH = BRGH2;
	U2MODEbits.UEN = 2;					// CTS/RTS flow control
    U2STA = 0;
    U2MODEbits.UARTEN = true;
    U2STAbits.UTXEN = true;
    IFS1bits.U2RXIF = 0;

	mdm_rx_index = 0;
	mdm_monitor_ptr = MDM_rx_buffer;
	IEC1bits.U2RXIE = true;
}
#endif
/******************************************************************************
** Function:	See if OK to send next string to modem
**
** Notes:		Returns true if buffer empty
*/
bool MDM_clear_to_send(void)
{
#ifndef HDW_PRIMELOG_PLUS
	return (MDM_tx_ptr == NULL);
#else
	return false;
#endif
}

/******************************************************************************
** Function:	Modem task
**
** Notes:	
*/
void MDM_task(void)
{
#ifndef HDW_PRIMELOG_PLUS
	if (TIM_20ms_tick)
	{
		if (mdm_20ms_timer != 0)
			mdm_20ms_timer--;

		if (MDM_tx_delay_timer_x20ms != 0)
			MDM_tx_delay_timer_x20ms--;
		else if (MDM_cmd_timer_x20ms != 0)	// don't start cmd_timer until delay timer == 0
			MDM_cmd_timer_x20ms--;
	}
	
	// If we have a char to tx, check if tx buffer free & sufficient delay between commands
	if ((MDM_tx_ptr != NULL) && !U2STAbits.UTXBF && (MDM_tx_delay_timer_x20ms == 0))
	{
		if (HDW_MODEM_DTR_N)						// modem UART currently in standby
		{
			HDW_MODEM_DTR_N = false;
			if (MDM_tx_delay_timer_x20ms < 9)
				MDM_tx_delay_timer_x20ms = 9;		// must be low for at least 150ms before comms
		}
		else
		{
			U2TXREG = *MDM_tx_ptr++;
			if (*MDM_tx_ptr == '\0')				// finished transmitting string
			{
				MDM_tx_ptr = NULL;
				if (MDM_cmd_timer_x20ms < MDM_CMD_TIMEOUT_X20MS)
					MDM_cmd_timer_x20ms = MDM_CMD_TIMEOUT_X20MS;
				// else leave timeout as-is
				MDM_cmd_status = MDM_CMD_BUSY;
			}
		}
	}
#endif
}

/******************************************************************************
** Function:	Modem initialisation
**
** Notes:	
*/
void MDM_init(void)
{
#ifndef HDW_PRIMELOG_PLUS
	HDW_MODEM_DTR_N = false;													// configure I/O and states
	HDW_MODEM_PWR_ON_N = true;
	CNEN4bits.CN48IE = 0;														// disengage CN48 interrupt
	HDW_MODEM_IGNITION = false;
	U2MODEbits.UARTEN = false;
	SLP_set_required_clock_speed();
	mdm_total_use = 0;
	MDM_tx_ptr = NULL;
	MDM_state = MDM_OFF;
	memcpy(&mdm_log_time, &RTC_now, sizeof(RTC_type));							// set use log time to now
	mdm_power_cycled = false;													// assure initial state
#endif
}

// eof

