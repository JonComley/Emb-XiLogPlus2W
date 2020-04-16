/******************************************************************************
** File:	Msg.c	
**
** Notes:	GSM SMS and GPRS messaging functions
*/

/* CHANGES
** v2.36 271010 PB  Replace MSG_find_oldest() with private fn msg_find_youngest
**					This calls new CFS_find_youngest_file() fn for extraction of next message from message outbox folder
**
** v2.37 281010 PB 	New function MSG_remove() removes message named in msg_out_filename from outbox
**                  MSG_get_message_to_tx now gets the youngest message in outbox
**					Incoming message name now resides in msg_in_filename so it does not corrupt outgoing message name while it is being txed
**
** v2.48 190111 PB 	DEL100 - In MSG_flush_outbox, initiate a tx_rx if modem is on, or if not in batch mode
**
** V2.65 070411 PB  added conditional compilation ifndef HDW_PRIMELOG_PLUS in MSG_flush_outbox_buffer
**
** V2.76 260511 PB  DEL142 - changes to use two message outboxes, one for SMS and one for FTP messages - 
**                           changes to MS_init(), MSG_task(), MSG_flush_outbox_buffer(), MSG_remove(), MSG_get_message_to_tx(), msg_find_next()
**                           replace msg_outbox_path with two paths
**
** V2.90 010811 PB  DEL142 - COM_initiate_tx uses bool parameter to select sms or ftp
**
** V3.17 091012 PB  Waste Water - bring up to date with Xilog+ V3.06 - use return value of CFS_open() in MSG_remove()
**
** V3.26 180613 PB  Remove CFS_FAILED_TO_OPEN state 
*/

#include <string.h>
#include <stdio.h>

#include "custom.h"
#include "compiler.h"
#include "MDD File System/FSIO.h"

#define extern
#include "Msg.h"
#undef extern

#include "mdm.h"
#include "cfs.h"
#include "Rtc.h"
#include "Com.h"
#include "Str.h"
#include "usb.h"
#include "log.h"

#define MSG_MAX_OUTBOX_SIZE		20		// files

uint16 msg_new_msg_number;

char msg_in_filename[CFS_FILE_NAME_SIZE];
char msg_out_filename[CFS_FILE_NAME_SIZE];
char msg_outbox_buffer[MSG_BUFFER_LENGTH];		// buffer for message going into outbox

const char msg_sms_outbox_path[] = "\\Outbox\\sms";
const char msg_ftp_outbox_path[] = "\\Outbox\\ftp";

/******************************************************************************
** Function:	Find youngest or oldest message in given outbox, according to config
**
** Notes:		Returns true for success, false if file system unavailable.
**				Result in msg_out_filename.
*/
bool msg_find_next(bool outbox)
{
	char * outbox_path;
	int  * files_in_outbox_p;

	if (outbox == SMS_OUTBOX)
	{
		outbox_path = (char *)msg_sms_outbox_path;
		files_in_outbox_p = &MSG_files_in_sms_outbox;
	}
	else
	{
		outbox_path = (char *)msg_ftp_outbox_path;
		files_in_outbox_p = &MSG_files_in_ftp_outbox;
	}

	*files_in_outbox_p = COM_schedule.tx_oldest_first ?
						 CFS_find_oldest_file(outbox_path, msg_out_filename) :
						 CFS_find_youngest_file(outbox_path, msg_out_filename);

	return (*files_in_outbox_p >= 0);
}

