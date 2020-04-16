/******************************************************************************
** File:	Cmd.h
**
** Notes:	Execute commands from USB interface, SMS or GPRS
**
**
** v2.51 090211 PB new error code CMD_ERR_TEST_IN_PROGRESS 24
**
** V3.04 171211 PB Waste Water - new error code CMD_ERR_TIMED_OUT 25
**
** V3.07 020212 PB Waste Water - new error code CMD_ERR_SERIAL_COMMS_FAIL 26
**
** V3.26 180613 PB Convert CMD_byte_mask[] to CMD_word_mask[] for general use for creating 8 or 16 bit mask from n
**		 250613    Rename CMD_ERR_TIMED_OUT to CMD_ERR_SERIAL_BUSY
**
** V4.00 220114 PB if HDW_GPS disable all analogue calls and functions
**
** V5.00 231014 PB new CMD_ERR_INCOMPATIBLE_HARDWARE
*/

#include "HardwareProfile.h"				// Needed to determine command character

#ifdef HDW_1FM
#define CMD_CHARACTER			'&'
#define CMD_CHARACTER_STRING	"&"
#else
#define CMD_CHARACTER			'#'
#define CMD_CHARACTER_STRING	"#"
#endif

// Command error codes:
#define CMD_ERR_NONE						0
#define CMD_ERR_NO_HASH						1
#define CMD_ERR_NO_PARAMETERS				2
#define CMD_ERR_INVALID_VALUE				3
#define CMD_ERR_VALUE_OUT_OF_RANGE			4
#define CMD_ERR_WRONG_SEPARATOR				5
#define CMD_ERR_INVALID_DATE				6
#define CMD_ERR_INVALID_DESTINATION			7
#define CMD_ERR_INVALID_CHANNEL_NUMBER		8
#define CMD_ERR_INVALID_WORKING_DIRECTORY	9
#define CMD_ERR_FILE_OR_DIRECTORY_NOT_FOUND	10
#define CMD_ERR_FILE_DELETE_FAILED			11
#define CMD_ERR_INVALID_FILENAME			12
#define CMD_ERR_FILE_READ_LINE_FAILED		13
#define CMD_ERR_FILE_WRITE_FAILED			14
#define CMD_ERR_FAILED_TO_CREATE_DIRECTORY	15
#define CMD_ERR_FAILED_TO_REMOVE_DIRECTORY	16
#define CMD_ERR_INVALID_CHARACTER			17
#define CMD_ERR_UNRECOGNISED_COMMAND		18
#define CMD_ERR_COMMAND_TOO_LONG			19
#define CMD_ERR_MODEM_OFF					20
#define CMD_ERR_READ_CAL_FAILED				21
#define CMD_ERR_REQUIRES_USB				22
#define CMD_ERR_SCRIPT_IN_PROGRESS			23
#define CMD_ERR_TEST_IN_PROGRESS			24
#define CMD_ERR_SERIAL_TIMED_OUT			25
#define CMD_ERR_SERIAL_COMMS_FAIL			26
#define CMD_ERR_NIVUS_BUSY					27
#define CMD_ERR_INCOMPATIBLE_HARDWARE		28
#define CMD_ERR_INTERNAL					255

// Pending command flags:
#define CMD_SOURCE_USB		0
#define CMD_SOURCE_SMS		1
#define CMD_SOURCE_FTP		2
#define CMD_SOURCE_SCRIPT	3
#define CMD_NUM_SOURCES		4

#define CMD_MAX_LENGTH			162

extern const uint16 CMD_word_mask[];

bool CMD_check_dirty_flags(void);
uint8 CMD_parse_rsd(PDU_sms_retrieve_type * p_dest, char * p_start, char * p_end);
#ifndef HDW_RS485
uint8 CMD_parse_dcc(DIG_config_type * p_dest, char * p_start, char * p_end);
#endif
#ifndef HDW_GPS
uint8 CMD_parse_acc(ANA_config_type * p_dest, char * p_start, char * p_end);
#endif
void  CMD_schedule_parse(uint8 index, char * input, char * output);
bool  CMD_busy(uint8 index);
bool  CMD_can_sleep(void);
void  CMD_task(void);
