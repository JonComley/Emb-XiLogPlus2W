/******************************************************************************
** File:	Com.c
**
** Notes:
*/

/* CHANGES
** v2.36 271010 PB  Increment ftp filename index before creating target filename
**					Add local int com_msg_body_index for stepping through message body
**					when sending FTP data
**
** v2.37 281010 PB 	New function MSG_remove() called when finished with a message
**                  Outgoing message does not reside in MSG_tx_buffer beyond this point
**					Message is always got from outbox before transmission with MSG_get_message_to_tx
**					This function now gets the youngest message in outbox
**
** v2.39 231110 PB  Do not do window transmission message if in batch mode - just send batch message(s)
**					New state COM_IDLE_WAIT to wait for LOG and PDU tasks to finish before proceeding
**					this will allow an sms batch message being generated at start of a window
**					also to be transmitted - solves 15 minute interval once per day batch messages issue
**
** V2.45 130111 PB  Add new states and code for gsm network test
**
** v2.48 190111 PB 	DEL100 - reverse v2.39 change above - always initiate tx_rx in transmission window
**
** V2.50 030211 PB  DEL119 - adjustments to MDM_ready() to give it a parameter to indicate test or not
**                  Adjustments to SIGTEST and GSMTEST state code to use MDM_ready correctly
**                  SIGTEST did not shut down when # received
**
** V2.51 040211 PB  change GSMxx to NWxx
**       090211 PB  New function COM_test_in_progress reports true if signal test or network test in progress
**
** V2.52 150211 PB  test for COM_build_info.modem flag or CM mode == 2 in com_idle_actions() and do not allow actions if set
**					SIGTST and NWTST should only turn the modem on after initial delay period
**					they also need to monitor abort at all times
**		 160211 PB	improvement to error reporting in NWTST: 'A' aborted, 'N' no network service, 'E' other error caused termination
**
** V2.56 240211 PB  New function COM_schedule_check_dflags - sets flag if in a signal test
**                  New flag com_pending_check_dflags - if set, end of signal test runs dirty flags check
**
** V2.59 010311 PB  DEL127 SIGTST expanded to ask for current network name and store it in file ACTIVITY\NETWORK.TXT
**
** V2.64 170311 PB  new local function com_create_logdata_filename and mod to com_create_extended_filename
**                  to take into account presence of site id string in creating ftp tx filenames for server's global outbox
**       220311 PB  Changes to COM_schedule_control() to ensure ftp polling is carried out on a retry
**
** V2.65 070411 PB  added conditional compilation ifndef HDW_PRIMELOG_PLUS
**                  alterations to ftp filename handling to account for contents of CONFIG\DETAILS.TXT
**                  the first line is added to the ftp filename if present (primarily for AGBAR requirements)
**
** V2.68 270411 PB  DEL129 - add test for ftp poll required to test for re-registration in COM_schedule_control()
**					DEL133 - put back "_logdata" in ftp filename in com_create_logdata_filename()
**                           new function com_create_message_filename for ftp messages
**
** V2.69 270411 PB  DEL137 - increase length of incoming FTP commands to 161 bytes to match SMS length + \r
**                  DEL136 - in COM_POLL_FTP_9, having received first line of the directory list from server,
**                           if MDM_rx_buffer does not contain NO CARRIER\r but does contain \r, reset rx_buffer
**                           this keeps the MDM_rx_buffer clear when a full directory listing would otherwise overflow it
**                  mods to tsync code to use a two line NTZLOGON.TXT file after a fixed context setting command
**
** V2.70 280411 PB  increase length of com_server_filename buffer to 128
**
** V2.71 040511 PB  added recalc of com_time.rx_delay if running to COM_recalc_wakeups()
**                  this solves the hangup in receive delay if time is changed during the 40 sec delay period
**
** V2.74 240511 PB  DEL144 - count number of bytes sent in ftp and compare with file size returned
**                           do retry if they do not match
**                           this applies to FTP_MSG, FTP_FILE, FTP_ALL_DATA and FTP_PART_DATA outbox message types
**       250511 PB  DEL143 - rework gprs nitz time sync to use a count down flag com_tsync_attempt_count rather than MSG_TYPE_GPRS_TIME
**                           new function com_gprs_tsync_abort_action() does not use MSG_remove
**
** V2.75 260511 PB  DEL143 - correction to com_gprs_fail_action() does not look for tsync states or use MSG_remove
**
** V2.76 260511 PB  DEL142 - changes to use two message outboxes, one for SMS and one for FTP messages -
**                           com_no_more_in_outbox() replaced with direct testing of MSG_files_in_sms/ftp_outbox global variable
**                           com_finished() and com_retry_message() only used for sms
**                           work in com_idle_actions and COM_task to ensure correct outbox is used in correct place
**                           add test of com_ftp_poll_required after sending all ftp messages from ftp outbox
**
** V2.81 260611 MA  GPRS=1 used for ftp and nitz time message
**  
** V2.82 280611 PB  merge of 2.76 code with 2.81 code
**
** V2.90 010811 PB  DEL142 - Create separate flags for pending ftp tx and sms tx as we now have separate outboxes
**
** V2.90 010811 PB  DEL142 - if get gprs fail or abort during ftp, need to check for SMS tx and rx, not just rx
**                           separated pending_tx_rx flag into two, and renamed and reworked all pending flags
**                           COM_initiate_tx() given a bool input parameter to select SMS or FTP 
**
** V3.00 220911 PB  Waste Water - make com_first_window_time and com_next_window_time global
**
** V3.04 050112 PB  Waste Water - implement extra standby window to run after every modem power up
**
** V3.15 190612 PB  Updates in line with Xilog+
**			        removed calls to MDM_retry()
**					when registration retry time past midnight or retries finished without success, stop trying
**					only set up retry registration if ftp messaging enabled
**
** V3.16 260612 PB  Updates in line with Xilog+ V3.01
**		 260612		add break after faults in COM_TX_FTP_DATA_1 state
**					remove com_read_sms() and replace with in line code
**					remove com_delete_current_sms() and replace with in-line code
**					replace calls to com_idle_actions() with state set to COM_IDLE_WAIT
**					improve COM_recalc_wakeups() actions
**
** V3.17 091012 PB  Bring up to date with Xilog+ V3.06 - new fn COM_long_interval()
**					Use of CFS_open() in com_send_ftp_logon(), COM_task()
** 					start 40 second SMS second shot timer when first log on in COM_RX_SMS_1 and COM_TX_SMS_1, whichever is sooner.
**					in COM_schedule_control() only set pending flags if window interval is compatible with use of internal or external power.
**					if on internal power, only intervals between 14 (15 min) and 24 (24 hour). If external, only between 9 (1 min) and 24.
** 					Correction to setting of second shot timer - don't go to RX_DELAY with long time delay
**					New state in COM_task() - COM_SD_TASK_FAILED
**
** V3.18 090113 PB  Bring up to date with Xilog+ NEW V3.03
** 					RX_DELAY not to be entered with SLP_NO_WAKEUP set in delay wake up time
**					In COM_RX_DELAY state exit immediately if no wait time set
**					Needs to gprs fail if times out when waiting to receive file in COM_RX_FTP_2, not move on to COM_RX_FTP_3 with no timeout set.
**
** V3.26 180613 PB  Remove CFS_FAILED_TO_OPEN state 
** 
** V3.29 171013 PB  Bring up to date with Xilog+ V3.05
**					in COM_task() state COM_SET_GPRSxx set gsm network id to zero if roaming and fail to get IP
**
** V3.31 131113 PB  reposition com_day_bcd = RTC_now.day_bcd at end of COM_task and in COM_init
**
** V3.32 201113 PB  return com_day_bcd = RTC_now.day_bcd to com_new_day_task()
*/

#include "Custom.h"
#include "Compiler.h"
#include "MDD File System/SD-SPI.h"
#include "MDD File System/FSIO.h"

#include "Msg.h"
#include "Mdm.h"
#include "Cfs.h"
#include "Tim.h"
#include "Str.h"
#include "rtc.h"
#include "Log.h"
#include "Usb.h"
#include "Pdu.h"
#include "Ana.h"
#include "Dig.h"
#include "Cmd.h"
#include "Cal.h"
#include "ftp.h"
#include "Pwr.h"
#include "HardwareProfile.h"
#include "Slp.h"
#include "tsync.h"

#define extern
#include "Com.h"
#undef extern

#define COM_NO_CARRIER_TIMEOUT_X20MS	(60 * 50)

#ifndef HDW_PRIMELOG_PLUS
// States:
#define COM_IDLE				0
#define COM_SHUTTING_DOWN		1
#define COM_TX_MSG				2
#define COM_TX_SMS_1			3
#define COM_TX_SMS_2			4
#define COM_CHECK_REGISTRATION	5
#define COM_RX_SMS_1			6
#define COM_CHECK_CSQ			7
#define COM_CHECK_CPMS_1		8
#define COM_CHECK_CPMS_2		9
#define COM_RX_SMS_3			10
#define COM_EXECUTE_SMS			11
#define COM_RX_DELAY			12
#define COM_REPEAT_RX_SMS_2		13
#define COM_DELETE_SMS			14
#define COM_QUERY_GPRS			15
#define COM_SET_GPRS_1			16
#define COM_SET_GPRS_2			17
#define COM_SET_GPRS_3			18
#define COM_TX_FTP_3			19
#define COM_TX_FTP_4			20
#define COM_TX_FTP_5			21
#define COM_TX_FTP_6			22
#define COM_TX_FTP_7			23
#define COM_TX_FTP_8			24
#define COM_TX_FTP_9			25
#define COM_TX_FTP_10			26
#define COM_TX_WAIT				27
#define COM_TX_FTP_MDM_WAIT		28
#define COM_TX_FTP_ABORT		29
#define COM_TX_FTP_CLOSE		30
#define COM_TX_FTP_DATA_1		31
#define COM_TX_FTP_DATA_2		32
#define COM_TX_FTP_DATA_MDM		33
#define COM_POLL_FTP_1			34
#define COM_POLL_FTP_4			35
#define COM_POLL_FTP_CWD		36
#define COM_POLL_FTP_6			37
#define COM_POLL_FTP_9			38
#define COM_RX_FTP_1			39
#define COM_RX_FTP_2			40
#define COM_RX_FTP_3			41
#define COM_RX_FTP_4			42
#define COM_TRANSFER_FTP_1		43
#define COM_TRANSFER_FTP_2		44
#define COM_TRANSFER_FTP_3		45
// Gap
#define COM_SIGNAL_TEST_0		51	// from here to COM_NW_TEST_REPLY MUST be consecutive for test in progress calc
#define COM_SIGNAL_TEST_1		52
#define COM_SIGNAL_TEST_2		53
#define COM_SIGNAL_TEST_3		54
#define COM_SIGNAL_TEST_4		55
#define COM_SIGNAL_TEST_5		56
#define COM_NW_TEST_0			57
#define COM_NW_CHECK_REG		58
#define COM_NW_TEST_1			59
#define COM_NW_TEST_2			60
#define COM_NW_TEST_3			61
#define COM_NW_TEST_4			62
#define COM_NW_TEST_5			63
#define COM_NW_TEST_6			64
#define COM_NW_TEST_7			65
#define COM_NW_TEST_8			66
#define COM_NW_TEST_9			67
#define COM_NW_TEST_EXIT		68
#define COM_NW_TEST_EXIT_2		69
#define COM_NW_TEST_REPLY 		70	// from here back to COM_SIGNAL_TEST_0 MUST be consecutive for test in progress calc
#define COM_GPRS_TIME_1			71
#define COM_GPRS_TIME_2			72
#define COM_GPRS_TIME_3			73
#define COM_GPRS_TIME_4			74
#define COM_GPRS_TIME_5			75
#define COM_WAIT_FOR_FS			76
#define COM_IDLE_WAIT			77
// Gap
#define COM_SD_CARD_FAILED		100

#define COM_FTP_FILE_BLOCK_SIZE	512

// COM_sign_on_status values
#define COM_STATUS_NO_UPDATE		0
#define COM_STATUS_OK				1
#define COM_STATUS_SD_FAIL			2
#define COM_STATUS_GSM_FAIL			3
#define COM_STATUS_GPRS_FAIL		4
#define COM_STATUS_FTP_FAIL			5
#define COM_STATUS_SERVER_ERROR		6

// RTC_now.wkd = 0..6 for Sun..Sat
// Mask = bit 6 for Monday ... bit 0 for Sunday. Ouch.
const uint8 com_day_mask[7] =
{
	_B00000001, _B01000000, _B00100000, _B00010000, _B00001000, _B00000100, _B00000010
};

uint8 com_state;
uint8 com_sms_current_slot;
uint8 com_n_sms_to_read;
uint8 com_day_bcd;
uint8 com_tsync_attempt_count;

// network test
uint8 com_network_count;
uint8 com_network_total;
uint8 com_network_cell;
uint8 com_neighbour_cells;
uint8 com_network_check_count;
uint8 com_home_cell_strength;
uint8 com_source_index;
FAR char com_network_name[17];
FAR char com_network_number[6];
FAR char com_home_network_number[6];

// modem retry strategy
uint8 com_registration_attempts;
uint8 com_registration_retry_limit;
uint8 com_registration_retry_interval;
uint8 com_tx_attempts;
uint8 com_transmission_retry_limit;
uint8 com_ftp_data_channel;

int com_sms_slot_count;
int com_ftp_time_index;				// points to timestamps in FTP_PART_DATA message
int com_msg_body_index;				// points to current position in tx message body during FTP data tx

uint16 com_ftp_sequence;

unsigned long com_ftp_block_size;
unsigned long com_ftp_file_seek_pos;
unsigned long com_ftp_file_end_pos;
unsigned long com_ftp_bytes_sent;

unsigned int com_ftp_file_flags;
unsigned int com_ftp_file_mask;

struct
{
	uint32 window_tx;
	uint32 window_rx;
	uint32 ftp_poll;
	uint32 re_register;
	uint32 rx_delay;
	uint32 sigtst_delay;
	uint32 nwtst_delay;
} com_time;

FAR	char com_ftp_path[32];
FAR char com_server_filename[128];

FAR RTC_type com_ftp_timestamp;

// *************************
// private functions
// *************************

#endif
/******************************************************************************
** Function:	Reset logger immediately
**
** Notes:		
*/
void COM_reset(void)
{
	CFS_power_down();		// switch off SD card
	HDW_SNS1_MCLR = true;	// reset sensor PIC

#if (HDW_NUM_CHANNELS == 9)
	HDW_SNS2_MCLR = true;	// reset second sensor PIC
#endif

	TIM_delay_ms(1000);		// wait 1 second
	Reset();
}

//*****************************************************************************
// Function:	Log a comms failure
//
// Notes:		Pass in a non-zero value to update sign-on status
//
void com_log_error(uint16 line, uint8 status)
{
#ifndef HDW_PRIMELOG_PLUS
	if (status != COM_STATUS_NO_UPDATE)
		COM_sign_on_status = status;
#endif

	LOG_enqueue_value(LOG_ACTIVITY_INDEX, LOG_COM_FILE, line);
}


#ifndef HDW_PRIMELOG_PLUS
/******************************************************************************
** Function:	find file given filename and path
**
** Notes:		returns size of file minus 1 or 0 if not present
*/
long com_find_file(char * path, char * filename)
{
	SearchRec com_srch;

	if (CFS_state != CFS_OPEN)
		return 0;

	if (FSchdir(path) != 0)
		return 0;

	com_srch.attributes = ATTR_DIRECTORY;							// currently in a directory
	if (FindFirst(filename, ATTR_MASK, &com_srch) != 0)
		return 0;

	if (com_srch.filesize == 0)
		return 0;

	// set end_pos to file length - 1
	return (com_srch.filesize - 1);
}

/******************************************************************************
** Function:	Action when GPRS fails for external reason during ftp communication
**
** Notes:		e.g. server error or modem fail due to low signal
*/
void com_gprs_fail_action(void)
{
	com_pending_sms_tx = true;				// check sms outbox for outgoing messages
	com_pending_sms_rx = true;				// check modem sms inbox for incoming messages
	com_gprs_fail = true;					// need to tell sms rx reason for being called
											// so it can call com_retry_registration
	com_state = COM_WAIT_FOR_FS;			// bypass modem power cycle
}

/******************************************************************************
** Function:	Action when GPRS fails for internal reason during ftp communication
**
** Notes:		e.g. faulty logon file
*/
void com_gprs_abort_action(void)
{
	MSG_remove(FTP_OUTBOX);					// remove current message - we cannot send it
	com_pending_sms_tx = true;				// check sms outbox for outgoing messages
	com_pending_sms_rx = true;				// check modem sms inbox for incoming messages
	com_state = COM_WAIT_FOR_FS;			// bypass modem power cycle
}

/******************************************************************************
** Function:	End GPRS socket connection
**
** Notes:
*/
void com_socket_disconnect(void)
{
	MDM_tx_delay_timer_x20ms = 2 * 50;					// wait 2s before sending +++
	MDM_send_cmd("+++");
	MDM_retry_ptr = NULL;								// only 1 attempt
	MDM_cmd_timer_x20ms = COM_NO_CARRIER_TIMEOUT_X20MS;	// wait for "NO CARRIER"
	com_ftp_slow_server = false;						// assume server OK by default
}

/******************************************************************************
** Function:	Check if modem schedule time is the next required
**
** Notes:		Updates COM_wakeup_time as necessary
*/
void com_update_wakeup_time_sec(uint32 t)
{
	if (t <= RTC_time_sec)		// don't schedule a wakeup if it's in the past
		return;

	if (t < COM_wakeup_time)
		COM_wakeup_time = t;
}

/******************************************************************************
** Function:	Check if modem schedule time is the next required
**
** Notes:		Updates COM_wakeup_time as necessary
*/
void com_update_wakeup_time_hhmm(RTC_hhmm_type *p)
{
	com_update_wakeup_time_sec(RTC_bcd_time_to_sec(p->hr_bcd, p->min_bcd, 0));
}