/******************************************************************************
** Function:	Get message to be transmitted into RAM from given outbox
**
** Notes:		If file system not available returns false.
**				If no message to send, MSG_tx_buffer becomes empty string.
*/
bool MSG_get_message_to_tx(bool outbox)
{
	int i;
	char * outbox_path;

	if (!msg_find_next(outbox))
		return false;

	MSG_tx_buffer[0] = '\0';					// default: nothing to Tx.

	if (outbox == SMS_OUTBOX)
	{
		outbox_path = (char *)msg_sms_outbox_path;
	}
	else
	{
		outbox_path = (char *)msg_ftp_outbox_path;
	}

	if (!CFS_read_file((char *)outbox_path, msg_out_filename, MSG_tx_buffer, sizeof(MSG_tx_buffer)))
		return false;							// leave tx buffer empty
	// else:

	// string-terminate destination field
	for (MSG_body_index = 3; MSG_body_index < sizeof(MSG_tx_buffer); MSG_body_index++)
	{
		if (MSG_tx_buffer[MSG_body_index] == '\r')
		{
			MSG_tx_buffer[MSG_body_index++] = '\0';
			if (MSG_tx_buffer[MSG_body_index] == '\n')	// \r\n sequence
				MSG_body_index++;

			break;
		}
	}

	// string-terminate message body or filename
	for (i = MSG_body_index; i < sizeof(MSG_tx_buffer); i++)
	{
		if (MSG_tx_buffer[i] == '\r')
		{
			MSG_tx_buffer[i] = '\0';
			break;
		}
	}

	if (outbox == SMS_OUTBOX)
	{
		sprintf(STR_buffer, "File got from sms outbox: %s", msg_out_filename);
	}
	else
	{
		sprintf(STR_buffer, "File got from ftp outbox: %s", msg_out_filename);
	}
	USB_monitor_string(STR_buffer);

	if (MSG_tx_buffer[0] == '\0')
	{
		USB_monitor_prompt("Invalid message type or destination");
		LOG_enqueue_value(LOG_ACTIVITY_INDEX, LOG_MSG_FILE, __LINE__);	// Invalid message type or destination
	}

	return true;
}

/******************************************************************************
** Function:	Remove the current outgoing message from the given outbox
**
** Notes:		Also clears MSG_tx_buffer
**				Returns false if file system is not open
**              If cannot find outbox directory then major failure - remakes outboxes and returns true so COM_task does not hang
*/
bool MSG_remove(bool outbox)
{
	if (!CFS_open())															// wait for file system
		return false;

	if (CFS_state == CFS_OPEN)
	{
		if (outbox == SMS_OUTBOX)
		{
			if (FSchdir((char *)msg_sms_outbox_path) != 0)							// select sms outbox directory
			{
				MSG_init();															// cannot find outbox - major fault - remake outboxes
				return true;
			}
			if (FSremove(msg_out_filename) == 0)									// Remove file
			{
				sprintf(STR_buffer, "File removed from sms outbox: %s", msg_out_filename);
				USB_monitor_string(STR_buffer);
				MSG_files_in_sms_outbox--;
			}
			else
				MSG_files_in_sms_outbox = -1;
		}
		else
		{
			if (FSchdir((char *)msg_ftp_outbox_path) != 0)							// select ftp outbox directory
			{
				MSG_init();															// cannot find outbox - major fault - remake outboxes
				return true;
			}
			if (FSremove(msg_out_filename) == 0)									// Remove file
			{
				sprintf(STR_buffer, "File removed from ftp outbox: %s", msg_out_filename);
				USB_monitor_string(STR_buffer);
				MSG_files_in_ftp_outbox--;
			}
			else
				MSG_files_in_ftp_outbox = -1;
		}
	}

	MSG_tx_buffer[0] = '\0';													// clear tx buffer
	msg_out_filename[0] = '\0';													// clear filename
	return true;
}

/******************************************************************************
** Function:	Add an SMS or FTP message to the outbox in buffer
**
** Notes:		If destination is empty string, do nothing
*/
void MSG_send(char type, char *msg, char *destination)
{
	if (*destination == '\0')
	{
		return;
	}

	msg_outbox_buffer[0] = type;
	sprintf(&msg_outbox_buffer[1], "\r\n%s\r\n%s\r\n", destination, msg);
}

