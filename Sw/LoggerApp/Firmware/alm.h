/******************************************************************************
** File:	alm.h
**			P Boyce
**			15/7/09
**
** Notes:	alarm handling
**
** Changes:
** v2.38 031110 PB  Add bool ALM_com_mode_alarm_enable flag
**
** Waste Water
**
** V3.30 221111 PB  double number of alarm channels for derived values
**
** V3.08 280212 PB  add fn proto ALM_log_pwr_alarm() to log eco power changeover alarms
*/

#define ALM_ALARM_CHANNEL0			0
#define ALM_ALARM_CHANNEL1			1
#define ALM_ALARM_CHANNEL2			2
#define ALM_ALARM_CHANNEL3			3
#define ALM_ALARM_CHANNEL4			4
#define ALM_ANA_ALARM_CHANNEL0		9
#define ALM_ALARM_DERIVED_CHANNEL0	11
#define ALM_ANA_ALARM_DERIVED_CH0	15
#define ALM_NUM_ALARM_CHANNELS		22
#define ALM_TOD_NUMBER				4

#define ALM_PROFILES_PATH	"\\PROFILES\\"

// ftp alarm types
#define ALM_FTP_CLEAR			0
#define ALM_FTP_ENTER			1
#define ALM_FTP_BATTERY			2
#define ALM_FTP_ECO				3

// Configuration of an alarm channel
typedef struct
{
	bool  enabled;
	uint8 sample_interval;
	float default_high_threshold;
	float default_low_threshold;
	float deadband;
	uint8 debounce_delay;
	uint8 high_enable_mask;
	uint8 high_clear_mask;
	uint8 low_enable_mask;
	uint8 low_clear_mask;
	uint8 type;					// 0 = fixed threshold, 1 = profile, 2 = envelope 
	float profile_width;
	bool  width_is_multiplier;
} ALM_config_type;

// Configuration for time of day alarm
typedef struct
{
	bool  			enabled;
	RTC_hhmmss_type trigger_time;
} ALM_tod_config_type;

// Working registers for an alarm channel
typedef struct
{
	uint8 mask;
	uint8 high_debounce_count;
	uint8 low_debounce_count;
	float high_threshold;
	float low_threshold;
	float value;
	//float excursion;
} ALM_channel_type;

extern FAR ALM_tod_config_type ALM_tod_config[ALM_TOD_NUMBER];
extern FAR ALM_config_type ALM_config[ALM_NUM_ALARM_CHANNELS];
extern FAR ALM_channel_type ALM_channel[ALM_NUM_ALARM_CHANNELS];
extern FAR float ALM_profile_levels[ALM_NUM_ALARM_CHANNELS];
extern FAR float ALM_envelope_high[ALM_NUM_ALARM_CHANNELS];
extern FAR float ALM_envelope_low[ALM_NUM_ALARM_CHANNELS];

extern uint32 ALM_wakeup_time;
extern bool   ALM_com_mode_alarm_enable;

void ALM_log_pwr_alarm();
void ALM_configure_channel(uint8 channel_index);
void ALM_act_on_tod(void);
void ALM_send_ftp_alarm(uint8 type);
void ALM_send_to_all_sms_numbers(void);
void ALM_process_value(int index, float value);
void ALM_process_event(int index, bool value_is_high);
void ALM_update_profile(void);
bool ALM_can_sleep(void);
void ALM_task(void);
void ALM_init(void);

void ALM_modbus_process_value(int index, float value, float high_th, float low_th, bool high_alarm, bool low_alarm);

