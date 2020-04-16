/******************************************************************************
** File:	PWR.h
**
** Notes:	Power management
**
** Changes
**
** 180112 V2.99 PB  add configuration for ECO
**					add PWR_task and PWR_wakeup_time
**
** 190612 V3.01 PB  add debug LED flag to PWR_measurement_flags	and functions to enable, control and read it
**
** 091012 V3.17 PB  Bring up to date with Xilog+ V3.06 - new fn PWR_have_external_power()
**
** 171013 V3.29 PB  bring up to date with Xilog+ V3.05
**					new flag for battery alarm sent
**					new variable and functions for battery failure management and transmission of alarm
**
*/

// Bit positions in PWR_eco_config_type.flags
#define PWR_ECO_PRODUCT				_B00000001
#define PWR_ECO_BELOW_THRESHOLD		_B00000010

// Alarm enable flags
#define PWR_LOG_ALARM				_B00000100
#define PWR_SMS_ALARM				_B00000010
#define PWR_FTP_ALARM				_B00000001

// PWR_measurement_flags masks
#define PWR_MASK_MODEM_ON			_B00000001
#define PWR_MASK_SD_ON				_B00000010
#define PWR_MASK_DEBUG_LED_ON		_B00000100
#define PWR_MASK_BATT_ALARM_SENT	_B00001000
#define PWR_MASK_LOG_USAGE			_B10000000

// ECO product variant configuration structure
typedef struct
{
	uint8 flags;
	float low_alarm_threshold;
	uint8 cla_val_sample_interval;
	uint8 internal_sample_interval;
	uint8 eco_power_loss_mask;
	uint8 eco_power_restored_mask;
} PWR_eco_config_type;

extern PWR_eco_config_type  PWR_eco_config;

extern uint32 PWR_wakeup_time;

extern float PWR_int_bat_when_failed;
extern float PWR_int_bat_volts;
extern float PWR_ext_supply_volts;

// values read for battery alarms:
extern float PWR_last_ext_supply;
extern uint8 PWR_measurement_flags;		// bit 0 = modem on, bit 1 = SD on, bit 2 = debug LED on

bool PWR_have_external_power(void);
void PWR_enable_debug_led(bool enable);
bool PWR_debug_led_enabled(void);
void PWR_drive_debug_led(bool led_on);
void PWR_tx_internal_batt_alarm(void);
void PWR_task(void);
void PWR_eco_init(void);
void PWR_set_pending_batt_test(bool log_usage);
bool PWR_measure_in_progress(void);
void PWR_read_diode_offset(void);

