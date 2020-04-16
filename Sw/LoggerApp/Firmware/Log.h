/******************************************************************************
** File:	Log.h
**
** Notes:
**
** V2.77 160611 PB new LOG_BLOCK_HEADER_PARAMS data type for enqueueing block header parameters	
**
** V3.03 161111 PB Waste Water - add derived value log mask
**
** V3.08 280212 PB ECO product - add function prototype LOG_set_pending_battery_test()
**
** V3.09 270312 PB remove LOG_set_pending_battery_test() as per V2.99 5th
**
** V3.29 171013 PB bring up to date with Xilog+ V3.05
**				   use new log state LOG_BATT_DEAD instead of LOG_VOLTAGE_TOO_LOW
**
** V3.32 201113 PB add indices for control output logging - move SMS and DERIVED MASKS to make room
**
** V3.33 251113 PB change control output logging indices and masks
**
*/

// Logging function indices:
// Note: these agree as far as possible with the channel numbers used in SMS messages.
#define LOG_ACTIVITY_INDEX		0
#ifdef HDW_RS485
#define LOG_SERIAL_1_INDEX		1		// modbus flow speed m/s
#define LOG_SERIAL_2_INDEX		2		// modbus volume m3/hr
#define LOG_SERIAL_3_INDEX		3		// modbus total flow m3
#define	LOG_SERIAL_4_INDEX		4		// modbus pressure mbar
#define	LOG_SERIAL_5_INDEX		5		// modbus temperature k
#define	LOG_SERIAL_6_INDEX		6		// modbus forward flow m3
#define	LOG_SERIAL_7_INDEX		7		// modbus reverse flow m3
#define	LOG_SERIAL_8_INDEX		8		// modbus flags
//#define LOG_DS1_INDEX			4		// Derived flow channel DS1
#else
#define LOG_DIGITAL_1A_INDEX	1
#define LOG_DIGITAL_1B_INDEX	2
#define LOG_DIGITAL_2A_INDEX	3
#define LOG_DIGITAL_2B_INDEX	4
#endif
#define LOG_ANALOGUE_1_INDEX	9		// Pressure or voltage
#define LOG_ANALOGUE_2_INDEX	10		// current
#define LOG_ANALOGUE_3_INDEX	11		// Pressure or voltage
#define LOG_EVENT_1A_INDEX		12		// event
#define LOG_EVENT_1B_INDEX		13		// event
#define LOG_EVENT_2A_INDEX		14		// event
#define LOG_EVENT_2B_INDEX		15		// event
#define LOG_NUM_FUNCTIONS		16		// activity, 2 * 2-way digital, 7 * analogue, 4 * event

// Add this mask to the above to log as SMS data. 16 is added to indices 1 to 11 to define SMS log values
#define LOG_SMS_MASK			_B00010000

// Add this mask to the above to log as derived data. 32 is added to indices 1 to 11 to define derived log values
#define LOG_DERIVED_MASK		_B00100000

// So _B01100000 is added to define derived SMS log values

// control output logging indices and masks
#define LOG_CONTROL_1_INDEX			1	// control output 1
#define LOG_CONTROL_2_INDEX			2	// control output 2
#define LOG_NUM_CONTROL_CHANNELS	2
// add this mask to define code as a control output to be logged
#define LOG_CONTROL_MASK			_B01000000

#define LOG_ACT_FILE			0
#define LOG_ALM_FILE			1
#define LOG_ANA_FILE			2
#define LOG_CAL_FILE			3
#define LOG_CFS_FILE			4
#define LOG_CMD_FILE			5
#define LOG_COM_FILE			6
#define LOG_COP_FILE			7
#define LOG_DIG_FILE			8
#define LOG_DOP_FILE			9
#define LOG_DSK_FILE			10
#define LOG_FRM_FILE			11
#define LOG_FTP_FILE			12
#define LOG_GPS_FILE			13
#define LOG_LOG_FILE			14
#define LOG_MAIN_FILE			15
#define LOG_MDM_FILE			16
#define LOG_MSG_FILE			17
#define LOG_PDU_FILE			18
#define LOG_PWR_FILE			19
#define LOG_RTC_FILE			20
#define LOG_SCF_FILE			21
#define LOG_SER_FILE			22
#define LOG_SLP_FILE			23
#define LOG_SNS_FILE			24
#define LOG_STR_FILE			25
#define LOG_TIM_FILE			26
#define LOG_TSYNC_FILE			27
#define LOG_USB_FILE			28
#define LOG_MOD_FILE			29
#define LOG_NUM_C_FILES			30


// Data type enqueued with item:
#define LOG_DATA_VALUE				0		// normal value
#define LOG_EVENT_TIMESTAMP			1		// event timestamp
#define LOG_EVENT_HEADER			2		// event header
#define LOG_BLOCK_HEADER_PARAMS		3		// data block header parameters
#define LOG_BLOCK_HEADER_TIMESTAMP	4		// data block header timestamp
#define LOG_BLOCK_FOOTER			5		// data block footer
#define LOG_CONTROL_TYPE			6		// control output type
#define LOG_TOTALISER_TIMESTAMP		7		// timestamp + fraction
#define LOG_TOTALISER_LS			8		// integer part LS 32 bits
#define LOG_TOTALISER_MS			9		// integer part MS 32 bits

// Logging states:
#define LOG_BATT_DEAD				0		// danger of corrupting file system
#define LOG_STOPPED					1		// transducers off, no measurement
#define LOG_PRE_LOGGING				2		// before log start time
#define LOG_LOGGING					3		// between log start & end times

typedef struct
{
	RTC_start_stop_type start;
	RTC_start_stop_type stop;
	uint32 min_space_remaining;
} LOG_config_type;

extern uint8 LOG_state;

// If first value of new data block, enqueue timestamp for header.
// Clear the corresponding bit in the mask to ensure header is added.
extern uint16 LOG_header_mask;
extern uint16 LOG_derived_header_mask;
extern uint16 LOG_sms_header_mask;
extern uint16 LOG_derived_sms_header_mask;

extern uint32 LOG_wakeup_time;

extern LOG_config_type LOG_config;

#ifndef extern
extern const char LOG_channel_id[LOG_NUM_FUNCTIONS][4];
extern const uint32 LOG_interval_sec[];
#endif

void LOG_entry(char * use_string);
void LOG_set_next_time(uint32 *p, uint8 time_enum, bool wrap_at_midnight);
uint32 LOG_get_timestamp(uint32 t, uint8 time_enum);
bool LOG_enqueue_value(uint8 channel_number, uint8 data_type, int32 value);
int LOG_create_event_header(char * buffer_p, int channel_index, RTC_type * time_stamp_p);
int LOG_create_block_header(char * buffer_p, int channel_index, RTC_type * time_stamp_p);
void LOG_set_wakeup_time(void);
void LOG_flush(void);
bool LOG_busy(void);
void LOG_init(void);
void LOG_task(void);
int LOG_print_footer_min_max(char * string, RTC_hhmmss_type *p_t1, float v1, RTC_hhmmss_type *p_t2, float v2);