/******************************************************************************
** Function:	Schedule the next registration attempt (if any)
**
** Notes:		Call when registration failed, even after power-cycling.
**				Do not set a retry time on or after midnight
*/
void com_retry_registration(void)
{
	com_state = COM_IDLE;
	com_pending_sms_tx = false;														// disable all pending rx and tx
	com_pending_sms_rx = false;
	com_pending_ftp_tx = false;
	com_pending_ftp_poll = false;
	
	com_time.re_register = SLP_NO_WAKEUP;											// by default
	if (!COM_schedule.ftp_enable && (++com_registration_attempts <= com_registration_retry_limit))	// can sleep before retry
		com_time.re_register = RTC_time_sec + LOG_interval_sec[com_registration_retry_interval];
	else if (com_ftp_slow_server)
		com_time.re_register = RTC_time_sec + 120;									// try again in a couple of minutes

	if (com_time.re_register != SLP_NO_WAKEUP)										// sleep then retry
	{
		if (com_time.re_register >= RTC_SEC_PER_DAY)								// if on or after midnight
		{
			com_registration_attempts = 0;											//  give up
			com_time.re_register = SLP_NO_WAKEUP;
		}
		else
			com_update_wakeup_time_sec(com_time.re_register);						// else retry at time specified
	}
	else																			// else end of registration retry
		com_registration_attempts = 0;												// give up * leave re_register time as NO_WAKEUP

	if (com_time.re_register == SLP_NO_WAKEUP)										// if we are not going to try again
	{												
		if ((MSG_tx_buffer[0] == MSG_TYPE_FTP_FILE) ||								// if we were trying to send ftp overriding ftp_enable 
			(MSG_tx_buffer[0] == MSG_TYPE_FTP_ALL_DATA))
		{
			MSG_remove(FTP_OUTBOX);													// Stop trying to send it
			com_log_error((uint16)__LINE__, COM_STATUS_NO_UPDATE);					// Data Rq abandoned
			com_pending_ftp_tx = true;												// anything else waiting?
			com_pending_ftp_poll = true;											// check for incoming ftp
		}
	}
}

/******************************************************************************
** Function:	Retry sending an sms message
**
** Notes:		We signed on OK, but message failed to send
*/
void com_retry_sms_message(void)
{
	if (++com_tx_attempts > com_transmission_retry_limit)				// Max retries exceeded - ditch the message
	{
		MSG_remove(SMS_OUTBOX);											// Stop trying to send it
		com_tx_attempts = 0;
		com_log_error((uint16)__LINE__, COM_STATUS_NO_UPDATE);			// Message abandoned
		com_pending_sms_tx = true;										// anything else waiting?
		com_state = COM_IDLE;
	}
	else
	{
		// check registration before next attempt to transmit
		MDM_send_cmd("AT+CREG?\r");
		com_state = COM_CHECK_REGISTRATION;
	}
}

/******************************************************************************
** Function:	Finished Tx/Rx
**
** Notes:		if more to do, set up correct state and continue
**				without shutting down modem
*/
void com_finished(void)
{
	if (MSG_files_in_sms_outbox > 0)									// double check that no more files need sending
		com_pending_sms_tx = true;
	if (!com_ftp_slow_server && (MSG_files_in_ftp_outbox > 0))			// if it's a slow server, do next file in a while
		com_pending_ftp_tx = true;

	if (com_gprs_fail)													// check if gprs fail - this was a read of sms after gprs failure
	{
		com_gprs_fail = false;
		com_retry_registration();										// set up registration retry
	}
	else
	{
		if (com_pending_ftp_tx || com_pending_sms_tx || com_pending_sms_rx)
			com_state = COM_WAIT_FOR_FS;
		else if (com_pending_ftp_poll)
		{
			com_pending_ftp_poll = false;
			com_state = COM_POLL_FTP_1;
		}
		else if (COM_mdm_stdby_required)
		{
			HDW_MODEM_DTR_N = true;										// modem standby
			com_state = COM_IDLE;
		}
		else
		{
			MDM_shutdown();
			com_state = COM_SHUTTING_DOWN;
		}
	}
	com_sms_rx_second_shot = false;										// clear second shot flag
}

/******************************************************************************
** Function:	Start reading the SMS in the next slot
**
** Notes:
*/
void com_read_next_sms(void)
{
	if ((++com_sms_current_slot > com_sms_slot_count) || (com_n_sms_to_read == 0))	// no more to read
	{
		MDM_send_cmd("AT+CSQ\r");						// check for more received - get signal quality
		com_state = COM_CHECK_CSQ;
	}
	else
	{
		sprintf(MDM_tx_buffer, "AT+CMGF=1;+CMGR=%u\r", com_sms_current_slot);
		MDM_send_cmd(MDM_tx_buffer);
		com_state = COM_RX_SMS_3;
	}
}

/******************************************************************************
** Function:	Get phone number of sender of incoming SMS
**
** Notes:		Sets com_sms_sender_number. Makes it an empty string if any problem.
*/
void com_get_sender_number(void)
{
	char * p;

	COM_sms_sender_number[0] = '\0';			// assume invalid number by default
	p = strstr(MDM_rx_buffer, ",\"");			// find number after first ,"
	STR_parse_delimited_string(p + 1, COM_sms_sender_number, sizeof(COM_sms_sender_number), '\"', '\"');
}

/******************************************************************************
** Function:	send ftp logon string to modem
**
** Notes:		gets string from memory card Config\ftplogon.txt
**				if file system not open or file does not exist - returns false
*/
bool com_send_ftp_logon(void)
{
	bool last_echo = USB_echo;

	if (!CFS_open())
	{
		com_log_error((uint16)__LINE__, COM_STATUS_SD_FAIL);		// FS not ready
		return false;
	}

	if (!FTP_get_logon_string(STR_buffer))
	{
		com_log_error((uint16)__LINE__, COM_STATUS_NO_UPDATE);		// No logon file
		return false;
	}
	// else got text in STR_buffer
	// suppress echo
	USB_echo = false;
	sprintf(MDM_tx_buffer, "%s", STR_buffer);
	MDM_send_cmd(MDM_tx_buffer);
	MDM_retry_ptr = NULL;			// only 1 attempt
	MDM_cmd_timer_x20ms = 35 * 50;	// FTP timeout

	// reinstate echo if was enabled
	if (last_echo)
	{
		USB_echo = true;
		USB_monitor_string("at#ftpopen=*");
	}

	return true;
}

/******************************************************************************
** Function:	Confirm or request GPRS connection
**
** Notes:
*/
void com_get_gprs(void)
{
	sprintf(MDM_tx_buffer, "at#ftpto=300;#gprs?\r");
	MDM_tx_delay_timer_x20ms = 20;			// 400ms delay
	MDM_send_cmd(MDM_tx_buffer);
	MDM_retry_ptr = NULL;					// only 1 attempt
	MDM_cmd_timer_x20ms = 10 * 50;			// 10s timeout for reply
	com_state = COM_QUERY_GPRS;
}

/******************************************************************************
** Function:	com_gprs_connected
**
** Notes:		common code run after gprs = 1 or IP received ok
**				to log on to gprs server
*/
void com_gprs_connected(void)
{
	if (com_tsync_attempt_count > 0)			// nitz tsync required using nitz logon
	{
		// send 1st line of gprs time logon file (at#sktd=...)
		if (CFS_read_line((char *)CFS_config_path, (char *)CFS_nitz_logon_name, 1, STR_buffer, 128) < 1)
		{
			USB_monitor_string("Faulty gprs time logon file 1");
			com_log_error((uint16)__LINE__, COM_STATUS_NO_UPDATE);	//Faulty gprs time logon file 2
			// give up - clear attempt counter and go and look for sms
			com_tsync_attempt_count = 0;			// clear nitz time sync attempt counter

#ifdef HDW_1FM
			if (MAG_first_tsync)					// no config, so disable TSYNC
			{
				TSYNC_on = false;
				TSYNC_action();
			}
#endif
			com_pending_sms_tx = true;				// check sms outbox for outgoing messages
			com_pending_sms_rx = true;				// check modem sms inbox for incoming messages
			com_state = COM_WAIT_FOR_FS;			// bypass modem power cycle
		}
		else										// got first line in STR_buffer
		{
			sprintf(MDM_tx_buffer, "%s\r\n", STR_buffer);
			MDM_send_cmd(MDM_tx_buffer);
			MDM_tx_delay_timer_x20ms = 50;			// tx delay of 1s
			MDM_retry_ptr = NULL;	 				// only 1 attempt
			MDM_cmd_timer_x20ms = 140 * 50;			// 140s to wait if no reply
			com_state = COM_GPRS_TIME_1;
		}

#ifdef HDW_1FM
		MAG_first_tsync = false;					// next TSYNC is not the first
#endif
	}
	else if (!com_send_ftp_logon())			// couldn't get logon string
		com_gprs_abort_action();
	else
		com_state = (MSG_tx_buffer[0] == '\0') ? COM_POLL_FTP_4 : COM_TX_FTP_3;
}

/******************************************************************************
** Function:	com_get_details_txt
**
** Notes:		gets first line of file CONFIG\DETAILS.TXT into STR_buffer
**				limits length to 128 chars
**				returns false if file does not exist
*/
bool com_get_details_txt(void)
{
	int j;

	if (!CFS_read_file((char *)CFS_config_path, (char *)CFS_details_name, STR_buffer, 128))
		return false;

	if (STR_buffer[0] == 0)
		return false;

	// strip off CRLF if present
	j = strlen(STR_buffer) - 1;
	if ((j > 0) && ((STR_buffer[j] == '\r') || (STR_buffer[j] == '\n')))
	{
		STR_buffer[j] = '\0';
		j--;
		if ((STR_buffer[j] == '\r') || (STR_buffer[j] == '\n'))
		{
			STR_buffer[j] = '\0';
		}
	}
	return true;
}

/******************************************************************************
** Function:	com_create_extended_filename
**
** Notes:		creates extended filename from unit id, file path and file name
**				in com_server_filename
**				taking into account presence of site id
*/
void com_create_extended_filename(char * path, char * filename)
{
	uint8 i = 0;
	int   j;

	j = sprintf(com_server_filename, "%s%s_%s", CAL_build_info.serial_number, path, filename);
	// change \ to _
	while (i < strlen(com_server_filename))
	{
		if (com_server_filename[i] == 0x5C) com_server_filename[i] = '_';
		i++;
	}
	// add details.txt contents in brackets if present
	if (com_get_details_txt())
	{
		// first strip ".txt" off end (this came with the filename)
		j -= 4;
		sprintf(&com_server_filename[j], "(%s).txt", STR_buffer);
	}
}

/******************************************************************************
** Function:	com_create_logdata_filename
**
** Notes:		creates ftp logdata filename from unit id, file path and file name
**				in com_server_filename
**				taking into account presence of site id
*/
void com_create_logdata_filename(char * filename)
{
	int j;

	j = sprintf(com_server_filename, "%s_logdata_%s", CAL_build_info.serial_number, filename);
	// add details.txt contents in brackets if present
	if (com_get_details_txt())
	{
		// first strip ".txt" off end (this came with the filename)
		j -= 4;
		sprintf(&com_server_filename[j], "(%s).txt", STR_buffer);
	}
}

/******************************************************************************
** Function:	com_create_message_filename
**
** Notes:		creates ftp message filename from unit id, file path and file name
**				in com_server_filename
**				taking into account presence of site id
*/
void com_create_message_filename(char * filename)
{
	int j;

	j = sprintf(com_server_filename, "%s_%s", CAL_build_info.serial_number, filename);
	// add details.txt contents in brackets if present
	if (com_get_details_txt())
	{
		// first strip ".txt" off end (this came with the filename)
		j -= 4;
		sprintf(&com_server_filename[j], "(%s).txt", STR_buffer);
	}
}

/******************************************************************************
** Function:	com_parse_ftp_date
**
** Notes:		extract date from string at date_p
**				string of form "210110"
**				return false if scan error
*/
bool com_parse_ftp_date(char * date_p)
{
	unsigned int value;

	if (sscanf(date_p, "%02x", &value) != 1)
	{
		return false;
	}
	com_ftp_timestamp.day_bcd = (uint8)value;
	date_p += 2;
	if (sscanf(date_p, "%02x", &value) != 1)
	{
		return false;
	}
	com_ftp_timestamp.mth_bcd = (uint8)value;
	date_p += 2;
	if (sscanf(date_p, "%02x", &value) != 1)
	{
		return false;
	}
	com_ftp_timestamp.yr_bcd = (uint8)value;
	return true;
}

/******************************************************************************
** Function:	com_parse_ftp_time
**
** Notes:		extract time from string at time_p
**				string of form "100530"
**				return false if scan error
*/
bool com_parse_ftp_time(char * time_p)
{
	unsigned int value;

	if (sscanf(time_p, "%02x", &value) != 1)
	{
		return false;
	}
	com_ftp_timestamp.hr_bcd = (uint8)value;
	time_p += 2;
	if (sscanf(time_p, "%02x", &value) != 1)
	{
		return false;
	}
	com_ftp_timestamp.min_bcd = (uint8)value;
	time_p += 2;
	if (sscanf(time_p, "%02x", &value) != 1)
	{
		return false;
	}
	com_ftp_timestamp.sec_bcd = (uint8)value;
	return true;
}

/******************************************************************************
** Function:	Determine if time now is within specified window
**
** Notes:
*/
bool COM_within_window(COM_window_type *p)
{
	uint16 yesterday_index;
	bool today;

	today = ((p->day_mask & com_day_mask[RTC_now.wkd]) != 0);

	if (p->start.word < p->stop.word)				// on if between start & stop times & today flag set
		return (today && RTC_hhmm_past(&p->start) && !RTC_hhmm_past(&p->stop));
	// else:

	if (p->start.word == p->stop.word)				// on if today
		return today;

	// else window straddles midnight
	if (RTC_hhmm_past(&p->start) || !RTC_hhmm_past(&p->stop))	// on if window started yesterday
	{
		// Weekday 0..6 = Sun..Sat, day mask bit 6 = Mon, bit 0 = Sun
		yesterday_index = (RTC_now.wkd == 0) ? 6 : RTC_now.wkd - 1;
		return ((p->day_mask & com_day_mask[yesterday_index]) != 0);
	}

	// else not in the midnight to stop period
	return false;
}

/******************************************************************************
** Function:	Adjust wakeup times on or after midnight
**
** Notes:		Run once per day at midnight
*/
void com_new_day_task(void)
{
	com_time.window_tx = COM_first_window_time(&COM_schedule.tx_window);
	com_time.window_rx = COM_first_window_time(&COM_schedule.rx_window);

	if (com_time.re_register != SLP_NO_WAKEUP)
	{
		if (com_time.re_register >= RTC_SEC_PER_DAY)
			com_time.re_register -= RTC_SEC_PER_DAY;
	}

	if (com_time.ftp_poll != SLP_NO_WAKEUP)
	{
		if (com_time.ftp_poll >= RTC_SEC_PER_DAY)
			com_time.ftp_poll -= RTC_SEC_PER_DAY;
	}

	COM_wakeup_time = 0;
	com_day_bcd = RTC_now.day_bcd;
}

/******************************************************************************
** Function:	Work out next action from IDLE state
**
** Notes:
*/
void com_idle_actions(void)
{
	com_state = COM_IDLE;				// set default

	if (COM_reset_logger == COM_RESET_LOGGER_KEY)
		COM_reset();
	// else:

	if ((COM_commissioning_mode == 2) || !CAL_build_info.modem)	// these override all else
	{
		if (MDM_state != MDM_OFF)
		{
			MDM_shutdown();
			com_state = COM_SHUTTING_DOWN;
		}
		return;
	}
	// else:

	if (MDM_state == MDM_OFF)			// see if we need to turn it on
	{
		if (COM_mdm_stdby_required)
		{
			com_pending_sms_tx = true;
			com_pending_sms_rx = true;
			if (COM_schedule.ftp_enable)
			{
				com_pending_ftp_poll = true;
				com_pending_ftp_tx = true;
			}
		}
	}

	if (com_pending_ftp_poll)
	{
		com_pending_ftp_poll = false;
		com_state = COM_POLL_FTP_1;
	}
	else if (com_pending_sms_tx || com_pending_sms_rx || com_pending_ftp_tx)
		com_state = COM_WAIT_FOR_FS;
	else if (COM_sigtst_count > 0)
		com_state = COM_SIGNAL_TEST_0;
	else if (com_pending_nw_test)
		com_state = COM_NW_TEST_0;
	else if (com_pending_tsync)
	{
		// only proceed if tsync is enabled
		if (TSYNC_on)
			com_state = COM_WAIT_FOR_FS;
		else
		{
			com_tsync_attempt_count = 0;
			com_pending_tsync = false;
		}
	}
	else if (com_pending_check_dflags)
	{
		com_pending_check_dflags = false;
		CMD_check_dirty_flags();
	}
	else if (!COM_mdm_stdby_required)
	{
		if (MDM_state != MDM_OFF)
		{
			MDM_shutdown();
			com_state = COM_SHUTTING_DOWN;
		}
	}
}

/********************************************************************
 * Function:        bool  com_parse_ftp_command_filename(void)
 *
 * PreCondition:    None
 *
 * Input:           None
 *
 * Output:          false if not a command file
 *
 * Side Effects:    None
 *
 * Overview:        extract filename CMDxxxx.TXT from STR_buffer into com_ftp_filename
 *
 * Note:            None
 *******************************************************************/
bool  com_parse_ftp_command_filename(void)
{
	char * start_p;
	char * end_p;

	start_p = strstr(STR_buffer, "CMD");
	if (start_p == 0)
	{
		start_p = strstr(STR_buffer, "cmd");
	}
	if (start_p == 0) return false;

	end_p = strstr(STR_buffer, ".TXT");
	if (end_p == 0)
	{
		end_p = strstr(STR_buffer, ".txt");
	}
	if (end_p == 0) return false;
	end_p+= 4;
	*end_p = '\0';
	strcpy(COM_ftp_filename, start_p);
	return true;
}

#endif
// *************************
// public functions
// *************************

/******************************************************************************
** Function:	long interval
**
** Notes:		return true if all window intervals are equal to or greater than 15 minutes
*/
bool COM_long_interval(void)
{
	if ((COM_schedule.tx_window.interval < 14) && (COM_schedule.tx_window.interval > 0))
		return false;
	if (COM_schedule.tx_window.interval > 24)
		return false;
	if ((COM_schedule.rx_window.interval < 14) && (COM_schedule.rx_window.interval > 0))
		return false;
	if (COM_schedule.rx_window.interval > 24)
		return false;
	if ((COM_schedule.modem_standby.interval < 14) && (COM_schedule.modem_standby.interval > 0))
		return false;
	if (COM_schedule.modem_standby.interval > 24)
		return false;
	return true;
}

