/******************************************************************************
** File:	Msg.h
**
** Notes:
*/

/* CHANGES
** v2.36 271010 PB  Remove prototype of MSG_find_oldest()
**
** v2.37 281010 PB 	New function MSG_remove() removes message named in msg_out_filename from outbox
**
** v2.74 250511 PB 	DEL143 - Remove MSG_TYPE_GPRS_TIME - to be done with a pending flag in com.c
**
** v2.76 260511 PB 	DEL142 - new outbox types define
**                           add bool parameter to MSG_remove() and MSG_get_msg_to_tx()
**                           replace MSG_files_in_outbox with MSG_files_in_sms_outbox and MSG_files_in_ftp_outbox
*/

#define MSG_PHONE_NUMBER_LENGTH		32

// Message types:
#define MSG_TYPE_SMS_TEXT			'T'
#define MSG_TYPE_SMS_PDU			'P'
#define MSG_TYPE_FTP_MSG			'M'
#define MSG_TYPE_FTP_FILE			'F'
#define MSG_TYPE_FTP_PART_DATA		'D'
#define MSG_TYPE_FTP_ALL_DATA		'A'

// outbox types
#define SMS_OUTBOX					(true)
#define FTP_OUTBOX					(false)

// Message stored in outbox: type\r\nDestination\r\nMessage\r\n\0
// Message length 280 (140 bytes as hexadecimal characters)
// Mail messages longer than this must be by file attachment
//#define MSG_BUFFER_LENGTH		(1 + 2 + MSG_PHONE_NUMBER_LENGTH + 2 + 280 + 2 + 1)	// PB does not take into account pdu header length
#define MSG_BUFFER_LENGTH		384													// PB 1.5 * 256	

extern int MSG_files_in_sms_outbox;						// -1 if indeterminate
extern int MSG_files_in_ftp_outbox;						// -1 if indeterminate
extern int MSG_body_index;								// points to body of message in MSG_tx_buffer

extern char MSG_tx_buffer[MSG_BUFFER_LENGTH];			// buffer from which we transmit

bool MSG_get_message_to_tx(bool outbox);
bool MSG_remove(bool outbox);
void MSG_send(char type, char *msg, char *destination);
void MSG_flush_outbox_buffer(bool initiate_tx);
void MSG_task(void);
void MSG_init(void);


