/******************************************************************************
** File:	Cop.h	
**
** Notes:	Header file for control output functions
**
** V3.00 210911 PB First version for Waste Water
**
** V3.32 201113 PB add prototype for COP_value_to_string()
*/

#define COP_NUM_CHANNELS	2

// Bit positions in COP_config_type.flags
#define COP_MASK_OUTPUT_ENABLED 	_B00000001							// set by #SCO
#define COP_MASK_OUTPUT_TYPE		_B00000010
#define COP_MASK_CURRENT_STATE		_B00000100
#define COP_MASK_REST_STATE			_B00001000
#define COP_MASK_AUTO_ENABLED		_B00010000							// set by #ACO
#define COP_MASK_QUANTITY			_B00100000
#define COP_MASK_IMMEDIATE_TRIGGER	_B01000000							// set by #TCO
#define COP_MASK_TIMED_TRIGGER		_B10000000

// Bit positions in COP_config_type.flags2
#define COP_MASK_REGULAR_ENABLED	_B00000001							// set by #RCO
#define COP_MASK_EVENT_ENABLED		_B00000010							// set by #ECO

// control output configuration type
typedef struct
{
	uint8			flags;												// set by #ACO, #SCO, #TCO
	uint8			flags2;												// set by #RCO, #ECO
	uint8 			report_enable_mask;									// set by #SCO
	uint16 			pulse_width_msec;
	uint16 			pulse_interval_msec;
	uint8 			number_of_pulses;
	uint8			auto_channel_id;									// set by #ACO
	uint8			auto_event_gate;
	COM_window_type auto_window;
	float			quantity;
	float			high_threshold;
	float			low_threshold;
	float			deadband;
	uint8			debounce_delay;
	uint8			threshold_trigger_mask;
	uint8			regular_event_gate;									// set by #RCO
	COM_window_type regular_window;
	RTC_hhmm_type 	trigger_time;										// set by #TCO
	uint8			event_channel_id;									// set by #ECO
} COP_config_type;

extern COP_config_type COP_config[COP_NUM_CHANNELS];

extern uint32 COP_wakeup_time;

int  COP_value_to_string(uint8 value);
void COP_recalc_wakeups(void);
void COP_configure_channel(int i);
void COP_act_on_trigger_channel(int i);
void COP_start_auto(int i);
bool COP_can_sleep(void);
void COP_task(void);
void COP_init(void);