/******************************************************************************
** Function:	calculate next window time
**
** Notes:
*/
uint32 COM_next_window_time(COM_window_type *p)
{
#ifndef HDW_PRIMELOG_PLUS
	uint16 yesterday_index;
	bool today;
	uint32 start_sec;
	uint32 stop_sec;
	uint32 interval_sec;
	uint32 t;
	uint32 num_intervals;

	if (p->interval == 0)																		// if no interval set no wakeup
		return SLP_NO_WAKEUP;
																								// else:
	today = ((p->day_mask & com_day_mask[RTC_now.wkd]) != 0);
	start_sec = RTC_bcd_time_to_sec(p->start.hr_bcd, p->start.min_bcd, 0);
	stop_sec = RTC_bcd_time_to_sec(p->stop.hr_bcd, p->stop.min_bcd, 0);
	interval_sec = LOG_interval_sec[p->interval];
																								// Get minimum interval anchored to start time which puts us in the future:
	t = RTC_time_sec;
	if (t < start_sec)
		t += RTC_SEC_PER_DAY;
	t -= start_sec;
	num_intervals = (t / interval_sec) + 1;
	t = start_sec + (num_intervals * interval_sec);
	if (t >= RTC_SEC_PER_DAY)																	// gone into tomorrow
	{
		t -= RTC_SEC_PER_DAY;																	// try correcting back to today
		if (t <= RTC_time_sec)																	// in the past, so keep it tomorrow
			t += RTC_SEC_PER_DAY;
	}

	if (p->start.word < p->stop.word)															// on if between start & stop times & today flag set
	{
		if (!today || (t > stop_sec))															// not today, or window finished
			return SLP_NO_WAKEUP;
																								// else wait for start of window, or return next time
		return ((RTC_time_sec < start_sec) ? start_sec : t);
	}
																								// else:
	if (p->start.word == p->stop.word)															// window on throughout today
		return (today ? t : SLP_NO_WAKEUP);
																								// else window straddles midnight:
	if (t <= stop_sec)																			// in the midnight to stop time section
	{
																								// Weekday 0..6 = Sun..Sat, day mask bit 6 = Mon, bit 0 = Sun
		yesterday_index = (RTC_now.wkd == 0) ? 6 : RTC_now.wkd - 1;
		if ((p->day_mask & com_day_mask[yesterday_index]) == 0)									// window didn't start yesterday
			return (today ? start_sec : SLP_NO_WAKEUP);
																								// else:
		return t;
	}
																								// else window straddles midnight, & we're past stop time
	if (!today)
		return SLP_NO_WAKEUP;
																								// else:
	return ((t < start_sec) ? start_sec : t);
#else
	return 0;
#endif
}

/******************************************************************************
** Function:	calculate first window event time, at midnight
**
** Notes:
*/
uint32 COM_first_window_time(COM_window_type *p)
{
#ifndef HDW_PRIMELOG_PLUS
	uint16 yesterday_index;
	bool today;
	uint32 start_sec;
	uint32 interval_sec;
	uint32 t;
	uint16 num_intervals;

	if (p->interval == 0)																		// if no interval set no wakeup
		return SLP_NO_WAKEUP;
																								// else:
	today = ((p->day_mask & com_day_mask[RTC_now.wkd]) != 0);
	start_sec = RTC_bcd_time_to_sec(p->start.hr_bcd, p->start.min_bcd, 0);
	interval_sec = LOG_interval_sec[p->interval];

	if (p->start.word < p->stop.word)															// on if between start & stop times & today flag set
		return (today ? start_sec : SLP_NO_WAKEUP);
																								// else:
																								// Get minimum interval anchored to start time which puts us in the present or future:
	t = RTC_SEC_PER_DAY - start_sec;
	num_intervals = (uint16)(t / interval_sec);
	if ((t % interval_sec) != 0)
		num_intervals++;
	t = start_sec + (num_intervals * interval_sec);
	if (t >= RTC_SEC_PER_DAY)																	// gone into tomorrow
		t -= RTC_SEC_PER_DAY;																	// correct back to today

	if (p->start.word == p->stop.word)															// window on throughout today
		return (today ? t : SLP_NO_WAKEUP);
																								// else window straddles midnight: on if window started yesterday
																								// Weekday 0..6 = Sun..Sat, day mask bit 6 = Mon, bit 0 = Sun
	yesterday_index = (RTC_now.wkd == 0) ? 6 : RTC_now.wkd - 1;
	if ((p->day_mask & com_day_mask[yesterday_index]) == 0)										// window didn't start yesterday
		return (today ? start_sec : SLP_NO_WAKEUP);
																								// else we're in an active window straddling midnight
	if (t > RTC_bcd_time_to_sec(p->stop.hr_bcd, p->stop.min_bcd, 0))							// new time past end of window
		return (today ? start_sec : SLP_NO_WAKEUP);
																								// else:
	return t;																					// new time within midnight to stop time of a window straddling midnight
#else
	return 0;
#endif
}

/******************************************************************************
** Function:	test in progress
**
** Notes:		returns true if either sigtest or network test is in progress
*/
bool COM_test_in_progress(void)
{
#ifndef HDW_PRIMELOG_PLUS
	return ((com_state >= COM_SIGNAL_TEST_0) && (com_state <= COM_NW_TEST_REPLY));
#else
	return false;
#endif
}

/******************************************************************************
** Function:	start ftp file transfer
**
** Notes:		path is target path in logger file memory
**				filename is source and target file name
*/
void COM_ftp_transfer_file(char * path, char * filename)
{
#ifndef HDW_PRIMELOG_PLUS
	strcpy(FTP_path_str, path);
	strcpy(FTP_filename_str, filename);
	com_ftp_transfer_file_pending = true;
#endif
}

/******************************************************************************
** Function:	recalculate wakeup times when time of day change or #MPS received
**
** Notes:
*/
void COM_recalc_wakeups(void)
{
#ifndef HDW_PRIMELOG_PLUS
	com_time.window_tx = COM_next_window_time(&COM_schedule.tx_window);
	com_time.window_rx = COM_next_window_time(&COM_schedule.rx_window);
	com_time.ftp_poll = COM_next_window_time(&COM_schedule.modem_standby);
	if (com_time.rx_delay != SLP_NO_WAKEUP)
		com_time.rx_delay = RTC_time_sec;
	com_time.re_register = SLP_NO_WAKEUP;
	com_time.sigtst_delay = SLP_NO_WAKEUP;
	com_time.nwtst_delay = SLP_NO_WAKEUP;
	com_registration_attempts = 0;
	COM_set_wakeup_time();
#endif
}

/******************************************************************************
** Function:	cancel commisioning mode
**
** Notes:		Do anything that needs doing when commissioning mode is no longer set to 1
*/
void COM_cancel_com_mode(void)
{
#ifndef HDW_PRIMELOG_PLUS
	// clear re-registration attempts
	com_registration_attempts = 0;
	com_time.re_register = SLP_NO_WAKEUP;
#endif
}

/******************************************************************************
** Function:	Initiate Tx
**
** Notes:		Just sets correct pending tx flag so COM state machine will do it when idle
*/
void COM_initiate_tx(bool sms)
{
#ifndef HDW_PRIMELOG_PLUS
	if (sms)
	{
		com_pending_sms_tx = true;
		com_pending_sms_rx = true;
	}
	else
	{
		com_pending_ftp_poll = true;
		com_pending_ftp_tx = true;
	}
	com_registration_attempts = 0;
#endif
}

/******************************************************************************
** Function:	Initiate network test
**
** Notes:		Sets a flag so COM state machine will do it when idle
*/
void COM_initiate_nw_test(void)
{
#ifndef HDW_PRIMELOG_PLUS
	com_pending_nw_test = true;
	COM_nwtest_progress = 'W';
	com_network_count = 0;
	com_network_cell = 0;
#endif
}

/******************************************************************************
** Function:	Initiate gprs time sync using NITZ
**
** Notes:		Sets a count so COM state machine will do it when idle
*/
void COM_initiate_gprs_nitz_tsync(void)
{
#ifndef HDW_PRIMELOG_PLUS
	// set nitz count to number of tries allowed
	com_tsync_attempt_count = 4;
	com_pending_tsync = true;
#endif
}

/******************************************************************************
** Function:	schedule dirty flags check
**
** Notes:		if in a signal test (SIGTST or NWTST) sets a flag so that dirty flags check is done when test finishes
**              otherwise runs check immediately
*/
void  COM_schedule_check_dflags(void)
{
#ifndef HDW_PRIMELOG_PLUS
	if (COM_test_in_progress())
		com_pending_check_dflags = true;			// do it next time com is idle
	else
		CMD_check_dirty_flags();
#endif
}

/******************************************************************************
** Function:	Check if OK to sleep
**
** Notes:		can sleep if COM_IDLE, which includes waiting to retry registration
**				or waiting to re-check for incoming SMS in mode COM_RX_DELAY
**				or when calling MDM_ready() and the modem can sleep
*/
bool COM_can_sleep(void)
{
#ifdef HDW_PRIMELOG_PLUS
	return true;
#else
	// if COM_IDLE
	if (com_state == COM_IDLE)
	{
		// can sleep if command task idle and no signal test set up
		if (CMD_can_sleep() && (COM_sigtst_count == 0)) return true;
	}
	// else can sleep when COM_RX_DELAY if rx delay wakeup time is set
	else if ((com_state == COM_RX_DELAY) && (com_time.rx_delay != SLP_NO_WAKEUP)) return true;
	// else can sleep when COM_SIGNAL_TEST_1 if sigtst delay wakeup time is set
	else if ((com_state == COM_SIGNAL_TEST_1) && (com_time.sigtst_delay != SLP_NO_WAKEUP)) return true;
	// else can sleep when COM_NW_TEST_1 if gsmtst delay wakeup time is set
	else if ((com_state == COM_NW_TEST_1) && (com_time.nwtst_delay != SLP_NO_WAKEUP)) return true;
	// else can sleep when calling MDM_ready() and modem can sleep
	else if ((com_state == COM_TX_MSG) || (com_state == COM_RX_SMS_1) || (com_state == COM_POLL_FTP_1) || (com_state == COM_SIGNAL_TEST_0) || (com_state == COM_NW_TEST_0))
	{
		if (MDM_can_sleep())
			return true;
	}
	return false;
#endif
}

/******************************************************************************
** Function:	Modem & messaging schedule control
**
** Notes:
*/
void COM_schedule_control(void)
{
#ifndef HDW_PRIMELOG_PLUS
	
	COM_mdm_stdby_required =
#ifdef HDW_1FM
		MAG_modem_active ||
#endif
		(COM_commissioning_mode == 1) || COM_within_window(&COM_schedule.modem_standby);

	if (COM_wakeup_time > RTC_time_sec)					// no pending schedule events
		return;
	// else wakeup time for something has expired.

	// Check for Tx window events
	if (com_time.window_tx <= RTC_time_sec)
	{
		if (!COM_schedule.ftp_enable)				// SMS mode
		{
			// Batch mode: transmit at start of the window & at scheduled interval.
			// Non-batch mode: generate & transmit at start of window & at scheduled interval.
			com_pending_sms_tx = true;				// always do transmit & receive
			com_pending_sms_rx = true;
			if (!COM_schedule.batch_enable)			// non-batch SMS mode
				PDU_schedule_all();					// generate SMS for all SMS-enabled channels
		}
		else										// FTP mode
		{
			FTP_schedule();							// generate outgoing FTP reports
			com_pending_ftp_poll = true;			// register need for a poll for ftp commands
		}

		com_time.window_tx = COM_next_window_time(&COM_schedule.tx_window);
	}

	// Check for Rx window events
	if (com_time.window_rx <= RTC_time_sec)
	{
		com_pending_sms_tx = true;					// always do transmit & receive
		com_pending_sms_rx = true;
		if (COM_schedule.ftp_enable)
			com_pending_ftp_poll = true;

		com_time.window_rx = COM_next_window_time(&COM_schedule.rx_window);
	}

	// catch start of standby or commissioning mode for ftp_poll
	if (COM_mdm_stdby_required)
	{
		if (COM_schedule.ftp_enable && (com_time.ftp_poll == SLP_NO_WAKEUP))
		{
			com_time.ftp_poll = RTC_time_sec;
		}
	}

	// this allows use of re_register time for registration retry if fails during window rx or tx
	if (com_time.re_register <= RTC_time_sec)
	{
		// time to attempt re-registration - do sms in all cases
		com_pending_sms_tx = true;
		com_pending_sms_rx = true;
		// also do ftp if enabled
		if (COM_schedule.ftp_enable)
		{
			com_pending_ftp_poll = true;
			com_pending_ftp_tx = true;
		}

		// if in commissioning mode, or in standby window, set a 15 minute re_register event,
		if (COM_mdm_stdby_required)
		{
			com_time.re_register = RTC_time_sec;
			LOG_set_next_time(&com_time.re_register, 14, false);
		}
		else											// standby not required
		{
			com_time.re_register = SLP_NO_WAKEUP;
		}
	}

	// check if FTP poll required
	if (com_time.ftp_poll <= RTC_time_sec)
	{
		com_pending_ftp_poll = true;
		// if still in standby and ftp enabled
		if (COM_mdm_stdby_required && COM_schedule.ftp_enable && (COM_schedule.modem_standby.interval != 0))
		{
			com_time.ftp_poll = RTC_time_sec;
			com_time.ftp_poll = COM_next_window_time(&COM_schedule.modem_standby);
		}
		else										// ftp poll not required
		{
			com_time.ftp_poll = SLP_NO_WAKEUP;
		}
	}

	COM_set_wakeup_time();
#endif
}

/******************************************************************************
** Function:	set COM_wakeup_time to lowest of all com times
**
** Notes:
*/
void COM_set_wakeup_time(void)
{
#ifndef HDW_PRIMELOG_PLUS
	// set wakeup time to lowest of all com times
	COM_wakeup_time = SLP_NO_WAKEUP;		// default = no wakeup
	// tx and rx windows
	com_update_wakeup_time_sec(com_time.window_tx);
	com_update_wakeup_time_sec(com_time.window_rx);
	// check for re-registration
	com_update_wakeup_time_sec(com_time.re_register);
	// check for receive delay
	com_update_wakeup_time_sec(com_time.rx_delay);
	// check for signal test delay
	com_update_wakeup_time_sec(com_time.sigtst_delay);
	// check for gsm test delay
	com_update_wakeup_time_sec(com_time.nwtst_delay);
	// check standby start and stop
	com_update_wakeup_time_hhmm(&COM_schedule.modem_standby.start);
	com_update_wakeup_time_hhmm(&COM_schedule.modem_standby.stop);
	// Default FTP poll if modem is on
	com_update_wakeup_time_sec(com_time.ftp_poll);
#endif
}

