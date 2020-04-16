/******************************************************************************
** File:	Dig.h
**
** Notes:	Digital channels
**
** v2.59 010311 PB DEL63 new prototype for function DIG_insert_event_headers for command #EC to call
**
** V3.00 210911 PB Waste Water - made digital input definitions global
**       240911                  add function DIG_immediate_value() - works like DIG_print_immediate_values
**
** V3.01 041011 				 add subchannel event config type for #CEC command
**
** V3.02 171011					 add pump flow rate table of 16 floats
**								 add pump running times type and memory
**
** V3.04 221211					 compiler switch off if RS485 to save or reuse memory
**
** V3.23 310513 PB			 	no longer any need for DIG_insert_event_headers() - done in dig_start_stop_logging()
*/

#ifndef HDW_RS485

#define DIG_NUM_CHANNELS	2

#if (HDW_NUM_CHANNELS == 9)															// event interrupt lines
#define DIG_INT1_LINE HDW_CH1_FLOW_1
#define DIG_INT2_LINE HDW_CH1_FLOW_2
#define DIG_INT3_LINE HDW_CH2_FLOW_1
#define DIG_INT4_LINE HDW_CH2_FLOW_2
#else																				// 3-channel
#define DIG_INT1_LINE HDW_FLOW_1
#define DIG_INT2_LINE HDW_FLOW_2
#define DIG_INT3_LINE DIG_INT1_LINE
#define DIG_INT4_LINE DIG_INT2_LINE
#endif

#define DIG_PULSE_MASK				0x07
#define DIG_TYPE_NONE				0												// Pulse counting sensor types in LS 3 bits of sensor type:
#define DIG_TYPE_FWD_A				1
#define DIG_TYPE_FWD_A_FWD_B		2
#define DIG_TYPE_FWD_A_REV_B		3
#define DIG_TYPE_BOTH_A_REV_B		4
#define DIG_TYPE_DIR_B_HIGH_FWD		5
#define DIG_TYPE_DIR_B_HIGH_REV		6

#define DIG_EVENT_MASK				0xF0
#define DIG_EVENT_A_MASK			0x30
#define DIG_EVENT_B_MASK			0xC0
#define DIG_EVENT_A_NONE			0x00											// Event sensor types in MS 4 bits of sensor type:
#define DIG_EVENT_A_RISE			0x10
#define DIG_EVENT_A_FALL			0x20
#define DIG_EVENT_A_BOTH			0x30
#define DIG_EVENT_B_NONE			0x00
#define DIG_EVENT_B_RISE			0x40
#define DIG_EVENT_B_FALL			0x80
#define DIG_EVENT_B_BOTH			0xC0

#define DIG_MASK_CHANNEL_ENABLED		_B00000001									// Bit positions in DIG_config_type.flags
#define DIG_MASK_CONTINUOUS_POWER		_B00000010
#define DIG_MASK_VOLTAGE_DOUBLER		_B00000100
#define DIG_MASK_COMBINE_SUB_CHANNELS	_B00001000
#define DIG_MASK_MESSAGING_ENABLED		_B00010000
#define DIG_MASK_DERIVED_VOL_ENABLED	_B00100000

typedef struct																		// Configuration of an event sub channel
{
	uint8 flags;
	uint8 sensor_type;
	uint8 log_interval;
	uint8 min_max_sample_interval;
	uint8 sms_data_interval;
	float cal;
	uint8 rate_enumeration;
	uint8 sensor_index;
	uint8 sms_message_type;
	uint8 description_index;
	uint8 event_units_index;
	uint8 output_units_index;
} DIG_sub_event_config_type;