/******************************************************************************
** Function:	Flush outbox in buffer to file in outbox directory
**
** Notes:		Assumes file system available
**				immediate transmission is initiated if flag is true OR modem is already on
*/
void MSG_flush_outbox_buffer(bool initiate_tx)
{
	bool is_sms;

	// only flush if something there
	if (msg_outbox_buffer[0] == '\0')
	{
		return;
	}
	// select outbox depending on message type
	if ((msg_outbox_buffer[0] == MSG_TYPE_SMS_TEXT) || (msg_outbox_buffer[0] == MSG_TYPE_SMS_PDU))
	{
		is_sms = true;
	}
	else if ((msg_outbox_buffer[0] == MSG_TYPE_FTP_MSG) ||
			 (msg_outbox_buffer[0] == MSG_TYPE_FTP_FILE) || 
			 (msg_outbox_buffer[0] == MSG_TYPE_FTP_PART_DATA) || 
			 (msg_outbox_buffer[0] == MSG_TYPE_FTP_ALL_DATA))
	{
		is_sms = false;
	}
	else
	{
		// trap unknown message types
		return;
	}

	sprintf(msg_in_filename, "MSG%05u.msg", msg_new_msg_number++);
	if (is_sms)
	{
		if (CFS_write_file((char *)msg_sms_outbox_path, msg_in_filename, "w", msg_outbox_buffer, strlen(msg_outbox_buffer)))
		{
			msg_outbox_buffer[0] = '\0';				// buffer now empty
			MSG_files_in_sms_outbox++;
		}
	}
	else
	{
		if (CFS_write_file((char *)msg_ftp_outbox_path, msg_in_filename, "w", msg_outbox_buffer, strlen(msg_outbox_buffer)))
		{
			msg_outbox_buffer[0] = '\0';				// buffer now empty
			MSG_files_in_ftp_outbox++;
		}
	}

#ifndef HDW_PRIMELOG_PLUS
	if (initiate_tx || (MDM_state == MDM_ON))
		COM_initiate_tx(is_sms);
#endif
}

/******************************************************************************
** Function:	Manage size of outbox buffers, and update number of messages
**
** Notes:
*/
void MSG_task(void)
{
	// sms outbox
	if (MSG_files_in_sms_outbox > MSG_MAX_OUTBOX_SIZE)	// delete oldest
	{
		if (!CFS_purge_oldest_file((char *)msg_sms_outbox_path))
			return;

		MSG_files_in_sms_outbox--;
	}
	else if (MSG_files_in_sms_outbox < 0)
	{
		msg_find_next(SMS_OUTBOX);
	}

	// ftp outbox
	if (MSG_files_in_ftp_outbox > MSG_MAX_OUTBOX_SIZE)	// delete oldest
	{
		if (!CFS_purge_oldest_file((char *)msg_ftp_outbox_path))
			return;

		MSG_files_in_ftp_outbox--;
	}
	else if (MSG_files_in_ftp_outbox < 0)
	{
		msg_find_next(FTP_OUTBOX);
	}
}

/******************************************************************************
** Function:	Initialise messaging outboxes	
**
** Notes:	
*/
void MSG_init(void)
{
	MSG_files_in_sms_outbox = -1;
	MSG_files_in_ftp_outbox = -1;							// indeterminate for now...

	(void)CFS_open();										// keep file system awake
	if (CFS_state == CFS_OPEN)
	{
		if (FSchdir((char *)msg_sms_outbox_path) != 0)			// can't set sms working directory
		{
			FSmkdir((char *)msg_sms_outbox_path);				// so create it
			MSG_files_in_sms_outbox = 0;
		}
		if (FSchdir((char *)msg_ftp_outbox_path) != 0)			// can't set ftp working directory
		{
			FSmkdir((char *)msg_ftp_outbox_path);				// so create it
			MSG_files_in_ftp_outbox = 0;
		}
	}
}