/******************************************************************************
** Function:	GSM communications state machine
**
** Notes:
*/
void COM_task(void)
{
#ifndef HDW_PRIMELOG_PLUS
	bool   derived;
	char * cptr;
	char * dptr;
#ifndef HDW_RS485
	uint8  sensor_type_mask;
#endif
	int	   i, len;
	uint32 t;
	unsigned long file_size;
#endif

#ifdef HDW_PRIMELOG_PLUS
	if (COM_reset_logger == COM_RESET_LOGGER_KEY)
		COM_reset();
#else

	// check ring indicator interrupt flag
	if (IFS1bits.CNIF == 1)
	{
		IFS1bits.CNIF = 0;		// clear flag
		if (MDM_state == MDM_ON)
		{
			com_pending_sms_tx = true;		// send any outgoing sms
			com_pending_sms_rx = true;		// get any incoming sms
		}
	}

	if (com_day_bcd != RTC_now.day_bcd)		// date has changed (normally midnight)
		com_new_day_task();					// adjusts all wakeup times that have gone over midnight

	COM_schedule_control();

	switch(com_state)
	{
	case COM_IDLE:							// can sleep in this state
		if (!LOG_busy() && !PDU_busy())		// log (including dig and ana) and pdu not busy
			com_idle_actions();
		else								// wait for them to finish
			com_state = COM_IDLE_WAIT;

		com_pending_abort = false;
		break;

	case COM_IDLE_WAIT:						// stays awake in this state
		if (!LOG_busy() && !PDU_busy())		// dig, ana, log and pdu finished
			com_idle_actions();
		break;

	case COM_SD_CARD_FAILED:
		// cancel everything that could cause com_idle_actions() to stay awake
		if (com_pending_ftp_tx)
		{
			com_pending_ftp_tx = false;
			MSG_files_in_ftp_outbox = 0;
		}

		if (com_pending_sms_tx)
		{
			com_pending_sms_tx = false;
			MSG_files_in_sms_outbox = 0;
		}

		com_pending_ftp_poll = false;
		com_tsync_attempt_count = 0;
		com_pending_tsync = false;
		com_pending_sms_rx = false;

		// will shut down the modem if necessary in IDLE state
		com_state = COM_IDLE;
		break;

	case COM_WAIT_FOR_FS:
		if (!CFS_open())																	// stay here until file system is open
			break;

		LOG_flush();																		// ensure files of logged data are up to date
		if (com_pending_ftp_tx)
		{
			com_pending_ftp_tx = false;
			if (MSG_files_in_ftp_outbox > 0)												// check if can get a message from the ftp outbox
			{
				if (!MSG_get_message_to_tx(FTP_OUTBOX))										// get next ftp message
					com_state = COM_IDLE;													// if no file system, we haven't got a logger!
				else																		// transmit it
				{
					MDM_cmd_timer_x20ms = 50;												// 1s delay before tx
					com_state = COM_TX_WAIT;
				}
				break;
			}
		}
		
		if (com_pending_sms_tx)
		{
			com_pending_sms_tx = false;														// check if can get a message from the sms outbox
			if (MSG_files_in_sms_outbox > 0)
			{
				if (!MSG_get_message_to_tx(SMS_OUTBOX))										// get next sms message
					com_state = COM_IDLE;													// if no file system, we haven't got a logger!
				{
					MDM_cmd_timer_x20ms = 50;												// transmit it - 1s delay before tx
					com_state = COM_TX_WAIT;
				}
				break;
			}
		}
		
		if (com_pending_tsync)																// gprs time sync required?
		{
			com_pending_tsync = false;

			MDM_cmd_timer_x20ms = 50;														// this requires transmission - 1s delay before tx
			com_state = COM_TX_WAIT;
		}
		else																				//  else (com_pending_sms_rx) check if we need to look for incoming sms
		{
			com_pending_sms_rx = false;
			com_state = COM_RX_SMS_1;
		}
		break;

	case COM_TX_WAIT:
		if (MDM_cmd_timer_x20ms == 0)
			com_state = COM_TX_MSG;
		break;

	case COM_TX_MSG:															// Power up, sign on, send command
		if (MDM_ready(false))													// now powered-up & signed-on OR after failure
		{
			if (MDM_state == MDM_FAIL_TO_REGISTER)								// failed to sign on GSM network
			{
				com_retry_registration();
				break;
			}
			// else

			if (com_tsync_attempt_count > 0)									// nitz tsync required using socket control and nitz logon
			{
				com_time.re_register = SLP_NO_WAKEUP;
				com_get_gprs();
			}
			else
			{
				if (!COM_mdm_stdby_required && !com_sms_rx_second_shot)			// if not in standby and this is first shot
				{
					if ((com_time.rx_delay == SLP_NO_WAKEUP) && COM_long_interval())		// if rx delay not already set and all window intervals > 15
					{
						t = RTC_time_sec + 40;												// set 40s period for SMS second shot from this point in time
						if (t < RTC_SEC_PER_DAY)
							com_time.rx_delay = t;
					}
				}
				com_time.re_register = SLP_NO_WAKEUP;
				switch (MSG_tx_buffer[0])
				{
				// send stuff over ftp if ftp enabled, otherwise discard
				case MSG_TYPE_FTP_MSG:						// send message to FTP global outbox
				case MSG_TYPE_FTP_PART_DATA:				// automatic send logged data to FTP global outbox
					// check if ftp is enabled
					if (COM_schedule.ftp_enable)
						com_get_gprs();
					else
					{
						// remove message from outbox and tx buffer and get any more from outbox if present
						USB_monitor_string("FTP not enabled - msg deleted");
						com_log_error((uint16)__LINE__, COM_STATUS_NO_UPDATE);	// FTP not enabled - msg deleted
						MSG_remove(FTP_OUTBOX);
						com_pending_ftp_tx = true;			// check for more to tx
						com_state = COM_IDLE_WAIT;			// go to idle but stay awake
					}
					break;

				case MSG_TYPE_FTP_FILE:						// send a single file to FTP global outbox
				case MSG_TYPE_FTP_ALL_DATA:					// force a tx of logged data to FTP global outbox
					// states send stuff over ftp regardless of ftp enable state
					// these will only be discarded after registration retries have failed
					com_get_gprs();
					break;

				case MSG_TYPE_SMS_PDU:
					// select text mode PDU and wait for prompt
					sprintf(MDM_tx_buffer, "AT+CMGF=0;+CSMS=0;+CMGS=%u\r",(strlen(&MSG_tx_buffer[MSG_body_index])-2)/2);
					MDM_tx_delay_timer_x20ms = 20;			// 400ms delay
					MDM_send_cmd(MDM_tx_buffer);
					MDM_retry_ptr = NULL;					// only 1 attempt
					MDM_cmd_timer_x20ms = 5 * 50;			// 5s timeout until "> "
					com_state = COM_TX_SMS_1;
					break;

				case MSG_TYPE_SMS_TEXT:
					// select text mode SMS, status report request, add phone no. etc
					TSYNC_csmp_parameters(STR_buffer);
					sprintf(MDM_tx_buffer, "AT+CMGF=1;+CSMP=%s;+CMGS=\"%s\"\r", STR_buffer, &MSG_tx_buffer[3]);
					MDM_tx_delay_timer_x20ms = 20;			// 400ms delay
					MDM_send_cmd(MDM_tx_buffer);
					MDM_retry_ptr = NULL;					// only 1 attempt
					MDM_cmd_timer_x20ms = 5 * 50;			// 5s timeout until "> "
					com_state = COM_TX_SMS_1;
					break;

				default:
					// we should not have faulty messages in the outboxes but just in case of corruption or interference:
					sprintf(STR_buffer, "Invalid message type: ASCII %02XH", MDM_tx_buffer[0]);
					USB_monitor_string(STR_buffer);
					com_log_error((uint16)__LINE__, COM_STATUS_NO_UPDATE);	// Invalid message type
					// give up - delete contents
					if (MSG_files_in_sms_outbox > 0)
					{
						MSG_remove(SMS_OUTBOX);
						com_pending_sms_tx = true;			// look for any more
					}
					else
					{
						MSG_remove(FTP_OUTBOX);
						com_pending_ftp_tx = true;			// look for any more
					}
					com_state = COM_IDLE_WAIT;			// go to idle but stay awake
					break;
				}
			}
		}
		break;

	case COM_TX_SMS_1:							// Wait for prompt, then send message
		if (strstr(MDM_rx_buffer, "> "))
		{
			USB_monitor_string(MDM_rx_buffer);
			sprintf(MDM_tx_buffer, "%s\x1A", &MSG_tx_buffer[MSG_body_index]);	// Add ^Z at end
			MDM_tx_delay_timer_x20ms = 10;
			MDM_send_cmd(MDM_tx_buffer);
			MDM_retry_ptr = NULL;				// only 1 attempt allowed
			MDM_cmd_timer_x20ms = 60 * 50;		// 60 sec timeout

			com_state = COM_TX_SMS_2;
		}
		else if (!MDM_cmd_in_progress())		// failed to get prompt
		{
			USB_monitor_string("No SMS prompt from modem");
			com_log_error((uint16)__LINE__, COM_STATUS_NO_UPDATE);	// No SMS prompt from modem
			com_retry_sms_message();
		}
		break;

	case COM_TX_SMS_2:
		if (!MDM_cmd_in_progress())
		{
			if (MDM_cmd_status == MDM_CMD_SUCCESS)
			{
				// at this point whole of "+CMGS: xx\r\nOK\r\n" is is rx buffer
				cptr = strstr(MDM_rx_buffer, "+CMGS: ");
				if (cptr != NULL)						// got CMGS
				{
					cptr += 7;
					// parse message ID and save submit time if neccessary
					TSYNC_parse_id(cptr);
					if (!MSG_remove(SMS_OUTBOX))		// wait for file system - clear buffer and remove from outbox - message gone
						break;
					com_tx_attempts = 0;

					com_log_error((uint16)__LINE__, COM_STATUS_NO_UPDATE);	// SMS success
					MDM_tx_delay_timer_x20ms = 25;		// tx delay of 500ms
					com_finished();
				}
				else
					com_retry_sms_message();
			}
			else
				com_retry_sms_message();
		}
		break;

	case COM_CHECK_REGISTRATION:
		if (!MDM_cmd_in_progress())
		{
			if (strstr(MDM_rx_buffer, "0,1") || strstr(MDM_rx_buffer, "0,5"))	// registered
			{
				MDM_cmd_timer_x20ms = (com_tx_attempts + 1) * 100;	// delay before tx
				com_state = COM_TX_WAIT;
				com_time.re_register = SLP_NO_WAKEUP;
			}
			else																// unregistered
			{
				com_retry_registration();
			}
		}
		break;

	case COM_RX_SMS_1:
		if (MDM_ready(false))							// now powered-up & signed on OR after failure
		{
			if (MDM_state == MDM_FAIL_TO_REGISTER)		// failed to sign on GSM network
				com_retry_registration();
			else
			{
				if (!COM_mdm_stdby_required && !com_sms_rx_second_shot)						// if not in standby and this is first shot
				{
					if ((com_time.rx_delay == SLP_NO_WAKEUP) && COM_long_interval())		// if rx delay not already set and all window intervals > 15
					{
						t = RTC_time_sec + 40;												// set 40s period for SMS second shot from this point in time
						if (t < RTC_SEC_PER_DAY)
							com_time.rx_delay = t;
					}
				}
				com_time.re_register = SLP_NO_WAKEUP;
				MDM_send_cmd("AT+CSQ\r");				// check for more received - get signal quality
				com_state = COM_CHECK_CSQ;
			}
		}
		break;

	case COM_CHECK_CSQ:
		if (!MDM_cmd_in_progress())
		{
			cptr = NULL;
			COM_csq = 0;
			if (MDM_cmd_status == MDM_CMD_SUCCESS)		// parse signal strength
			{
				cptr = strstr(MDM_rx_buffer, "CSQ: ");
				if (cptr != NULL)						// got CSQ
				{
					cptr += 4;
					if (sscanf(cptr, " %d", &COM_csq) != 1)
						COM_csq = 0;
				}
			}

			MDM_send_cmd("AT#E2SMSRI=100;+CPMS?\r");		// get message slots
			MDM_tx_delay_timer_x20ms = 50;
			com_state = COM_CHECK_CPMS_1;
		}
		break;

	case COM_CHECK_CPMS_1:
	case COM_CHECK_CPMS_2:
		if (!MDM_cmd_in_progress())							// command done, or timeout
		{
			cptr = NULL;
			if (MDM_cmd_status == MDM_CMD_SUCCESS)			// parse message slots
			{
				cptr = strstr(MDM_rx_buffer, "\"SM\",");
				if (cptr != NULL)							// got "SM",
				{
					cptr += 5;
					if (sscanf(cptr, " %d,%d,", &i, &com_sms_slot_count) != 2)
						cptr = NULL;														// garbled number of messages
					else if (i == 0)														// no messages to read
					{																		// if we are about to power the modem down, stay awake for a while instead
																							// to get any further incoming messages
						if (!COM_mdm_stdby_required && 										// if not in standby
						   (com_sms_rx_second_shot == false) && 							// and if not doing second try 
						   (com_time.rx_delay != SLP_NO_WAKEUP))							// and if an interval has been set
						{
							if (COM_long_interval() && 										// if no short window intervals set
							    (((int32)com_time.rx_delay - (int32)RTC_time_sec) < 40))	// and delay time less than 40 seconds
							{																// if all window intervals > 15 minutes and time delay positive and less than 40 seconds
								HDW_MODEM_DTR_N = true;										// modem can standby and processor can sleep
								com_state = COM_RX_DELAY;									// in case any more SMSs arrive
								com_update_wakeup_time_sec(com_time.rx_delay);				// retry in time specified
							}
							else
							{
								com_time.rx_delay = SLP_NO_WAKEUP;							// clear rx delay time
								com_finished();												// power down modem or standby
							}
						}
						else
							com_finished();				// power down modem or standby
					}
					else								// check message slots in sequence
					{
						com_sms_current_slot = 0;
						com_n_sms_to_read = i;
						com_read_next_sms();
					}
				}
			}

			if (cptr == NULL)									// didn't get expected response
			{
				if (com_state == COM_CHECK_CPMS_1)				// retry command
				{
					MDM_send_cmd("AT#E2SMSRI=100;+CPMS?\r");	// get message slots
					MDM_tx_delay_timer_x20ms = 500;				// nice long timeout
					com_state = COM_CHECK_CPMS_2;
				}
				else
				{
					com_log_error((uint16)__LINE__, COM_STATUS_NO_UPDATE);	// Invalid message counts
					com_finished();					// power down modem or standby
				}
			}
		}
		break;

	case COM_RX_SMS_3:
		if (!MDM_cmd_in_progress())
		{
			// The GE864 gives +CMS ERROR: 321 if there is no message in the slot.
			// The GL865 just gives "OK", and is also cool about deleting a non-existent message.
			if (MDM_cmd_status == MDM_CMD_SUCCESS)		// message present, or GL865 empty slot
			{
				com_n_sms_to_read--;
				com_get_sender_number();
				cptr = strstr(MDM_rx_buffer, "\"\r\n");
#ifdef HDW_MODEM_GE864
				if (COM_sms_sender_number[0] == '\0')
				{
					USB_monitor_string("Invalid SMS reply number");
					com_log_error((uint16)__LINE__, COM_STATUS_NO_UPDATE);	// Invalid SMS reply number
				}
				else
#endif
				if (cptr == NULL)								// not a potential command
				{
					// Attempt to parse a status report. Look for <fo> of 6
					cptr = strstr(MDM_rx_buffer, ",6,");
					if (cptr != NULL)
					{
						cptr += 3;								// point to <mr> - message reference number
						TSYNC_parse_status(cptr);
					}
#ifndef HDW_MODEM_GE864											// GL865
					else if (COM_sms_sender_number[0] == '\0')	// assume empty slot
					{
						com_n_sms_to_read++;					// we decremented above
						com_read_next_sms();
						break;									// don't try to delete this (empty slot)
					}
#endif
					else
					{
						USB_monitor_string("SMS text not understood");
						com_log_error((uint16)__LINE__, COM_STATUS_NO_UPDATE);	// SMS text not understood
					}
				}
				else
				{
					// sms text & potential command - message ended in "\r\n
					cptr += 3;
					if (*cptr == CMD_CHARACTER)
					{
						CMD_schedule_parse(CMD_SOURCE_SMS, cptr, COM_output_buffer);
						com_state = COM_EXECUTE_SMS;
						break;								// don't delete message yet
					}
				}
				sprintf(MDM_tx_buffer, "AT+CMGD=%u\r", com_sms_current_slot);
				MDM_send_cmd(MDM_tx_buffer);
				com_state = COM_DELETE_SMS;
			}
			else if (strstr(MDM_rx_buffer, "+CMS ERROR: 321") != NULL)	// no message
				com_read_next_sms();
			else
			{
				USB_monitor_string("Failed to read SMS");
				com_log_error((uint16)__LINE__, COM_STATUS_NO_UPDATE);	// Failed to read SMS
				com_finished();
			}
		}
		break;

	case COM_EXECUTE_SMS:
		if (!CMD_busy(CMD_SOURCE_SMS))			// Done
		{
			MSG_send(MSG_TYPE_SMS_TEXT, COM_output_buffer, COM_sms_sender_number);
			MSG_flush_outbox_buffer(true);
			sprintf(MDM_tx_buffer, "AT+CMGD=%u\r", com_sms_current_slot);
			MDM_send_cmd(MDM_tx_buffer);
			com_state = COM_DELETE_SMS;
		}
		break;

	case COM_RX_DELAY:
		if ((com_time.rx_delay <= RTC_time_sec)	||										// if delay time has expired
			(com_time.rx_delay == SLP_NO_WAKEUP))										// or no interval has been set
		{
			com_sms_rx_second_shot = true;
			com_time.rx_delay = SLP_NO_WAKEUP;
			if (!com_gprs_fail)					// if this is not part of a gprs fail sequence
				com_pending_sms_tx = true;		// check for tx messages
			com_pending_sms_rx = true;
			com_state = COM_WAIT_FOR_FS;
		}
		break;

	case COM_DELETE_SMS:
		if (!MDM_cmd_in_progress())
		{
			// If timeout, we've tried twice to delete this message but it won't go.
			// Just read the next one anyway, as if the modem has died, we will
			// time out of the read message state.
			com_pending_sms_tx = true;		  // check for further messages
			com_pending_sms_rx = true;
			com_gprs_fail = false;				// do this for sanity
			com_state = COM_WAIT_FOR_FS;
		}
		break;

	case COM_QUERY_GPRS:
		if (!MDM_cmd_in_progress())
		{
			if (strstr(MDM_rx_buffer, "GPRS") != NULL)
			{
				if (strstr(MDM_rx_buffer, "1") != NULL)			// if activated
					com_gprs_connected();
				else											// not activated
				{
					sprintf(MDM_tx_buffer, "at#gprs=1\r");		// enable context
					MDM_tx_delay_timer_x20ms = 10 * 50;			// 10s delay
					MDM_send_cmd(MDM_tx_buffer);
					MDM_cmd_timer_x20ms = 30 * 50;				// timeout to wait for connection
					MDM_retry_ptr = NULL;						// only 1 attempt
					com_state = COM_SET_GPRS_1;
				}
			}
			else												// have to look for sms rx and try again later
				com_gprs_fail_action();
		}
		break;

	case COM_SET_GPRS_1:
	case COM_SET_GPRS_2:
	case COM_SET_GPRS_3:
		if (!MDM_cmd_in_progress())
		{
			if ((MDM_cmd_status == MDM_CMD_SUCCESS) && (strstr(MDM_rx_buffer, "IP") != NULL))
				com_gprs_connected();
			else if (com_state < COM_SET_GPRS_3)				// retry after 1s
			{
				sprintf(MDM_tx_buffer, "at#gprs=1\r");			// enable context
				MDM_tx_delay_timer_x20ms = 10 * 50;				// 10s delay
				MDM_send_cmd(MDM_tx_buffer);
				MDM_cmd_timer_x20ms = 30 * 50;					// 30s timeout for retries
				MDM_retry_ptr = NULL;							// only 1 attempt
				com_state++;									// -> COM_SET_GPRS_2 or 3
			}
			else
			{
				USB_monitor_string("No IP");
				com_log_error((uint16)__LINE__, COM_STATUS_GPRS_FAIL);	// No IP
				if (com_tsync_attempt_count != 0)
					com_tsync_attempt_count--;					// decrement retry count
				if (COM_roaming_enabled)
					COM_gsm_network_id = 0;						// roam next time
																// else stay locked to the configured network, or the home network
				com_gprs_fail_action();							// have to look for sms rx and try again later
			}
		}
		break;

	case COM_TX_FTP_3:
		// wait for OK or error
		if (!MDM_cmd_in_progress())
		{
			// wait for OK or "Already connected" error
			if ((MDM_cmd_status == MDM_CMD_SUCCESS) || (strstr(MDM_rx_buffer, "Already connected") != NULL))
			{
				COM_sign_on_status = COM_STATUS_OK;

				// if sending message
				if (MSG_tx_buffer[0] == MSG_TYPE_FTP_MSG)
				{
					// set up path "global_outbox" and extend filename from message buffer in com_server_filename
					com_create_message_filename(&MSG_tx_buffer[3]);
				}
				// else if sending all ftp data for a day or partial ftp data
				else if ((MSG_tx_buffer[0] == MSG_TYPE_FTP_ALL_DATA) || (MSG_tx_buffer[0] == MSG_TYPE_FTP_PART_DATA))
				{
					// check on presence of source data first - this message may be a relic of a previous life
					// check that files are present for this date
					if (!com_parse_ftp_date(&MSG_tx_buffer[3]))
					{
						USB_monitor_string("Date fail f3");
						com_log_error((uint16)__LINE__, COM_STATUS_NO_UPDATE);	// Date fail ftp3
						// close down and delete message immediately - no point retrying
						MSG_remove(FTP_OUTBOX);			// clear buffer and remove message - message error
						MDM_cmd_timer_x20ms = 50;		// 1s delay
						com_state = COM_TX_FTP_CLOSE;	// go and close ftp link
						break;
					}
					com_ftp_timestamp.reg32[0] = 0;
					if ((FTP_flag_normal_files_present(&com_ftp_timestamp) == 0) && (FTP_flag_derived_files_present(&com_ftp_timestamp) == 0))
					{
						USB_monitor_string("No data for date f3");
						com_log_error((uint16)__LINE__, COM_STATUS_NO_UPDATE);	// No data for the specified date ftp3
						// close down and delete message immediately - no point retrying
						MSG_remove(FTP_OUTBOX);			// clear buffer and remove message - message error
						MDM_cmd_timer_x20ms = 50;		// 1s delay
						com_state = COM_TX_FTP_CLOSE;	// go and close ftp link
						break;
					}
					else
					{
						// set up path "global_outbox" and create filename from timestamp
						// filenames are different for all or part data
						// if all data
						if (MSG_tx_buffer[0] == MSG_TYPE_FTP_ALL_DATA)
						{
							// if derived add "-d" to end of filename
							// extract flags from message body
							if (sscanf(&MSG_tx_buffer[MSG_body_index], "%x", &com_ftp_file_flags) == 1)
							{
								// extract derived flag from file flags
								derived = ((com_ftp_file_flags & 0x8000) == 0x8000);
								if (derived)
									sprintf(MDM_tx_buffer, "20%02x_%02x_%02x-d.txt", com_ftp_timestamp.yr_bcd, com_ftp_timestamp.mth_bcd, com_ftp_timestamp.day_bcd);
								else
									sprintf(MDM_tx_buffer, "20%02x_%02x_%02x.txt", com_ftp_timestamp.yr_bcd, com_ftp_timestamp.mth_bcd, com_ftp_timestamp.day_bcd);
							}
							else
							{
								USB_monitor_string("Message flag fault f3");
								com_log_error((uint16)__LINE__, COM_STATUS_NO_UPDATE);	// message flag fault ftp3
								// close down and delete message immediately - no point retrying
								MSG_remove(FTP_OUTBOX);			// clear buffer and remove message - message error
								MDM_cmd_timer_x20ms = 50;		// 1s delay
								com_state = COM_TX_FTP_CLOSE;	// go and close ftp link
								break;
							}
						}
						else		// part data adds a sequence number
						{
							// increment partial data sequence number here - this will increment for EVERY attempt to send a partial FTP
							com_ftp_sequence++;
							sprintf(MDM_tx_buffer,
									"20%02x_%02x_%02x-%u.txt",
									com_ftp_timestamp.yr_bcd, com_ftp_timestamp.mth_bcd, com_ftp_timestamp.day_bcd, com_ftp_sequence);
						}
						// create logdata filename in com_server_filename
						com_create_logdata_filename(MDM_tx_buffer);
					}
				}
				// else if sending file
				else if (MSG_tx_buffer[0] == MSG_TYPE_FTP_FILE)
				{
					// check on presence of source file first - this message may be a relic of a previous life
					strcpy(COM_ftp_filename, &MSG_tx_buffer[3]);
					strcpy(com_ftp_path, &MSG_tx_buffer[MSG_body_index]);
					// if file does not exist
					if (com_find_file(com_ftp_path, COM_ftp_filename) == 0)
					{
						USB_monitor_string("No such file f3");
						com_log_error((uint16)__LINE__, COM_STATUS_NO_UPDATE);	// No such file tx ftp3
						// close down and delete message immediately - no point retrying
						MSG_remove(FTP_OUTBOX);			// clear buffer and remove message - message error
						MDM_cmd_timer_x20ms = 50;		// 1s delay
						com_state = COM_TX_FTP_CLOSE;	// go and close ftp link
						break;
					}
					else	// create extended filename ("<serial number>_logdata_<path>_filename" from message buffer)
						com_create_extended_filename(&MSG_tx_buffer[MSG_body_index], &MSG_tx_buffer[3]);
				}
				// everything ok if reach here

				// set up to send file with extended filename to ftp server global_outbox directory
#ifdef HDW_1FM
				if ((MSG_tx_buffer[0] == MSG_TYPE_FTP_MSG) && (memcmp(&MSG_tx_buffer[MSG_body_index], "dAFS", 4) == 0))
					sprintf(MDM_tx_buffer, "at#ftptype=0;#ftpcwd=1fm_outbox;#ftpput=\"Delta_%s\"\r", com_server_filename);
				else
					sprintf(MDM_tx_buffer, "at#ftptype=0;#ftpcwd=global_outbox;#ftpput=\"Delta_%s\"\r", com_server_filename);
#else
				sprintf(MDM_tx_buffer, "at#ftptype=0;#ftpcwd=global_outbox;#ftpput=\"Delta_%s\"\r", com_server_filename);
#endif

				MDM_send_cmd(MDM_tx_buffer);
				MDM_retry_ptr = NULL;			// only 1 attempt
				MDM_cmd_timer_x20ms = 35 * 50;	// 35s to wait for "CONNECT" reply
				com_state = COM_TX_FTP_4;
			}
			else	// got server error
			{
				USB_monitor_string("Server error f3");
				com_log_error((uint16)__LINE__, COM_STATUS_SERVER_ERROR);	// Server error f3
				// if here have to look for sms rx and try again later
				com_gprs_fail_action();
			}
		}
		break;

	case COM_TX_FTP_4:
		if (strstr(MDM_rx_buffer, "CONNECT\r\n") != NULL)
		{
			// FTP connected OK
			// if sending message
			if (MSG_tx_buffer[0] == MSG_TYPE_FTP_MSG)
			{
				// save size of message
				com_ftp_bytes_sent = strlen(&MSG_tx_buffer[MSG_body_index]);
				// send the message
				MDM_send_cmd(&MSG_tx_buffer[MSG_body_index]);
				MDM_retry_ptr = NULL;			// only 1 attempt
				com_state = COM_TX_FTP_6;
			}
			// else if sending file
			else if (MSG_tx_buffer[0] == MSG_TYPE_FTP_FILE)
			{
				// we will get whole file
				strcpy(COM_ftp_filename, &MSG_tx_buffer[3]);
				strcpy(com_ftp_path, &MSG_tx_buffer[MSG_body_index]);
				com_ftp_file_seek_pos = 0;
				com_ftp_file_end_pos = com_find_file(com_ftp_path, COM_ftp_filename);
				// save size of file
				com_ftp_bytes_sent = com_ftp_file_end_pos + 1;
				// if file does not exist - double double check - should not get here if so
				// but still trap it and exit gracefully
				if (com_ftp_file_end_pos == 0)
				{
					USB_monitor_string("No such file f4");
					com_log_error((uint16)__LINE__, COM_STATUS_NO_UPDATE);	// No such file f4
					// close down and delete message immediately - no point retrying
						MSG_remove(FTP_OUTBOX);			// clear buffer and remove message - message error
					MDM_cmd_timer_x20ms = 1 * 50;	// 1s delay
					com_state = COM_TX_FTP_ABORT;	// go and close ftp link
				}
				else
				{
					MDM_cmd_timer_x20ms = 1 * 50;	// 1s delay
					com_state = COM_TX_FTP_5;
				}
			}
			else	// FTP_ALL_DATA or PART_DATA
			{
				// retrieve message body index - local index gets incremented through message body during data tx
				// so we need to reset it here in case this is a retry of the same message
				com_msg_body_index = MSG_body_index;
				// extract flags from message body
				if (sscanf(&MSG_tx_buffer[com_msg_body_index], "%x", &com_ftp_file_flags) == 1)
				{
					// extract derived flag from file flags
					derived = ((com_ftp_file_flags & 0x8000) == 0x8000);
					// limit file flags to the 11 allowed
					com_ftp_file_flags &= 0x07FF;
					if (com_ftp_file_flags != 0)
					{
						// if part data
						if (MSG_tx_buffer[0] == MSG_TYPE_FTP_PART_DATA)
						{
							// set index to first channel seek pos in message body
							com_msg_body_index += 5;
							// set index to first channel timestamp in filename part of message
							com_ftp_time_index = 10;
						}
						// set up mask and channel offset
						com_ftp_file_mask = 0x0001;
						com_ftp_data_channel = derived ? FTP_DERIVED_BASE_INDEX : FTP_BASE_FTPR_INDEX;
						// get ready to count up bytes sent
						com_ftp_bytes_sent = 0;
						com_state = COM_TX_FTP_DATA_1;
						break;
					}
				}
				// else
				USB_monitor_string("file flag fault f4");
				com_log_error((uint16)__LINE__, COM_STATUS_NO_UPDATE);	// file flag fault f4
				// close down and delete message immediately - nothing more to do as message is faulty
				MSG_remove(FTP_OUTBOX);			// clear buffer and remove msg from outbox - message error
				MDM_cmd_timer_x20ms = 1 * 50;	// 1s delay
				com_state = COM_TX_FTP_ABORT;	// go and close ftp link
			}
		}
		else if (!MDM_cmd_in_progress())
		{
			USB_monitor_string("No CONNECT f4");
			com_log_error((uint16)__LINE__, COM_STATUS_FTP_FAIL);	// No CONNECT
			// if here have to look for sms rx and try again later
			com_gprs_fail_action();
		}
		break;

	case COM_TX_FTP_MDM_WAIT:
		// wait for modem tx buffer to be empty
		if (MDM_tx_ptr == NULL)
		{
			// then set a small delay
			MDM_cmd_timer_x20ms = 10;						// 200s timeout
			com_state = COM_TX_FTP_5;
		}
		break;

	case COM_TX_FTP_5:
		// getting file contents
		if (MDM_cmd_timer_x20ms == 0)
		{
			com_ftp_block_size = 0;
			// check if any more to get (end pos and seek pos are inclusive)
			if (com_ftp_file_end_pos >= com_ftp_file_seek_pos)
			{
				com_ftp_block_size = com_ftp_file_end_pos - com_ftp_file_seek_pos + 1;
				// limit size of block to get
				if (com_ftp_block_size > COM_FTP_FILE_BLOCK_SIZE)
				{
					com_ftp_block_size = COM_FTP_FILE_BLOCK_SIZE;
				}
			}
			// if get a block or part of a block successfully
			if (CFS_read_block(com_ftp_path, COM_ftp_filename, MDM_tx_buffer, com_ftp_file_seek_pos, com_ftp_block_size) == true)
			{
				MDM_tx_buffer[com_ftp_block_size] = '\0';		// terminate block (partial block will be terminated with eof '\0')
				MDM_tx_delay_timer_x20ms = 25;					// wait 500ms
				MDM_send_cmd(MDM_tx_buffer);
				MDM_retry_ptr = NULL;							// only 1 attempt
				com_ftp_file_seek_pos += com_ftp_block_size;	// next block
				com_state = COM_TX_FTP_MDM_WAIT;				// go and wait for modem tx buffer to empty
			}
			else	// no more blocks in file
			{
				// complete tx
				MDM_cmd_timer_x20ms = 50;		// 1s delay
				com_state = COM_TX_FTP_6;
			}
		}
		break;

/****************************** FTP DATA TX engine *****************************************/

	case COM_TX_FTP_DATA_1:
		// for each channel with a flag
		if (com_ftp_file_flags & com_ftp_file_mask)
		{
			// if ALL DATA
			if (MSG_tx_buffer[0] == MSG_TYPE_FTP_ALL_DATA)
			{
				// create filename and path of channel data file using requested date in timestamp
				com_ftp_file_seek_pos = 0;
				com_ftp_file_end_pos = FTP_find_file(com_ftp_data_channel, &com_ftp_timestamp);
				// save size of file
				com_ftp_bytes_sent += (com_ftp_file_end_pos + 1);
				// if it exists (double check)
				if (com_ftp_file_end_pos > 0)
				{
					// set FTP_MDM state
					com_state = COM_TX_FTP_DATA_MDM;
					break;
				}
			}
			// else if PART_DATA
			else if (MSG_tx_buffer[0] == MSG_TYPE_FTP_PART_DATA)
			{
				// scan next seek and end pos from message content
				if (sscanf(&MSG_tx_buffer[com_msg_body_index], "%8lx", &com_ftp_file_seek_pos) != 1)
				{
					// if fault, end tx
					USB_monitor_string("seek pos fault fd1");
					com_log_error((uint16)__LINE__, COM_STATUS_NO_UPDATE);	// seek pos fault fd1
					// set COM_TX_FTP_6 state
					MDM_cmd_timer_x20ms = 50;		// 1s delay
					com_state = COM_TX_FTP_6;
					break;
				}
				com_msg_body_index += 9;				// skip to start of end pos
				if (sscanf(&MSG_tx_buffer[com_msg_body_index], "%8lx", &com_ftp_file_end_pos) != 1)
				{
					// if fault, end tx
					USB_monitor_string("end pos fault fd1");
					com_log_error((uint16)__LINE__, COM_STATUS_NO_UPDATE);	// end pos fault fd1
					// set COM_TX_FTP_6 state
					MDM_cmd_timer_x20ms = 50;		// 1s delay
					com_state = COM_TX_FTP_6;
					break;
				}
				com_msg_body_index += 9;				// skip to start of next seek pos
				// get next timestamp time from message filename area
				if (!com_parse_ftp_time(&MSG_tx_buffer[com_ftp_time_index]))
				{
					// if fault, end tx
					USB_monitor_string("timestamp fault fd1");
					com_log_error((uint16)__LINE__, COM_STATUS_NO_UPDATE);	// timestamp fault fd1
					// set COM_TX_FTP_6 state
					MDM_cmd_timer_x20ms = 50;		// 1s delay
					com_state = COM_TX_FTP_6;
					break;
				}
				com_ftp_time_index += 7;			// skip to start of next timestamp time
				// create filename and path of channel data file using requested date
				FTP_set_filename_and_path(com_ftp_data_channel, &com_ftp_timestamp);
				// save size of part of file to be sent
				com_ftp_bytes_sent += ((com_ftp_file_end_pos - com_ftp_file_seek_pos) + 1);
				// if it exists (double check)
				if (com_ftp_file_end_pos > 0)
				{
					// if seek pos non zero
					if (com_ftp_file_seek_pos != 0)
					{
#ifndef HDW_RS485
						sensor_type_mask = ((com_ftp_data_channel % 2) == 0) ? 0x30 : 0xc0; 
						// if a digital event channel
						if ((com_ftp_data_channel < 4) && ((DIG_config[com_ftp_data_channel / 2].sensor_type & sensor_type_mask) != 0x00))
						{
							// if digital event value logging
							if (DIG_config[com_ftp_data_channel / 2].ec[com_ftp_data_channel % 2].sensor_type > 0)
								// create block header - identical format to logged file header - using message time stamp
								len = LOG_create_block_header(MDM_tx_buffer, com_ftp_data_channel, &com_ftp_timestamp);
							else
								// else create event header - identical format to logged file header - using message time stamp
								len = LOG_create_event_header(MDM_tx_buffer, com_ftp_data_channel + LOG_EVENT_1A_INDEX, &com_ftp_timestamp);
							// add to number of bytes to be sent
							com_ftp_bytes_sent += len;
						}
						else
#endif
						{
							// create block header - identical format to logged file header - using message time stamp
							len = LOG_create_block_header(MDM_tx_buffer, com_ftp_data_channel, &com_ftp_timestamp);
							// add to number of bytes to be sent
							com_ftp_bytes_sent += len;
						}
						// send it to ftp server
						MDM_send_cmd(MDM_tx_buffer);
						MDM_retry_ptr = NULL;							// only 1 attempt
					}
					// set FTP_MDM state
					com_state = COM_TX_FTP_DATA_MDM;
					break;
				}
			}
		}
		// else
		// no file flag set or no file
		// shift mask left and increment channel offset
		com_ftp_file_mask <<= 1;
		com_ftp_data_channel++;
		// if  no more channels (mask out of range)
		if (com_ftp_file_mask > 0x0400)
		{
			// end of tx
			// set COM_TX_FTP_6 state
			MDM_cmd_timer_x20ms = 50;		// 1s delay
			com_state = COM_TX_FTP_6;
		}
		// else stay in this state and try for another channel
		break;

	case COM_TX_FTP_DATA_MDM:
		// wait for modem tx buffer to be empty
		if (MDM_tx_ptr == NULL)
		{
			// then set a small delay
			MDM_cmd_timer_x20ms = 10;						// 200s timeout
			com_state = COM_TX_FTP_DATA_2;
		}
		break;

	case COM_TX_FTP_DATA_2:
		// getting file contents
		if (MDM_cmd_timer_x20ms == 0)
		{
			// do
				// get and send file block by block from current seek pos
				// set FTP_MDM between blocks

			com_ftp_block_size = 0;
			// check if any more to get (end pos and seek pos are inclusive)
			if (com_ftp_file_end_pos >= com_ftp_file_seek_pos)
			{
				com_ftp_block_size = com_ftp_file_end_pos - com_ftp_file_seek_pos + 1;
				// limit size of block to get
				if (com_ftp_block_size > COM_FTP_FILE_BLOCK_SIZE)
				{
					com_ftp_block_size = COM_FTP_FILE_BLOCK_SIZE;
				}
			}
			// if get a block or part of a block successfully
			if (CFS_read_block(FTP_path_str, FTP_filename_str, MDM_tx_buffer, com_ftp_file_seek_pos, com_ftp_block_size) == true)
			{
				MDM_tx_buffer[com_ftp_block_size] = '\0';		// terminate block (partial block will be terminated with eof '\0')
				MDM_tx_delay_timer_x20ms = 25;					// wait 500ms
				MDM_send_cmd(MDM_tx_buffer);
				MDM_retry_ptr = NULL;							// only 1 attempt
				com_ftp_file_seek_pos += com_ftp_block_size;	// next block
				com_state = COM_TX_FTP_DATA_MDM;				// go and wait for modem tx buffer to empty
				break;
			}
			// (else no more blocks in file)

			// until no more blocks

			// clear file flag bit
			com_ftp_file_flags ^= com_ftp_file_mask;
			// if file flags is not zero
			if (com_ftp_file_flags != 0)
			{
				// shift mask left and increment channel offset
				com_ftp_file_mask <<= 1;
				com_ftp_data_channel++;
				// if  valid channels (mask within range)
				if (com_ftp_file_mask < 0x0800)
				{
					// set COM_TX_FTP_DATA_1 for next channel
					com_state = COM_TX_FTP_DATA_1;
					break;
				}
			}
			// else
			// end of tx
			// set COM_TX_FTP_6 state
			MDM_cmd_timer_x20ms = 50;		// 1s delay
			com_state = COM_TX_FTP_6;
		}
		else if (!MDM_cmd_in_progress())
		{
			USB_monitor_string("No CONNECT d2");
			com_log_error((uint16)__LINE__, COM_STATUS_FTP_FAIL);	// No CONNECT d2
			// if here have to look for sms rx and try again later
			com_gprs_fail_action();
		}
		break;

/****************************** FTP abort (after opening file in server) ********************/
	case COM_TX_FTP_ABORT:
		// premature end of file transmission - send "+++"
		if (MDM_cmd_timer_x20ms == 0)
		{
			com_socket_disconnect();
			com_state = COM_TX_FTP_CLOSE;
		}
		break;

/****************************** FTP sign off sequence **************************************/
	case COM_TX_FTP_6:
		// end of file transmission - send "+++"
		if (MDM_cmd_timer_x20ms == 0)
		{
			com_socket_disconnect();
			com_state = COM_TX_FTP_7;
		}
		break;

	case COM_TX_FTP_7:
		if (strstr(MDM_rx_buffer, "NO CARRIER") != NULL)
		{
			USB_monitor_string(MDM_rx_buffer);

			com_ftp_slow_server = false;			// by default

			// If it takes less than 20 sec to get "NO CARRIER", assume FTP server is well-behaved
			// and only delete the message from the outbox if we can read its size.
			// Otherwise delete the message unconditionally (assume it went OK) and disconnect GPRS
			// before any further FTP
			if (MDM_cmd_timer_x20ms > COM_NO_CARRIER_TIMEOUT_X20MS - (20 * 50))		// took less than 20s
			{
				// ask for size of this file from server
				sprintf(MDM_tx_buffer, "at#ftpfsize=\"Delta_%s\"\r", com_server_filename);
				MDM_send_cmd(MDM_tx_buffer);
				MDM_retry_ptr = NULL;			// only 1 attempt
				MDM_cmd_timer_x20ms = 60 * 50;	// 60s to wait for file size
				com_state = COM_TX_FTP_10;
			}
			else	// took > 20s: assume file OK, but disconnect GPRS & do next file in a few minutes
			{
				MSG_remove(FTP_OUTBOX);				// assume file went
				com_pending_ftp_tx = false;			// don't try again until we've re-registered
				if (MSG_files_in_ftp_outbox > 0)	// more files to send though
				{
					com_ftp_slow_server = true;
					com_gprs_fail_action();
				}
				else								// outbox now empty
				{
					MDM_cmd_timer_x20ms = 50;		// 1s delay
					com_state = COM_TX_FTP_8;		// go and close ftp link
				}
			}
		}
		else if (!MDM_cmd_in_progress())
		{
			USB_monitor_string("No NO CARRIER f7");
			com_log_error((uint16)__LINE__, COM_STATUS_NO_UPDATE);	// No NO CARRIER f7
			// if here have to look for sms rx and try again later
			com_gprs_fail_action();
		}
		break;

	case COM_TX_FTP_10:
		// waiting for file size
		// wait for OK or error
		if (!MDM_cmd_in_progress())
		{
			cptr = strstr(MDM_rx_buffer, "FTPFSIZE:");
			// get FTPSIZE: and file_size
			if ((cptr != NULL) && (sscanf((cptr + 10), "%ld", &file_size) == 1))
			{
				// we have a file size - check against bytes sent
				if (file_size != com_ftp_bytes_sent)
				{
					// file contents did not get through to server
					if (++com_tx_attempts < com_transmission_retry_limit)
					{
						// if not exceeded retry limit, try message again
						USB_monitor_string("wrong number of bytes - retry f10");
						com_log_error((uint16)__LINE__, COM_STATUS_NO_UPDATE);	// incorrect number of bytes received retry f10
						MDM_send_cmd("AT#FTPCWD=..\r");		// go back to root directory
						MDM_retry_ptr = NULL;	 			// only 1 attempt
						MDM_cmd_timer_x20ms = 35 * 50;		// 35s to wait for "OK" reply
						com_state = COM_TX_FTP_3;
						break;
					}
					// else fall through as if it was successful and discard this message
				}
				// else successful
				com_tx_attempts = 0;
				// remove current message from outbox
				if (!MSG_remove(FTP_OUTBOX))
					break;									// wait here for file system, if neccessary
				// there may be another ftp file to send, so check on ftp outbox and get it
				if ((MSG_files_in_ftp_outbox > 0) && MSG_get_message_to_tx(FTP_OUTBOX))
				{
					// if it is ftp msg or file
					if ((MSG_tx_buffer[0] == MSG_TYPE_FTP_MSG) ||
						(MSG_tx_buffer[0] == MSG_TYPE_FTP_FILE) ||
						(MSG_tx_buffer[0] == MSG_TYPE_FTP_ALL_DATA) ||
						(MSG_tx_buffer[0] == MSG_TYPE_FTP_PART_DATA)
					   )
					{
						MDM_send_cmd("AT#FTPCWD=..\r");	// go back to root directory
						MDM_retry_ptr = NULL;	 		// only 1 attempt
						MDM_cmd_timer_x20ms = 35 * 50;	// 35s to wait for "OK" reply
						com_state = COM_TX_FTP_3;
						break;
					}
					else
					{
						// faulty message - remove it
						MSG_remove(FTP_OUTBOX);
					}
				}
				// check if pending ftp poll flag has been set again
				else if (com_pending_ftp_poll)
				{
					com_pending_ftp_poll = false;
					MDM_send_cmd("AT#FTPCWD=..\r");	// go back to root directory
					MDM_retry_ptr = NULL;	 		// only 1 attempt
					MDM_cmd_timer_x20ms = 35 * 50;	// 35s to wait for "OK" reply
					com_state = COM_POLL_FTP_4;
					break;
				}
				MDM_cmd_timer_x20ms = 50;		// 1s delay
				com_state = COM_TX_FTP_CLOSE;	// go and close ftp link
			}
			else 				// faulty reply
			{
				USB_monitor_string("No file size f10");
				com_log_error((uint16)__LINE__, COM_STATUS_NO_UPDATE);	// No file size f10
				// if here have to look for sms rx and try again later
				com_gprs_fail_action();
			}
		}
		break;

/****************************** FTP closedown sequence **************************************/
	case COM_TX_FTP_CLOSE:
		if (MDM_cmd_timer_x20ms == 0)
		{
			// close ftp after successful tx or rx - common exit point for TX_FTP and POLL_FTP with empty directory
			// leads to sensible shut down of server, then gprs, then modem
			MDM_send_cmd("AT#FTPCLOSE\r");	// close the ftp connection
			MDM_cmd_timer_x20ms = 40 * 50;	// 40s timeout
			MDM_retry_ptr = NULL;	 		// only 1 attempt
			com_tx_attempts = 0;
			com_state = COM_TX_FTP_8;
		}
		break;

	case COM_TX_FTP_8:
		if (!MDM_cmd_in_progress())				// FTP disconnected OK or timed out
		{
#ifdef HDW_1FM
			if (!MAG_modem_active)				// Don't shutdown GPRS between dAFS transmissions
#endif
			{
				MDM_send_cmd("AT#GPRS=0\r");
				MDM_cmd_timer_x20ms = 40 * 50;	// 40s timeout
			}
			com_state = COM_TX_FTP_9;
		}
		break;

	case COM_TX_FTP_9:
		// wait for OK after AT#GPRS=0
		if (!MDM_cmd_in_progress())
		{
			// GPRS cleared OK or timed out
			com_tx_attempts = 0;
			com_pending_sms_tx = true;		// further sms tx or rx as required
			com_pending_sms_rx = true;
			com_state = COM_IDLE_WAIT;			// go to idle but stay awake
		}
		break;
/****************************** end of FTP closedown sequence ********************************/

	case COM_SHUTTING_DOWN:
		if (MDM_ready(false))
			com_state = COM_IDLE;
		break;
/********************************************************************************************/

	case COM_POLL_FTP_1:								// Power up, sign on, send command
		if (!CFS_open())								// stay here until file system is open
			break;

		if (MDM_ready(false))							// now powered-up & signed-on OR after failure
		{
			if (MDM_state == MDM_FAIL_TO_REGISTER)		// failed to sign on GSM network
				com_gprs_fail_action();					// look for sms rx and try again later
			else
			{
				com_time.re_register = SLP_NO_WAKEUP;
				MSG_tx_buffer[0] = '\0';				// indicate that we're doing a poll
				com_get_gprs();
			}
		}
		break;

	case COM_POLL_FTP_4:
		// wait for OK or error from FTP logon
		if (!MDM_cmd_in_progress())
		{
			// wait for OK or "Already connected" error
			if ((MDM_cmd_status == MDM_CMD_SUCCESS) || (strstr(MDM_rx_buffer, "Already connected") != NULL))
			{
				// set up path (Delta/"<serial number>/inbox" and ask for directory listing)
				sprintf(MDM_tx_buffer, "at#ftpcwd=Delta/%s/Inbox\r", CAL_build_info.serial_number);
				MDM_send_cmd(MDM_tx_buffer);
				MDM_retry_ptr = NULL;			// only 1 attempt
				MDM_cmd_timer_x20ms = 35 * 50;	// 35s to wait for "OK" reply
				com_state = COM_POLL_FTP_CWD;
			}
			else		// got server error
			{
				USB_monitor_string("Server error p4");
				com_log_error((uint16)__LINE__, COM_STATUS_SERVER_ERROR);	// Server error p4
				// if here have to look for sms rx and try again later
				com_gprs_fail_action();
			}
		}
		break;

	case COM_POLL_FTP_CWD:
		if (!MDM_cmd_in_progress())
		{
			if (MDM_cmd_status == MDM_CMD_SUCCESS)
			{
				MDM_send_cmd("at#ftplist\r");
				MDM_retry_ptr = NULL;			// only 1 attempt
				MDM_cmd_timer_x20ms = 40 * 50;	// 40s to wait for directory listing
				com_state = COM_POLL_FTP_6;
			}
			else		// got server error - no such directory - not a killer (DEL24 fix pb 030310)
						// this must not close ftp down if next message is ftp (DEL61 fix pb 200410)
			{
				USB_monitor_string("No ftp inbox");
				com_log_error((uint16)__LINE__, COM_STATUS_NO_UPDATE);	// No ftp inbox
				// there may be another ftp file to send, so check on ftp outbox and get it
				if ((MSG_files_in_ftp_outbox > 0) && MSG_get_message_to_tx(FTP_OUTBOX))
				{
					// if it is ftp msg or file
					if ((MSG_tx_buffer[0] == MSG_TYPE_FTP_MSG) ||
						(MSG_tx_buffer[0] == MSG_TYPE_FTP_FILE) ||
						(MSG_tx_buffer[0] == MSG_TYPE_FTP_ALL_DATA) ||
						(MSG_tx_buffer[0] == MSG_TYPE_FTP_PART_DATA)
					   )
					{
						MDM_send_cmd("AT#FTPCWD=../../..\r");	// go back to root directory
						MDM_retry_ptr = NULL;	 		// only 1 attempt
						MDM_cmd_timer_x20ms = 35 * 50;	// 35s to wait for "OK" reply
						com_state = COM_TX_FTP_3;
						break;
					}
					else
					{
						// faulty message - remove it
						MSG_remove(FTP_OUTBOX);
					}
				}
				MDM_cmd_timer_x20ms = 50;										// 1s delay
				com_state = COM_TX_FTP_CLOSE;									// go and close ftp link
			}
		}
		break;

	case COM_POLL_FTP_6:
		// wait for "CMDxxx.txt" or "NO CARRIER"
		if (((strstr(MDM_rx_buffer, "CMD") != NULL) || (strstr(MDM_rx_buffer, "cmd") != NULL)) &&
			((strstr(MDM_rx_buffer, ".txt") != NULL) || (strstr(MDM_rx_buffer, ".TXT") != NULL)))
		{
			// we have a command file - parse it
			USB_monitor_string(MDM_rx_buffer);
			strcpy(STR_buffer, MDM_rx_buffer);
			if (!com_parse_ftp_command_filename())
			{
				// catastrophic parse fail - should not be here, so look for sms rx and try again later
				com_gprs_fail_action();
			}
			else
			{
				// successful command parse - go and wait for the "NO CARRIER" at end of directory
				MDM_cmd_timer_x20ms = 40 * 50;	// 40s to wait for whole directory to come
				com_state = COM_POLL_FTP_9;
			}
		}
		else if (strstr(MDM_rx_buffer, "NO CARRIER") != NULL)
		{
			// no directory or no CMDxxxx.txt files in it - all done - need to check on next message for ftp
			USB_monitor_string(MDM_rx_buffer);
			// there may be another ftp file to send, so check on ftp outbox and get it
			if ((MSG_files_in_ftp_outbox > 0) && MSG_get_message_to_tx(FTP_OUTBOX))
			{
				// if it is ftp msg or file
				if ((MSG_tx_buffer[0] == MSG_TYPE_FTP_MSG) ||
					(MSG_tx_buffer[0] == MSG_TYPE_FTP_FILE) ||
					(MSG_tx_buffer[0] == MSG_TYPE_FTP_ALL_DATA) ||
					(MSG_tx_buffer[0] == MSG_TYPE_FTP_PART_DATA)
				   )
				{
					MDM_send_cmd("AT#FTPCWD=../../..\r");	// go back to root directory
					MDM_retry_ptr = NULL;	 		// only 1 attempt
					MDM_cmd_timer_x20ms = 35 * 50;	// 35s to wait for "OK" reply
					com_state = COM_TX_FTP_3;
					break;
				}
				else
					MSG_remove(FTP_OUTBOX);					// faulty message - remove it
			}
			MDM_cmd_timer_x20ms = 50;		// 1s delay
			com_state = COM_TX_FTP_CLOSE;	// go and close ftp link
		}
		else if (!MDM_cmd_in_progress())
		{
			USB_monitor_string("No directory p6");
			com_log_error((uint16)__LINE__, COM_STATUS_NO_UPDATE);	// No directory p6
			// if here have to look for sms rx and try again later
			com_gprs_fail_action();
		}
		break;

	case COM_POLL_FTP_9:
		// wait for "NO CARRIER" after #ftplist
		if (strstr(MDM_rx_buffer, "NO CARRIER\r") != NULL)
		{
			// get command file and download it
			sprintf(MDM_tx_buffer, "at#ftpget=%s\r", COM_ftp_filename);
			MDM_send_cmd(MDM_tx_buffer);
			MDM_retry_ptr = NULL;			// only 1 attempt
			MDM_cmd_timer_x20ms = 40 * 50;	// 40s to wait to get file
			com_state = COM_RX_FTP_1;
		}
		// else if get \r without NO CARRIER, need to throw away line of input by resetting rx buffer
		// so we can cope with long directory listings
		else if (strstr(MDM_rx_buffer, "\r") != NULL)
			MDM_clear_rx_buffer();
		else if (!MDM_cmd_in_progress())
		{
			USB_monitor_string("No NO CARRIER p9");
			com_log_error((uint16)__LINE__, COM_STATUS_NO_UPDATE);	// No NO CARRIER p9
			// if here have to look for sms rx and try again later
			com_gprs_fail_action();
		}
		break;

	case COM_RX_FTP_1:	// need to extract command from file and act on it
						// get line from file into a static buffer
						// give CMD_schedule_parse a ptr to this buffer
						// and a ptr to an output buffer from which an ftp reply file can be built
						// need to remember file name so we can use it for reply file in outbox
		// wait for "CONNECT" and following cr
		if (strstr(MDM_rx_buffer, "CONNECT\r\n") != NULL)
		{
			MDM_cmd_timer_x20ms = 40 * 50;	// 40s to wait for file or "NO CARRIER" reply
			com_state = COM_RX_FTP_2;
		}
		else if (!MDM_cmd_in_progress())
		{
			USB_monitor_string("No CONNECT r1");
			com_log_error((uint16)__LINE__, COM_STATUS_FTP_FAIL);	// No CONNECT r1
			// if here have to look for sms rx and try again later
			com_gprs_fail_action();
		}
		break;

	case COM_RX_FTP_2:
		// downloading file contents
		// wait for '#' and the following '\r'
		cptr = strstr(MDM_rx_buffer, CMD_CHARACTER_STRING);
		if (cptr != NULL)
		{
			// copy string from # until get '\r'
			strcpy(STR_buffer, cptr);
			if (strstr(STR_buffer, "\r") != NULL)
			{
				// we have a command - act on it
				USB_monitor_string(STR_buffer);
				FTP_act_on_ftp_command();
				// go and wait for the "NO CARRIER" at end of the file
				MDM_cmd_timer_x20ms = 10 * 50;	// 10s to wait for whole file to come
				com_state = COM_RX_FTP_3;
			}
			else if (strlen(STR_buffer) >= 161)
			{
				USB_monitor_string("cmd too long r2");
				com_log_error((uint16)__LINE__, COM_STATUS_NO_UPDATE);	// cmd too long r2
				// go and wait for the "NO CARRIER" at end of the file
				MDM_cmd_timer_x20ms = 10 * 50;	// 10s to wait for whole file to come
				com_state = COM_RX_FTP_3;
			}
		}

		// if still in this state, check for overflow or timeout:
		if (com_state == COM_RX_FTP_2)		// haven't gone to RX_FTP_3 yet
		{
			if (strlen(MDM_rx_buffer) >= 256)
			{
				USB_monitor_string("cmd too long r2B");
				com_log_error((uint16)__LINE__, COM_STATUS_NO_UPDATE);	// cmd too long r2
				// go and wait for the "NO CARRIER" at end of the file
				MDM_cmd_timer_x20ms = 10 * 50;	// 10s to wait for whole file to come
				com_state = COM_RX_FTP_3;
			}
			else if (MDM_cmd_timer_x20ms == 0)
			{
				USB_monitor_string("time out r2");
				com_log_error((uint16)__LINE__, COM_STATUS_NO_UPDATE);	// time out r2
				// if here have to look for sms rx and try again later
				com_gprs_fail_action();
			}
		}
		break;

	case COM_RX_FTP_3:
		// wait for "NO CARRIER" after file contents
		if (strstr(MDM_rx_buffer, "NO CARRIER") != NULL)
		{
			// end of command download - delete it
			sprintf(MDM_tx_buffer, "at#ftpdele=%s\r", COM_ftp_filename);
			MDM_send_cmd(MDM_tx_buffer);
			MDM_cmd_timer_x20ms = 4 * 50;		// 4s timeout
			MDM_retry_ptr = NULL;	 			// only 1 attempt
			com_state = COM_RX_FTP_4;			// go and wait for ok
		}
		else if (!MDM_cmd_in_progress())
		{
			USB_monitor_string("No NO CARRIER r3");
			com_log_error((uint16)__LINE__, COM_STATUS_NO_UPDATE);	// No NO CARRIER r3
			// if here have to look for sms rx and try again later
			com_gprs_fail_action();
		}
		break;

	case COM_RX_FTP_4:
		if (!MDM_cmd_in_progress())				// wait for OK after file delete and command to finish
		{
			if (!CMD_busy(CMD_SOURCE_FTP))
			{
				// if ftp transfer pending (was a #FWR command)
				if (com_ftp_transfer_file_pending)
				{
					if (!CFS_open())								// stay here until file system is open
						break;

					com_ftp_transfer_file_pending = false;
					// delete target file if it exists
					if (CFS_state == CFS_OPEN)
					{
						if (FSchdir((FTP_path_str[0] == '\0') ? "\\" : FTP_path_str) == 0)
							FSremove(FTP_filename_str);
					}

					// send AT#FTPGETPKT filename
					sprintf(MDM_tx_buffer, "at#ftpgetpkt=%s\r", FTP_filename_str);
					MDM_send_cmd(MDM_tx_buffer);
					MDM_retry_ptr = NULL;			// only 1 attempt
					MDM_cmd_timer_x20ms = 120 * 50;	// 2m to wait for file buffering
					// go and wait for response
					com_state = COM_TRANSFER_FTP_1;
				}
				else
				{
					// ask for directory listing
					sprintf(MDM_tx_buffer, "at#ftplist\r");
					MDM_send_cmd(MDM_tx_buffer);
					MDM_retry_ptr = NULL;			// only 1 attempt
					MDM_cmd_timer_x20ms = 40 * 50;	// 40s to wait for directory listing
					com_state = COM_POLL_FTP_6;
				}
			}
			else if (MDM_cmd_timer_x20ms == 0)
			{
				USB_monitor_string("No OK r4");
				com_log_error((uint16)__LINE__, COM_STATUS_NO_UPDATE);	// No OK r4
				// if here have to look for sms rx and try again later
				com_gprs_fail_action();
			}
		}
		break;

	case COM_TRANSFER_FTP_1:
		// wait for OK or error from FTPGETPKT
		if (!MDM_cmd_in_progress())
		{
			// wait for OK
			if (MDM_cmd_status == MDM_CMD_SUCCESS)
			{
				// request first block of file
				sprintf(MDM_tx_buffer, "at#ftprecv=480\r");
				MDM_send_cmd(MDM_tx_buffer);
				MDM_retry_ptr = NULL;							// only 1 attempt
				MDM_cmd_timer_x20ms = 10 * 50;					// 10s to wait for reply
				com_state = COM_TRANSFER_FTP_2;
			}
			else		// timeout or ERROR received - got server error
			{
				USB_monitor_string("Server error ftpt1");
				com_log_error((uint16)__LINE__, COM_STATUS_SERVER_ERROR);	// Server error ftpt1
				// if here have to look for sms rx and try again later
				com_gprs_fail_action();
			}
		}
		break;

	case COM_TRANSFER_FTP_2:
		// wait for OK or error from FTPRECV
		if (!MDM_cmd_in_progress())
		{
			// wait for OK
			if (MDM_cmd_status == MDM_CMD_SUCCESS)
			{
				// parse number of bytes sent into i
				cptr = strstr(MDM_rx_buffer, "#FTPRECV: ");
				if (cptr != NULL)
				{
					cptr+= 10;
					if (sscanf(cptr, "%d", &i) == 1)
					{
						// find start of data, next line after #FTPRECV:=<number>
						cptr = strstr(cptr, "\r\n") + 2;
						// append it to file
						CFS_write_file(FTP_path_str, FTP_filename_str, "a", cptr, i);
					}
					// poll for file download completion
					sprintf(MDM_tx_buffer, "at#ftpgetpkt?\r");
					MDM_send_cmd(MDM_tx_buffer);
					MDM_retry_ptr = NULL;							// only 1 attempt
					MDM_cmd_timer_x20ms = 5 * 50;					// 5s to wait for reply
					com_state = COM_TRANSFER_FTP_3;
				}
				else	// got incorrect reply
				{
					USB_monitor_string("Wrong reply ftpt2");
					com_log_error((uint16)__LINE__, COM_STATUS_NO_UPDATE);	// Wrong reply ftpt2
					// if here have to look for sms rx and try again later
					com_gprs_fail_action();
				}
			}
			else		// got server error
			{
				USB_monitor_string("Server error ftpt2");
				com_log_error((uint16)__LINE__, COM_STATUS_SERVER_ERROR);	// Server error ftpt2
				// if here have to look for sms rx and try again later
				com_gprs_fail_action();
			}
		}
		break;

	case COM_TRANSFER_FTP_3:
		// wait for OK or error from FTPRECV
		if (!MDM_cmd_in_progress())
		{
			// wait for OK
			if (MDM_cmd_status == MDM_CMD_SUCCESS)
			{
				// look for "#FTPGETPKT:" and the following '\r'
				cptr = strstr(MDM_rx_buffer, "#FTPGETPKT: ");
				if (cptr != NULL)
				{
					// copy string from rxbuffer until get CRLF
					strcpy(STR_buffer, cptr);
					if (strstr(STR_buffer, "\r\n") != NULL)
					{
						// get eof value from char before '\r'
						cptr = strstr(STR_buffer, "\r") - 1;
						// if '0'
						if (*cptr == '0')
						{
							// request next block of file
							sprintf(MDM_tx_buffer, "at#ftprecv=480\r");
							MDM_send_cmd(MDM_tx_buffer);
							MDM_retry_ptr = NULL;								// only 1 attempt
							MDM_cmd_timer_x20ms = 10 * 50;						// 10s to wait for reply
							com_state = COM_TRANSFER_FTP_2;
							break;
						}
						else if (*cptr == '1')
						{
							// download complete
							// ask for directory listing
							sprintf(MDM_tx_buffer, "at#ftplist\r");
							MDM_tx_delay_timer_x20ms = 2 * 50;					// 2s delay
							MDM_send_cmd(MDM_tx_buffer);
							MDM_retry_ptr = NULL;								// only 1 attempt
							MDM_cmd_timer_x20ms = 40 * 50;						// 40s to wait for directory listing
							com_state = COM_POLL_FTP_6;
							break;
						}
					}
				}
				// anything else - got incorrect reply
				USB_monitor_string("Wrong reply ftpt3");
				com_log_error((uint16)__LINE__, COM_STATUS_NO_UPDATE);	// Wrong reply ftpt3
				// if here have to look for sms rx and try again later
				com_gprs_fail_action();
			}
			else		// got server error
			{
				USB_monitor_string("Server error ftpt3");
				com_log_error((uint16)__LINE__, COM_STATUS_SERVER_ERROR);	// Server error ftpt3
				// if here have to look for sms rx and try again later
				com_gprs_fail_action();
			}
		}
		break;

	case COM_GPRS_TIME_1:
		// wait for "CONNECT" and following cr
		if (strstr(MDM_rx_buffer, "CONNECT\r\n") != NULL)
		{
			// send 2nd line of gprs time logon file (access time function)
			i = CFS_read_line((char *)CFS_config_path, (char *)CFS_nitz_logon_name, 2, STR_buffer, 128);
			if (i < 1)
			{
				USB_monitor_string("Gprs time logon file: no line 2");
				com_log_error((uint16)__LINE__, COM_STATUS_NO_UPDATE);	// Faulty gprs time logon file 3
				com_socket_disconnect();								// already connected, so ensure disconnect
				com_tsync_attempt_count = 0;							// cancel this attempted time sync
				com_state = COM_GPRS_TIME_5;
				break;
			}
			// else got second line in STR_buffer (GET .... HTTP/1.2)
			sprintf(MDM_tx_buffer, "%s\r\n", STR_buffer);
			MDM_send_cmd(MDM_tx_buffer);
			MDM_tx_delay_timer_x20ms = 50;			// tx delay of 1s
			MDM_retry_ptr = NULL;	 				// only 1 attempt
			//MDM_cmd_timer_x20ms = 50;				// 1s to wait before next line
			com_state = COM_GPRS_TIME_2;
		}
		else if (!MDM_cmd_in_progress())
		{
			USB_monitor_string("No CONNECT gtime1");
			com_log_error((uint16)__LINE__, COM_STATUS_NO_UPDATE);	//No CONNECT gtime3
			// if here have to look for sms rx and try again later
			if (com_tsync_attempt_count != 0)
				com_tsync_attempt_count--;	// decrement retry count
			com_gprs_fail_action();
		}
		break;

	case COM_GPRS_TIME_2:
	case COM_GPRS_TIME_3:
		if (strstr(MDM_rx_buffer, "#NITZ") != NULL)
		{
			MDM_cmd_timer_x20ms = 5 * 50;		// 5s to get rest of the line
			com_state = COM_GPRS_TIME_4;
			break;
		}
		// else:
		if (!MDM_cmd_in_progress())
		{
			// stream lines 3 and maybe 4 of gprs time logon file to host
			i = CFS_read_line((char *)CFS_config_path, (char *)CFS_nitz_logon_name,
							  3 + com_state - COM_GPRS_TIME_2, STR_buffer, 128);

			if (i < 0)										// past end of file
				com_state = COM_GPRS_TIME_4;
			else											// got line in STR_buffer (NB may be blank)
			{
				sprintf(MDM_tx_buffer, "%s\r\n", STR_buffer);
				MDM_send_cmd(MDM_tx_buffer);
				//MDM_tx_delay_timer_x20ms = 50;			// tx delay of 1s
				MDM_retry_ptr = NULL;	 					// only 1 attempt
				//MDM_cmd_timer_x20ms = 50;					// 1s before next line
				com_state++;								// -> COM_GPRS_TIME_3 or COM_GPRS_TIME_4
			}
		}
		if (com_state == COM_GPRS_TIME_4)
			MDM_cmd_timer_x20ms = 40 * 50;					// 5s to get #NITZ
		break;

	case COM_GPRS_TIME_4:
		cptr = strstr(MDM_rx_buffer, "#NITZ");
		if (cptr != NULL)
		{
			// Format: #NITZ: 14/09/25,14:25:55
			// Sometimes terminated by \r, but not always, so check all required chars received
			for (dptr = cptr + 5; dptr < cptr + 24; dptr++)
			{
				if (*dptr == '\0')													// #NITZ string incomplete
				{
					cptr = NULL;
					break;
				}
			}
		}

		if (cptr != NULL)														// got full string
		{
			USB_monitor_string(cptr);
			LOG_entry(cptr);
			TSYNC_parse_nitz_reply(cptr + 7);									// parse #NITZ: date and time content
			com_tsync_attempt_count = 0;										// Success.
			com_pending_tsync = false;
			com_socket_disconnect();
			com_state = COM_GPRS_TIME_5;
		}
		else if (strstr(MDM_rx_buffer, "NO CARRIER\r") != NULL)		// unexpected disconnect
		{
			USB_monitor_string("Unexpected disconnect");
			com_log_error((uint16)__LINE__, COM_STATUS_NO_UPDATE);
			if (com_tsync_attempt_count != 0)
				com_tsync_attempt_count--;
			com_pending_tsync = false;
			com_pending_sms_tx = true;								// further tx or rx as required
			com_pending_sms_rx = true;
			com_state = COM_IDLE_WAIT;			// go to idle but stay awake
		}
		else if (!MDM_cmd_in_progress())
		{
			USB_monitor_string("No NITZ reply (timeout)");
			com_log_error((uint16)__LINE__, COM_STATUS_NO_UPDATE);	// No NITZ reply
			if (com_tsync_attempt_count != 0)
				com_tsync_attempt_count--;
			com_pending_tsync = false;
			com_socket_disconnect();
			com_state = COM_GPRS_TIME_5;
		}
		break;

	case COM_GPRS_TIME_5:
		if ((strstr(MDM_rx_buffer, "NO CARRIER\r") != NULL) || !MDM_cmd_in_progress())
		{
			com_pending_sms_tx = true;								// further tx or rx as required
			com_pending_sms_rx = true;
			com_state = COM_IDLE_WAIT;			// go to idle but stay awake
		}
		break;

	case COM_SIGNAL_TEST_0:
		com_time.sigtst_delay = RTC_time_sec + COM_sigtst_delay;	// set sigtst wait delay for sleep
		com_update_wakeup_time_sec(com_time.sigtst_delay);			// set wake up time
		com_state = COM_SIGNAL_TEST_1;
		break;

	case COM_SIGNAL_TEST_1:
		if (com_time.sigtst_delay <= RTC_time_sec)					// check delay time has expired
		{
			if (!CFS_open())										// stay here until file system is open
				break;

			if (MDM_ready(true))									// now powered-up & signed-on OR after failure
			{
				if (MDM_state == MDM_FAIL_TO_REGISTER)				// failed to power up
				{
					COM_sigtst_count = 0;
					com_state = COM_IDLE_WAIT;			// go to idle but stay awake
					break;
				}
				else
				{
					sprintf(MDM_tx_buffer, "ATE0;+COPS=3,0\r");		// no echo, ask for current network by name
					MDM_send_cmd(MDM_tx_buffer);
					MDM_retry_ptr = NULL;							// only 1 attempt
					com_state = COM_SIGNAL_TEST_2;					// waiting for reply
					break;
				}
			}
		}
		if (com_pending_abort)										// stop test
		{
			com_pending_abort = false;
			com_time.sigtst_delay = SLP_NO_WAKEUP;
			COM_sigtst_count = 0;
			com_state = COM_IDLE_WAIT;			// go to idle but stay awake
		}
		break;

	case COM_SIGNAL_TEST_2:
		if (!MDM_cmd_in_progress())								// wait for OK or timeout after message sent
		{
			if (MDM_cmd_status == MDM_CMD_SUCCESS)				// if OK
			{
				sprintf(MDM_tx_buffer, "AT+COPS?\r");			// ask for name of current network if any
				MDM_send_cmd(MDM_tx_buffer);
				MDM_retry_ptr = NULL;							// only 1 attempt
				com_state = COM_SIGNAL_TEST_3;					// waiting for reply
			}
			else
			{													// error or timeout
				com_time.sigtst_delay = SLP_NO_WAKEUP;
				COM_sigtst_count = 0;
				com_state = COM_IDLE_WAIT;			// go to idle but stay awake
			}
		}
		else if (com_pending_abort)								// stop test
		{
			com_pending_abort = false;
			com_time.sigtst_delay = SLP_NO_WAKEUP;
			COM_sigtst_count = 0;
			com_state = COM_IDLE_WAIT;			// go to idle but stay awake
		}
		break;

	case COM_SIGNAL_TEST_3:
		if (!MDM_cmd_in_progress())								// wait for OK or timeout after message sent
		{
			if (MDM_cmd_status == MDM_CMD_SUCCESS)				// if OK
			{
				// parse network name and write to file NETWORK.TXT (empty file written if no name present i.e. not registered)
				STR_parse_delimited_string(MDM_rx_buffer, STR_buffer, 30, '\"', '\"');
				CFS_write_file((char *)CFS_activity_path, (char *)CFS_network_name, "w", STR_buffer, strlen(STR_buffer));
				com_state = COM_SIGNAL_TEST_4;
			}
			else
			{													// error or timeout
				com_time.sigtst_delay = SLP_NO_WAKEUP;
				COM_sigtst_count = 0;
				com_state = COM_IDLE_WAIT;			// go to idle but stay awake
			}
		}
		else if (com_pending_abort)								// stop test
		{
			com_pending_abort = false;
			com_time.sigtst_delay = SLP_NO_WAKEUP;
			COM_sigtst_count = 0;
			com_state = COM_IDLE_WAIT;			// go to idle but stay awake
		}
		break;

	case COM_SIGNAL_TEST_4:
		if (com_time.sigtst_delay <= RTC_time_sec)						// check delay time has expired
		{
			sprintf(MDM_tx_buffer, "ATE0;+CSQ;+CREG?\r");				// get signal strength and registered state
			MDM_send_cmd(MDM_tx_buffer);
			MDM_retry_ptr = NULL;										// only 1 attempt
			com_state = COM_SIGNAL_TEST_5;								// wait for reply
		}
		else if (com_pending_abort)										// stop test
		{
			com_pending_abort = false;
			com_time.sigtst_delay = SLP_NO_WAKEUP;
			COM_sigtst_count = 0;
			com_state = COM_IDLE_WAIT;			// go to idle but stay awake
		}
		break;

	case COM_SIGNAL_TEST_5:
		if (!MDM_cmd_in_progress())		// wait for OK or timeout after message sent
		{
			strcpy(STR_buffer,",N??");				// set default in case of timeout
			COM_csq = 0;
			// if not timed out
			if (MDM_cmd_timer_x20ms != 0)			// parse signal strength & registration
			{
				strcpy(STR_buffer,",N??");			// set default
				cptr = NULL;
				cptr = strstr(MDM_rx_buffer, "CSQ: ");
				if (cptr != NULL)						// got CSQ
				{
					cptr += 4;
					if (sscanf(cptr, " %d", &COM_csq) != 1)
					{
						cptr = NULL;
						COM_csq = 99;
					}
				}
				// create string for appending to file
				if (COM_csq == 99)
				{
					COM_csq = 0;
				}
				else
				{
					// calculate percentage
					i = (uint8)(((int)COM_csq * 100) / 31);
					if (i > 99) i = 99;
					sprintf(STR_buffer, ",N%02d", i);
				}
				// parse registration
				if ((strstr(MDM_rx_buffer, "+CREG: 0,1") != NULL) || (strstr(MDM_rx_buffer, "+CREG: 0,5") != NULL))
					STR_buffer[1] = 'R';
			}

			if (--COM_sigtst_count == 0)	// end of test
			{
				// append string to file and terminate it
				CFS_write_file((char *)CFS_activity_path, (char *)CFS_sigres_name, "a", STR_buffer, strlen(STR_buffer) + 1);
				com_state = COM_IDLE_WAIT;			// go to idle but stay awake
			}
			else
			{
				// append string to file
				CFS_write_file((char *)CFS_activity_path, (char *)CFS_sigres_name, "a", STR_buffer, strlen(STR_buffer));
				com_time.sigtst_delay = RTC_time_sec + COM_sigtst_interval;			// set sigtst interval
				com_update_wakeup_time_sec(com_time.sigtst_delay);					// set wake up time
				com_state = COM_SIGNAL_TEST_4;
			}
		}
		else if (com_pending_abort)					// stop test
		{
			com_pending_abort = false;
			com_time.sigtst_delay = SLP_NO_WAKEUP;
			COM_sigtst_count = 0;
			com_state = COM_IDLE_WAIT;			// go to idle but stay awake
		}
		break;

	case COM_NW_TEST_0:
		com_pending_nw_test = false;
		com_time.nwtst_delay = RTC_time_sec + COM_nwtst_delay;	// set gsmtst wait delay for sleep
		com_update_wakeup_time_sec(com_time.nwtst_delay);			// set wake up time
		com_state = COM_NW_TEST_1;
		break;

	case COM_NW_TEST_1:
		if (com_time.nwtst_delay <= RTC_time_sec)	// check delay time has expired
		{
			if (!CFS_open())										// stay here until file system is open
				break;

			if (MDM_ready(true))												// now powered-up & signed-on OR after failure
			{
				if (MDM_state == MDM_FAIL_TO_REGISTER)							// failed to power up
				{
					COM_nwtest_progress = 'M';
					com_state = COM_NW_TEST_EXIT;								// exit
					break;
				}
				else
				{
					// set up index for source of command - read line 1 of networks.txt with source info
					if (CFS_read_line((char *)CFS_activity_path, (char *)CFS_networks_name, 1, STR_buffer, 10) != 0)
					{
						if (strstr(STR_buffer, "USB") != NULL)
							com_source_index = CMD_SOURCE_USB;
						else if (strstr(STR_buffer, "SMS") != NULL)
							com_source_index = CMD_SOURCE_SMS;
						else if (strstr(STR_buffer, "FTP") != NULL)
							com_source_index = CMD_SOURCE_FTP;
						else
							com_source_index = CMD_SOURCE_SCRIPT;
					}
					// report running
					COM_nwtest_progress = 'R';
					// set up check reg retry counter
					com_tx_attempts = 0;
					// look at reg status now
					MDM_send_cmd("AT+CREG?\r");
					com_state = COM_NW_CHECK_REG;				// waiting for reply
					break;
				}
			}
		}
		if (com_pending_abort)								// stop test before we have brought modem up
		{
			com_pending_abort = false;
			com_time.nwtst_delay = SLP_NO_WAKEUP;
			// terminate result file
			CFS_write_file((char *)CFS_activity_path, (char *)CFS_nwres_name, "a", "", 1);
			COM_nwtest_progress = 'A';
			com_state = COM_IDLE_WAIT;			// go to idle but stay awake
		}
		break;

	case COM_NW_CHECK_REG:
		if (!MDM_cmd_in_progress())
		{
			if (strstr(MDM_rx_buffer, "0,1") || strstr(MDM_rx_buffer, "0,5"))	// registered
			{
				com_tx_attempts = 0;
				sprintf(MDM_tx_buffer, "AT+COPS=?\r");	// get list of networks
				MDM_send_cmd(MDM_tx_buffer);
				MDM_cmd_timer_x20ms = 30 * 50;			// 30s to wait for "OK" reply
				com_state = COM_NW_TEST_2;				// waiting for reply
				break;
			}
			else if (strstr(MDM_rx_buffer, "0,2") && (com_tx_attempts++ < 10))	// unregistered and searching
			{
				MDM_send_cmd("AT+CREG?\r");				// try again with delay
				MDM_tx_delay_timer_x20ms = 5 * 50;		// wait 5s before retry
				MDM_retry_ptr = NULL;					// only 1 attempt
				MDM_cmd_timer_x20ms = 10 * 50;			// wait up to 10s
				break;
			}
			// else																// cannot register, denied or unknown or counted out searching
			{
				// terminate result file
				com_tx_attempts = 0;
				CFS_write_file((char *)CFS_activity_path, (char *)CFS_nwres_name, "a", "", 1);
				COM_nwtest_progress = 'E';				// set error result
				com_state = COM_IDLE_WAIT;			// go to idle but stay awake
			}
		}
		break;

	case COM_NW_TEST_2:
		if (!MDM_cmd_in_progress())				// wait for OK or timeout after message sent
		{
			// wait for OK
			if (MDM_cmd_status == MDM_CMD_SUCCESS)
			{
				// parse list of networks into a file
				com_network_count = 0;
				cptr = MDM_rx_buffer;
				do
				{
					// get next string enclosed in brackets
					cptr = STR_parse_delimited_string(cptr, STR_buffer, 30, '(', ')');
					i = strlen(STR_buffer);
					// check string contains quotes
					if (strstr(STR_buffer, "\"") == NULL) i = 0;
					if ((i != 0) && (com_network_count < 12))
					{
						// write string as a newline in file
						strcat(STR_buffer, "\r\n");
						CFS_write_file((char *)CFS_activity_path, (char *)CFS_networks_name, "a", STR_buffer, strlen(STR_buffer));
						// trap home network string - begins with '2'
						if (STR_buffer[0] == '2')
						{
							// extract home network number		CAN THIS BE DELETED??
							dptr = strstr(STR_buffer, ",,\"");
							dptr = STR_parse_delimited_string(dptr, com_home_network_number, 6, '\"', '\"');
						}
						com_network_count++;
					}
					cptr++;
				}
				while (strlen(STR_buffer) > 0);
				com_network_total = com_network_count;
				com_network_count = 1;
				com_state = COM_NW_TEST_3;
			}
			else									// error or timeout
			{
				// look for known error
				COM_nwtest_progress = 'E';
				if (strstr(MDM_rx_buffer, "CME ERROR:") != NULL)
				{
					if ((strstr(MDM_rx_buffer, "30") != NULL) || (strstr(MDM_rx_buffer, "no network service") != NULL))
						COM_nwtest_progress = 'N';
				}
				com_state = COM_NW_TEST_EXIT;
			}
		}
		else if (com_pending_abort)					// stop test
		{
			com_pending_abort = false;
			COM_nwtest_progress = 'A';
			com_state = COM_NW_TEST_EXIT;
		}
		break;

	case COM_NW_TEST_3:
		// get network number for com_network_count into STR_buffer
		if (CFS_read_line((char *)CFS_activity_path, (char *)CFS_networks_name, com_network_count + 1, STR_buffer, 30) != 0)
		{
			// extract and save network name
			dptr = STR_buffer;
			dptr = STR_parse_delimited_string(dptr, com_network_name, 17, '\"', '\"');
			// extract network number
			dptr++;
			dptr = STR_parse_delimited_string(dptr, com_network_number, 6, '\"', '\"');
			com_network_cell = 0;
			com_neighbour_cells = 0;
			com_state = COM_NW_TEST_4;
		}
		else
		{
			COM_nwtest_progress = 'E';
			com_state = COM_NW_TEST_EXIT;
		}
		break;

	case COM_NW_TEST_4:
		// change monitor data to current cell
		sprintf(MDM_tx_buffer, "AT#MONI=%u\r", com_network_cell);
		MDM_send_cmd(MDM_tx_buffer);
		com_state = COM_NW_TEST_5;
		break;

	case COM_NW_TEST_5:
		if (!MDM_cmd_in_progress())				// wait for OK or timeout after message sent
		{
			// wait for OK
			if (MDM_cmd_status == MDM_CMD_SUCCESS)
			{
				// get monitor data
				sprintf(MDM_tx_buffer, "AT#MONI\r");
				MDM_send_cmd(MDM_tx_buffer);
				MDM_cmd_timer_x20ms = 10 * 50;			// 10s to wait for "OK" reply
				com_state = COM_NW_TEST_6;
			}
			else									// error or timeout
			{
				COM_nwtest_progress = 'E';
				com_state = COM_NW_TEST_EXIT;
			}
		}
		else if (com_pending_abort)					// stop test
		{
			com_pending_abort = false;
			COM_nwtest_progress = 'A';
			com_state = COM_NW_TEST_EXIT;
		}
		break;

	case COM_NW_TEST_6:
		if (!MDM_cmd_in_progress())				// wait for OK or timeout after message sent
		{
			// wait for OK
			if (MDM_cmd_status == MDM_CMD_SUCCESS)
			{
				// parse AT#MONI reply and save in results file
				// if current cell
				if (com_network_cell == 0)
				{
					// get and save power
					cptr = strstr(MDM_rx_buffer, "PWR:");
					if (cptr != NULL)
					{
						com_home_cell_strength = 0;
						cptr+= 4;
						if (sscanf(cptr, " %d", &i) == 1)
						{
							// convert to percentage
							if (i >= -51)
							{
								com_home_cell_strength = 31;
							}
							else if (i > -113)
							{
								com_home_cell_strength = (uint8)((113 + i)/2);
							}
						}
						com_home_cell_strength = ((uint16)com_home_cell_strength * 100)/31;
						com_network_cell++;
						com_state = COM_NW_TEST_4;
					}
					else	// if fail just exit safely
					{
						COM_nwtest_progress = 'E';
						com_state = COM_NW_TEST_EXIT;
					}
				}
				else
				{
					// count presence of neighbour cell - look for presence of "LAC:" in #MONI reply
					cptr = strstr(MDM_rx_buffer, "LAC:");
					if (cptr != NULL)
					{
						com_neighbour_cells++;
					}
					// if last neighbour cell
					if (com_network_cell == 6)
					{
						// save name, cell strength, and neigbour cell count
						sprintf(STR_buffer, ",%s,%u%%,%u", com_network_name, com_home_cell_strength, com_neighbour_cells);
						CFS_write_file((char *)CFS_activity_path, (char *)CFS_nwres_name, "a", STR_buffer, strlen(STR_buffer));
						// reset #MONI to home cell
						sprintf(MDM_tx_buffer, "AT#MONI=0\r");
						MDM_send_cmd(MDM_tx_buffer);
						com_state = COM_NW_TEST_7;
					}
					else
					{
						com_network_cell++;
						com_state = COM_NW_TEST_4;
					}
				}
			}
			else									// error or timeout
			{
				COM_nwtest_progress = 'E';
				com_state = COM_NW_TEST_EXIT;
			}
		}
		else if (com_pending_abort)					// stop test
		{
			com_pending_abort = false;
			COM_nwtest_progress = 'A';
			com_state = COM_NW_TEST_EXIT;
		}
		break;

	case COM_NW_TEST_7:
		if (!MDM_cmd_in_progress())				// wait for OK or timeout after message sent
		{
			// wait for OK
			if (MDM_cmd_status == MDM_CMD_SUCCESS)
			{
				// was this the last network?
				if (com_network_count == com_network_total)
				{
					// correct exit point
					COM_nwtest_progress = 'F';
					com_state = COM_NW_TEST_EXIT;
				}
				else
				{
					// get next network from network file
					com_network_count++;
					// get network number for com_network_count into STR_buffer
					if (CFS_read_line((char *)CFS_activity_path, (char *)CFS_networks_name, com_network_count + 1, STR_buffer, 30) != 0)
					{
						// extract and save network name
						dptr = STR_buffer;
						dptr = STR_parse_delimited_string(dptr, com_network_name, 17, '\"', '\"');
						// extract network number
						dptr++;
						dptr = STR_parse_delimited_string(dptr, com_network_number, 6, '\"', '\"');
						com_network_cell = 0;
						com_neighbour_cells = 0;
						// set new network
						com_network_check_count = 0;
						sprintf(MDM_tx_buffer, "AT+COPS=1,2,%s\r", com_network_number);
						MDM_send_cmd(MDM_tx_buffer);
						MDM_cmd_timer_x20ms = 30 * 50;			// 30s to wait for "OK" reply
						// go and check new network
						com_state = COM_NW_TEST_8;
					}
					else
					{
						COM_nwtest_progress = 'E';
						com_state = COM_NW_TEST_EXIT;
					}
				}
			}
			else									// error or timeout
			{
				COM_nwtest_progress = 'E';
				com_state = COM_NW_TEST_EXIT;
			}
		}
		else if (com_pending_abort)					// stop test
		{
			com_pending_abort = false;
			COM_nwtest_progress = 'A';
			com_state = COM_NW_TEST_EXIT;
		}
		break;

	case COM_NW_TEST_8:
		if (!MDM_cmd_in_progress())					// wait for OK or timeout after message sent
		{
			// wait for OK
			if (MDM_cmd_status == MDM_CMD_SUCCESS)
			{
				// check that we have changed network - ask for current network number
				sprintf(MDM_tx_buffer, "AT+COPS?\r");
				MDM_send_cmd(MDM_tx_buffer);
				MDM_tx_delay_timer_x20ms = 150;		// 3 second delay
				MDM_cmd_timer_x20ms = 30 * 50;		// 30s to wait for "OK" reply
				com_state = COM_NW_TEST_9;
			}
			else									// error or timeout
			{
				COM_nwtest_progress = 'E';
				com_state = COM_NW_TEST_EXIT;
			}
		}
		else if (com_pending_abort)					// stop test
		{
			com_pending_abort = false;
			COM_nwtest_progress = 'A';
			com_state = COM_NW_TEST_EXIT;
		}
		break;

	case COM_NW_TEST_9:
		if (!MDM_cmd_in_progress())					// wait for OK or timeout after message sent
		{
			// wait for OK
			if (MDM_cmd_status == MDM_CMD_SUCCESS)
			{
				// parse returned network number if a COPS reply
				cptr = strstr(MDM_rx_buffer, "+COPS:");
				if (cptr != NULL)
				{
					// network number is enclosed in quotes
					dptr = STR_parse_delimited_string(cptr, STR_buffer, 6, '\"', '\"');
					// is it new number?
					if (strcmp(STR_buffer, com_network_number) == 0)
					{
						// go and check its cells
						MDM_tx_delay_timer_x20ms = 150;		// 3 second delay
						com_state = COM_NW_TEST_4;
					}
					else
					{
						// if not reached retry limit (10 times at 3 second intervals)
						if (++com_network_check_count < 11)
						{
							// try again
							com_state = COM_NW_TEST_8;
						}
						else
						{
							// failed to set new network - go and try the next one
							com_state = COM_NW_TEST_7;
						}
					}
				}
				else									// not +COPS reply
				{
					COM_nwtest_progress = 'E';
					com_state = COM_NW_TEST_EXIT;
				}
			}
			else									// error or timeout
			{
				COM_nwtest_progress = 'E';
				com_state = COM_NW_TEST_EXIT;
			}
		}
		else if (com_pending_abort)					// stop test
		{
			com_pending_abort = false;
			COM_nwtest_progress = 'A';
			com_state = COM_NW_TEST_EXIT;
		}
		break;

	case COM_NW_TEST_EXIT:
		// set home network
		sprintf(MDM_tx_buffer, "AT+COPS=0\r");
		MDM_send_cmd(MDM_tx_buffer);
		MDM_cmd_timer_x20ms = 30 * 50;			// 30s to wait for "OK" reply
		MDM_retry_ptr = NULL;					// only 1 attempt
		com_state = COM_NW_TEST_EXIT_2;		// waiting for reply
		break;

	case COM_NW_TEST_EXIT_2:
		if (!MDM_cmd_in_progress())				// wait for any reply or timeout after message sent
		{
			// send result file if source is SMS or FTP
			// read line 1 of networks.txt with source info
			if (com_source_index == CMD_SOURCE_SMS)
			{
				sprintf(STR_buffer, CMD_CHARACTER_STRING "nwres\r");
				CMD_schedule_parse(CMD_SOURCE_SMS, STR_buffer, COM_output_buffer);
				com_state = COM_NW_TEST_REPLY;
				break;
			}
			else if (com_source_index == CMD_SOURCE_FTP)
			{
				sprintf(STR_buffer, CMD_CHARACTER_STRING "nwres\r");
				FTP_act_on_ftp_command();
			}
			com_time.nwtst_delay = SLP_NO_WAKEUP;
			// terminate result file
			CFS_write_file((char *)CFS_activity_path, (char *)CFS_nwres_name, "a", "", 1);
			com_state = COM_IDLE_WAIT;			// go to idle but stay awake
		}
		break;

	case COM_NW_TEST_REPLY:
		if (!CMD_busy(CMD_SOURCE_SMS))
		{
			MSG_send(MSG_TYPE_SMS_TEXT, COM_output_buffer, COM_sms_sender_number);
			MSG_flush_outbox_buffer(true);
			com_state = COM_IDLE_WAIT;			// go to idle but stay awake
		}
		break;

	default:	// confused state
		USB_monitor_string("Confused state");
		com_log_error((uint16)__LINE__, COM_STATUS_NO_UPDATE);	// Assert confused state
		MDM_power_off();
		com_state = COM_IDLE;
		break;
	}
#endif
}