typedef struct																		// Configuration of a digital channel
{
	uint8 flags;
	uint8 sensor_type;
	uint8 sample_interval;
	uint8 log_interval;
	uint8 min_max_sample_interval;
	uint8 sms_data_interval;
	uint8 event_log_min_period;
	uint8 pit_min_period;
	float fcal_a;
	float fcal_b;
	uint8 rate_enumeration;
	uint8 sensor_index;
	uint8 sms_message_type;
	uint8 description_index;
	uint8 units_index;
	uint8 ff_pulse_width_x32us;
	uint8 ff_period[8];
	DIG_sub_event_config_type ec[2];
} DIG_config_type;

typedef struct																		// Config and working values for totaliser
{
	uint32 tcal_x10000;																// 0 - 100000 representing 0 - 10.0
	long long int value_x10000;														// 0.0 - 9999999999.9999
	uint8 units_enumeration;														// 0..15, set by host & reported in SMS type F3
	uint8 log_interval_enumeration;
} DIG_totaliser_type;

typedef struct																		// Structure for sub-channels A and B of a digital channel
{
	uint8  event_flag;

	uint16 sample_count;
	uint16 previous_sample_count;
	uint16 previous_event_count;
	uint16 event_count;

	int32 log_count;
	int32 sms_count;
	int32 normal_alarm_count;

	uint32 normal_alarm_time;
	uint32 derived_alarm_time;
	uint32 event_time;
	uint32 event_log_time;
	uint32 event_sms_time;
	uint32 event_min_max_sample_time;

	float sms_amount;
	float normal_alarm_amount;
	float min_max_sample;
	float last_derived_volume;
	float derived_volume;
	float derived_sms_volume;
	float derived_alarm_volume;
	float derived_min_max_sample;

	RTC_hhmmss_type min_time;
	float min_value;
	RTC_hhmmss_type max_time;
	float max_value;

	RTC_hhmmss_type min_derived_time;
	float min_derived_value;
	RTC_hhmmss_type max_derived_time;
	float max_derived_value;

	DIG_totaliser_type totaliser;

} DIG_sub_channel_type;

typedef struct																		// Working registers for a digital channel:
{
	uint8 state;

	uint32 sample_time;
	uint32 log_time;
	uint32 sms_time;
	uint32 min_max_sample_time;

	DIG_sub_channel_type sub[2];

} DIG_channel_type;

extern DIG_config_type DIG_config[DIG_NUM_CHANNELS];
extern DIG_channel_type DIG_channel[DIG_NUM_CHANNELS];

extern bool DIG_INT1_active;
extern bool DIG_INT2_active;

#if (HDW_NUM_CHANNELS == 9)
extern bool DIG_INT3_active;
extern bool DIG_INT4_active;
#endif

extern float DIG_pfr_table[16];														// system pump flow rate table

typedef struct																		// system pump working registers
{
	uint32	hrs;
	uint8	mins;
	uint8	secs;
}DIG_prt_type;

typedef struct
{
	bool			on;																// present state of pump defined by state of interrupt line
	uint32			time_on_500ms;													// time on in half seconds
	float			volume;
	float			previous_volume;
	DIG_prt_type	running_time;
}DIG_system_pump_type;

extern DIG_system_pump_type DIG_system_pump[4];

extern float  DIG_system_volume;
extern float  DIG_system_previous_volume;
extern uint32 DIG_system_change_time_500ms;

void DIG_init(void);
int DIG_print_totaliser_int(char *buffer, long long int *p_value_x10000);
int DIG_create_block_footer(char * string, uint8 channel, bool sms, bool derived);
void DIG_insert_headers(int index);
void DIG_configure_channel(int n);
void DIG_task(void);
bool DIG_busy(void);
int DIG_print_config(int index, char * s);
void DIG_start_stop_logging(void);
void DIG_print_immediate_values(int index, bool units_as_strings);
void DIG_print_immediate_derived_values(int index, bool units_as_strings);
float DIG_immediate_value(uint8 channel_id, bool per_second);
void DIG_synchronise(void);
float DIG_combine_flows(uint8 sensor_type, float a, float b);
float DIG_volume_to_rate_enum(float value, uint8 time_enumeration, uint8 rate_enumeration);

#endif
