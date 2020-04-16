/******************************************************************************
** File:	Pdu.h
**			P Boyce
**			8/7/09
**
** Notes:	PDU message coding - header file
**
** Changes
**
** v2.39 231110 PB	New struct PDU_sms_header_type allows header contents to be compared
**
** v2.49 250111 PB  Add PDU_disable_batch - sets a channel's batch count to -1
**
** V3.03 231111 PB Waste Water - add items to handle derived data sms pdu tx
**								 reduce length of file and line buffer to save RAM
*/

#define PDU_LOG_INTERVALS			96
#define PDU_FDATA_BUFFER_SIZE		PDU_LOG_INTERVALS	// size of floating point logged data buffer for encoding

#define PDU_FILE_BUFFER_SIZE		128			// size of buffer for reading raw data from a file
#define PDU_LINE_BUFFER_SIZE		64			// size of buffer for reading raw data from a file
#define PDU_HEX_BUFFER_SIZE			340			// max size will be 331			
#define PDU_BYTE_BUFFER_SIZE		140			// temporary buffer,  to cover header size or data size
#define PDU_USER_DATA_LENGTH		140 
#define PDU_MAX_HEADER_BYTES		24

#define PDU_TENBIT_NO_DATA_VALUE	1023
#define PDU_C_FLOW_NO_DATA_VALUE	127
#define PDU_C_PRES_NO_DATA_VALUE	15

#define	PDU_ADDRESS_TYPE			0x81		//TODO_PHB this will finally be held in a configuration file

#define PDU_FLOW_MAX 				(float)255
#define PDU_FLOW_MIN 				(float)-255
#define PDU_PRESSURE_MAX 			(float)254
#define PDU_PRESSURE_MIN 			(float)0

#define PDU_AN0_CHANNEL_NUM			4
#define PDU_NUM_NORMAL_CHANNELS		11
#define PDU_DERIVED_AN0_CHANNEL_NUM	15
#define PDU_NUM_SMSR_CHANNELS		22

// Configuration of sms data retrieval
typedef struct
{
	uint8    channel;
	RTC_type when;
} PDU_sms_retrieve_type;

extern FAR PDU_sms_retrieve_type PDU_rsd_retrieve;		// place for commanded timestamp

typedef struct
{
	uint8	data_interval;
	uint8	description_index;
	uint8	units_index;
	uint8	message_type;
} PDU_sms_header_data_type;

// Configuration of sms file header
typedef struct
{
	PDU_sms_retrieve_type	 	timestamp;
	PDU_sms_header_data_type	data;
} PDU_sms_header_type;

void  PDU_schedule(uint8 channel);
bool  PDU_time_for_batch(uint8 channel);
void  PDU_schedule_all(void);
void  PDU_test_message(char * p_id, char * p_phone_number);
uint8 PDU_retrieve_data(bool to_host);
bool  PDU_busy(void);
void  PDU_task(void);
void  PDU_init(void);