/******************************************************************************
** Function:	Initialisation
**
** Notes:		set defaults before SMSC and MPS commands
**
*/
void COM_init(void)
{
#ifndef HDW_PRIMELOG_PLUS
	COM_reset_logger = 0;

	// telephone numbers - default to empty
	COM_host1[0] = '\0';
	COM_host2[0] = '\0';
	COM_alarm1[0] = '\0';
	COM_alarm2[0] = '\0';
	// modem retry strategy - set sensible defaults
	com_registration_retry_limit = 4;
 	com_registration_retry_interval = 14;		// index for 15 minutes
	com_transmission_retry_limit = 4;
	com_sms_rx_second_shot = false;
	com_tx_attempts = 0;
	com_ftp_sequence = 0;
	// com_time value defaults
	com_time.window_tx = SLP_NO_WAKEUP;
	com_time.window_rx = SLP_NO_WAKEUP;
	com_time.ftp_poll = SLP_NO_WAKEUP;
	com_time.re_register = SLP_NO_WAKEUP;
	com_time.rx_delay = SLP_NO_WAKEUP;
	com_time.sigtst_delay = SLP_NO_WAKEUP;
	com_time.nwtst_delay = SLP_NO_WAKEUP;
	COM_nwtest_progress = 'F';
	com_day_bcd = RTC_now.day_bcd;
	COM_schedule._1fm_standby_mins = 15;		// default value
#ifdef HDW_1FM
	COM_schedule.ftp_enable = 1;
#endif

#endif
}


