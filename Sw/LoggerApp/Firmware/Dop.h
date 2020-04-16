/******************************************************************************
** File:	Dop.h	
**
** Notes:	Header file for Doppler sensor logging code
**
** v3.04 181211 PB First version for Waste Water
**
** v3.16 210612 PB add DOP_retrieve_derived_conversion_data() to read depth to pipe diameter file into RAM
**				   add DOP_derived_config type and memory
**
** V3.27 250613 PB DEL194: debug #NVM faults
**				   DEL195: remove individual DOP_configure_xxx() functions
**
** V4.11 260814 PB add DOP_sensor_present flag
*/

#ifdef HDW_RS485

#define DOP_DEPTH_TO_AREA_POINTS				40
#define DOP_PI									3.14159265f
// Bit positions in DOP_config_type.sensor_flags
#define DOP_MASK_SENSOR_ENABLED					_B00000001
#define DOP_MASK_DEPTH_SOURCE					_B00000010

// Bit positions in DOP_config_type.flags
#define DOP_MASK_VELOCITY_LOG_ENABLED			_B00000001
#define DOP_MASK_VELOCITY_MESSAGING_ENABLED		_B00000010
#define DOP_MASK_TEMPERATURE_LOG_ENABLED		_B00000100
#define DOP_MASK_TEMPERATURE_MESSAGING_ENABLED	_B00001000
#define DOP_MASK_DEPTH_LOG_ENABLED				_B00010000
#define DOP_MASK_DEPTH_MESSAGING_ENABLED		_B00100000
#define DOP_MASK_DERIVED_FLOW_LOG_ENABLED		_B01000000
#define DOP_MASK_DERIVED_FLOW_MESSAGING_ENABLED	_B10000000

typedef struct																		// doppler sensor configuration structure
{
	uint8 sensor_flags;
	uint8 flags;
	uint8 sensor_index;
	uint8 depth_data_channel;
	uint8 log_interval;
	uint8 min_max_sample_interval;
	uint8 sms_data_interval;
	uint8 velocity_sms_message_type;
	float velocity_cal;
	uint8 velocity_description_index;
	uint8 velocity_units_index;
	uint8 temperature_sms_message_type;
	uint8 temperature_description_index;
	uint8 temperature_units_index;
	uint8 depth_sms_message_type;
	float depth_cal;
	uint8 depth_description_index;
	uint8 depth_units_index;
	uint8 flow_pipe_area_source;
	float flow_pipe_diameter_m;
	float sensor_offset_m;
	uint8 flow_sms_message_type;
	float flow_cal;
	uint8 flow_description_index;
	uint8 flow_units_index;
} DOP_config_type;

typedef struct
{
	float min_value;																// depth to area conversion constants
	float max_value; 
	float K_value;
	float k_value;
	float input_value[DOP_DEPTH_TO_AREA_POINTS];									// depth to area look up table
	float point_value[DOP_DEPTH_TO_AREA_POINTS];
} DOP_derived_config_type;
																					// Doppler sensor registers
typedef struct
{
	uint32 time;
	float total;
	uint16 count;
} DOP_register_type;

typedef struct																		// Doppler sensor working registers
{
	DOP_register_type velocity_sms;
	DOP_register_type temperature_sms;
	DOP_register_type depth_sms;
	DOP_register_type derived_flow_sms;
	DOP_register_type velocity_min_max;
	DOP_register_type temperature_min_max;
	DOP_register_type depth_min_max;
	DOP_register_type derived_flow_min_max;
	DOP_register_type velocity_alarm;
	DOP_register_type temperature_alarm;
	DOP_register_type depth_alarm;
	DOP_register_type derived_flow_alarm;

	float velocity_value_mps;
	float velocity_value;
	float temperature_value;
	float depth_value;
	float derived_flow_value;

	RTC_hhmmss_type velocity_min_time;
	float velocity_min_value;
	RTC_hhmmss_type velocity_max_time;
	float velocity_max_value;

	RTC_hhmmss_type temperature_min_time;
	float temperature_min_value;
	RTC_hhmmss_type temperature_max_time;
	float temperature_max_value;

	RTC_hhmmss_type depth_min_time;
	float depth_min_value;
	RTC_hhmmss_type depth_max_time;
	float depth_max_value;

	RTC_hhmmss_type derived_flow_min_time;
	float derived_flow_min_value;
	RTC_hhmmss_type derived_flow_max_time;
	float derived_flow_max_value;
} DOP_channel_type;


extern DOP_config_type  		DOP_config;
extern DOP_derived_config_type  DOP_derived_config;
extern DOP_channel_type 		DOP_channel;
extern char  					DOP_nivus_code;
extern uint32 					DOP_wakeup_time;
extern bool						DOP_sensor_present;

bool DOP_retrieve_derived_conversion_data(void);
void DOP_configure_sensor(void);
int  DOP_create_block_footer(char * string, uint8 channel, bool derived);
float DOP_immediate_value(uint8 channel_id);
void DOP_start_sensor(void);
bool DOP_busy(void);
void DOP_task(void);
void DOP_init(void);

#endif
