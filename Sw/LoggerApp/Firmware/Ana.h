/******************************************************************************
** File:	Ana.h
**
** Notes:	Analogue channels
**
** V3.03 161111 PB Waste Water - add derived flow from depth config to channel config
**		 201111 PB               add derived sample value to ANA_channel typedef
**
** V3.14 130612 PB add depth to flow conversion constants and flow table values to ANA_config typedef
**
** V3.16 210612 PB separate out config for number of channels and number of derived channels
**
** V3.23 310513 PB add ANA_insert_derived_header()
**
** V4.00 220114 PB disable if HDW_GPS defined
*/

#ifndef HDW_GPS

#include "HardwareProfile.h"	// essential for 3ch/9ch selection


#if (HDW_NUM_CHANNELS == 3)
#define ANA_NUM_CHANNELS			4
#define ANA_NUM_DERIVED_CHANNELS	2
#define ANA_DEPTH_TO_FLOW_POINTS	40
#else
#define ANA_NUM_CHANNELS			7
#define ANA_NUM_DERIVED_CHANNELS	7
#define ANA_DEPTH_TO_FLOW_POINTS	6
#endif

#define ANA_DEFAULT_BOOST_TIME_MS	50

#if (HDW_NUM_CHANNELS == 9)
#define ANA_MAX_CHANNEL_MASK	_B01111111
#else
#define ANA_MAX_CHANNEL_MASK	_B00001111
#endif

#define ANA_SENSOR_NONE		0														// Transducer types:
#define ANA_SENSOR_DIFF_MV	1
#define ANA_SENSOR_0_2V		2
#define ANA_SENSOR_0_5V		3
#define ANA_SENSOR_0_10V	4
#define ANA_SENSOR_CURRENT	5														// 4-20mA

#define ANA_DEPTH_TO_FLOW_RECTANGULAR	1											// derived data types	
#define ANA_DEPTH_TO_FLOW_V_NOTCH		2
#define ANA_DEPTH_TO_FLOW_VENTURI		3
#define ANA_DEPTH_TO_FLOW_TABLE			4

#define ANA_MASK_CHANNEL_ENABLED			_B00000001								// Bit positions in ANA_config_type.flags
#define ANA_MASK_POWER_TRANSDUCER			_B00000010
#define ANA_MASK_MESSAGING_ENABLED			_B00000100
#define ANA_MASK_DERIVED_DATA_ENABLED		_B00001000
#define ANA_MASK_DERIVED_MESSAGING_ENABLED	_B00010000

// Analogue channel configuration structure
typedef struct																		// Analogue channel configuration structure
{
	uint8 flags;
	uint8 sensor_type;
	uint8 sample_interval;
	uint8 log_interval;
	uint8 min_max_sample_interval;
	uint8 sms_data_interval;
	float user_offset;
	float auto_offset;
	float e0;
	float e1;
	float p0;
	float p1;
	uint8 sensor_index;
	uint8 sms_message_type;
	uint8 description_index;
	uint8 units_index;
	uint8 derived_type;
	uint8 derived_sensor_index;
	uint8 derived_sms_message_type;
	uint8 derived_description_index;
	uint8 derived_units_index;
} ANA_config_type;

// analogue channel depth to flow conversion data - only on main channels
typedef struct
{
	float min_value;																// depth to flow conversion constants
	float max_value; 
	float K_value;
	float k_value;
	float input_value[ANA_DEPTH_TO_FLOW_POINTS];									// depth to flow look up table
	float point_value[ANA_DEPTH_TO_FLOW_POINTS];
} ANA_derived_config_type;

// Analogue channel registers
typedef struct
{
	uint32 time;
	float total;
	uint16 count;
} ANA_register_type;

// Analogue channel working registers
typedef struct
{
	uint32 sample_time;

	ANA_register_type log;
	ANA_register_type sms;
	ANA_register_type min_max;
	ANA_register_type normal_alarm;
	ANA_register_type derived_alarm;

	float sample_value;
	float derived_sample_value;

	RTC_hhmmss_type min_time;
	float min_value;
	RTC_hhmmss_type max_time;
	float max_value;

	RTC_hhmmss_type min_derived_time;
	float min_derived_value;
	RTC_hhmmss_type max_derived_time;
	float max_derived_value;

	float amplifier_gain;				// electrical value = (gain * adc value) + offset
	float amplifier_offset;
} ANA_channel_type;

extern bool ANA_on_perm_flag;
extern bool ANA_boost_on_perm_flag;

extern uint16 ANA_vref_counts;			// 2.5V ref channel
extern uint16 ANA_zero_counts;			// diff mV zero reading
extern uint16 ANA_boost_time_ms;		// time used when logger powering 4-20mA

extern FAR ANA_config_type  ANA_config[ANA_NUM_CHANNELS];
extern FAR ANA_derived_config_type ANA_derived_config[ANA_NUM_DERIVED_CHANNELS];
extern FAR ANA_channel_type ANA_channel[ANA_NUM_CHANNELS];

bool ANA_retrieve_derived_conversion_data(uint8 channel, uint8 derived_type);
bool ANA_channel_exists(int index);
bool ANA_configure_channel(uint8 index);
bool ANA_busy(void);
void ANA_print_config(int index, char *s);
void ANA_start_adc_read(int index);
bool ANA_get_adc_values(int index);
void ANA_ext_power_disconnected(void);
void ANA_task(void);
void ANA_start_stop_logging(void);
void ANA_insert_derived_header(uint8 index);
void ANA_synchronise(void);
int  ANA_create_block_footer(char * string, uint8 channel, bool derived);
float ANA_immediate_value(uint8 channel_id);
void ANA_vref_off(void);
bool ANA_power_transducer_required(void);

#else
#define ANA_ext_power_disconnected()
#endif
