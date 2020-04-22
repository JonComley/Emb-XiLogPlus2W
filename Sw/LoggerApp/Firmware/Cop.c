/******************************************************************************
** File:	Cop.c	
**
** Notes:	Control Output functions
**
** v3.00 210911 PB First version for Waste Water
**
** V3.03 251111 PB cop_event_gate_true() modified to only test one input of four against common sense flag
**
** V3.12 070612 PB change to FTP filename for control messages: "ALARM_CONTROL<xxxxx>.TXT"
**
** V3.29 151013 PB DEL201: add wait for file system to task and remove from cop_log_control()
**					bring up to date with Xilog+ V3.05
**					use new log state LOG_BATT_DEAD instead of LOG_VOLTAGE_TOO_LOW
**
** V3.31 131113 PB reposition cop_day_bcd = RTC_now.day_bcd at end of COP_task() and in COP_init()
**
** V3.32 201113 PB return cop_day_bcd = RTC_now.day_bcd to cop_new_day_task()
**				   DEL201: fix in V3.29 keeps SD card on permanently - enqueue control output event as an activity
**				   add COP_value_to_string() to decode value to letters when logging event
**
** V3.33 251113 PB change in enqueueing control output logs
			sprintf(cop_filename_str, "ALARM_CONTROL%05d.TXT", cop_ftp_msg_index);
			MSG_send(MSG_TYPE_FTP_MSG, STR_buffer,  cop_filename_str);
**
** V4.02 090414 PB add COP_EVENT case to COP_value_to_string()
**				   reorganise cop_state into an array
**				   do not count pulse width until out of COP_IDLE_WAIT state
**				   ensure file system is woken up when task has detected it is time to do something
**				   rewrite interrupt event handling in COP_task()
*/

#include <string.h>
#include <stdio.h>
#include <float.h>

#include "Custom.h"
#include "Compiler.h"
#include "Str.h"
#include "HardwareProfile.h"
#include "MDD File System/FSIO.h"
#include "Tim.h"
#include "Cfs.h"
#include "rtc.h"
#include "Msg.h"
#include "Com.h"
#include "Slp.h"
#include "Cal.h"
#include "Dig.h"
#include "Ana.h"
#include "Dop.h"
#include "Log.h"
#include "ftp.h"
#include "alm.h"
#include "Pdu.h"
#include "usb.h"

#define extern
#include "Cop.h"
#undef extern

// definitions of task states

#define COP_IDLE		0
#define COP_IDLE_WAIT	1
#define COP_MARK		2
#define COP_SPACE		3

// control output config action masks:
#define COP_ACTION_LOG				_B00000100
#define COP_ACTION_SMS				_B00000010
#define COP_ACTION_FTP				_B00000001

// control output description indices:
#define COP_IMMEDIATE				1
#define COP_TIMED					2
#define COP_AUTO_EH					3
#define	COP_AUTO_EL					4
#define COP_AUTO_CH					5
#define COP_AUTO_CL					6
#define COP_AUTO_Q					7
#define COP_REGULAR					8
#define COP_EVENT					9

// Threshold trigger channel masks
#define COP_MASK_IN_HIGH_ALARM		_B00000001
#define COP_MASK_ENTER_HIGH_ALARM	_B00000010
#define COP_MASK_EXIT_HIGH_ALARM	_B00000100
#define COP_MASK_IN_LOW_ALARM		_B00010000
#define COP_MASK_ENTER_LOW_ALARM	_B00100000
#define COP_MASK_EXIT_LOW_ALARM		_B01000000

// Threshold trigger enable masks
#define COP_MASK_ABOVE_HIGH_ALARM	_B00001000
#define COP_MASK_BELOW_HIGH_ALARM	_B00000100
#define COP_MASK_BELOW_LOW_ALARM	_B00000010
#define COP_MASK_ABOVE_LOW_ALARM	_B00000001

// local variables

uint8 cop_state[2];																				// independent state machines and outputs

uint8 cop_day_bcd;
uint16 cop_ftp_msg_index;

typedef struct
{
	uint32 timed_trigger;
	uint32 regular_window_trigger;
	uint32 auto_sample;
} cop_time_type;

cop_time_type cop_time[COP_NUM_CHANNELS];

typedef struct
{
	uint8 mask;
	uint8 high_debounce_count;
	uint8 low_debounce_count;
	float last_value;
	float quantity_value;
} cop_auto_values_type;

cop_auto_values_type cop_auto_values[COP_NUM_CHANNELS];

typedef struct
{
	uint16 cop_pulse_width_timer_x20ms;
	uint16 cop_pulse_interval_timer_x20ms;
	uint8  cop_pulse_counter;
} cop_pulse_timers_type;

cop_pulse_timers_type cop_pulse_timers[COP_NUM_CHANNELS];

FAR	char  cop_filename_str[16];
FAR	char  cop_path_str[32];
FAR char  cop_file_str[16];

const char * const cop_descriptions[10] =
{
	"U", "I", "T", "EH", "EL", "CH", "CL", "Q", "R", "E"
};
	

// private functions

/******************************************************************************
** Function:	Adjust wakeup times on or after midnight
**
** Notes:		Run once per day at midnight
*/
void cop_new_day_task(void)
{
	int i;

	for (i = 0; i < COP_NUM_CHANNELS; i++)
	{
		cop_time[i].regular_window_trigger = COM_first_window_time(&COP_config[i].regular_window);
		cop_time[i].auto_sample = COM_first_window_time(&COP_config[i].auto_window);
		if (cop_time[i].timed_trigger != SLP_NO_WAKEUP)
		{
			if (cop_time[i].timed_trigger >= RTC_SEC_PER_DAY)
				cop_time[i].timed_trigger -= RTC_SEC_PER_DAY;
		}
	}

	COP_wakeup_time = 0;
	cop_day_bcd = RTC_now.day_bcd;
}

/******************************************************************************
** Function:	cop_toggle_output
**
** Notes:		toggles current state and drives output
*/
void cop_toggle_output(int i)
{
	if (i == 0)
	{
		COP_config[i].flags ^= COP_MASK_CURRENT_STATE;											// toggle current state
		HDW_CONTROL1_ON = 
			((COP_config[i].flags & COP_MASK_CURRENT_STATE) == 0x00) ? 0 : 1;					// set output 1 to current state
	}
	else if (i == 1)
	{
		COP_config[i].flags ^= COP_MASK_CURRENT_STATE;											// toggle current state
		HDW_CONTROL2_ON = 
			((COP_config[i].flags & COP_MASK_CURRENT_STATE) == 0x00) ? 0 : 1;					// set output 2 to current state
	}
}

/******************************************************************************
** Function:	cop_stop_pulse_output
**
** Notes:		clear timers and return to idle
**				if pulse output returns output to rest state
*/
void cop_stop_pulse_output(int i)
{
	cop_pulse_timers[i].cop_pulse_width_timer_x20ms = 0;									// zero timers
	cop_pulse_timers[i].cop_pulse_interval_timer_x20ms = 0;
	cop_pulse_timers[i].cop_pulse_counter = 0;
	if ((COP_config[i].flags & COP_MASK_OUTPUT_TYPE) == 0x00)								// if pulse type
	{																						// return output to rest state
		if ((COP_config[i].flags & COP_MASK_REST_STATE) == 0x00)							// i.e. toggle if current state different to rest state
		{
			if ((COP_config[i].flags & COP_MASK_CURRENT_STATE) != 0x00)
				cop_toggle_output(i);
		}
		else
		{
			if ((COP_config[i].flags & COP_MASK_CURRENT_STATE) == 0x00)
				cop_toggle_output(i);
		}
	}
	cop_state[i] = COP_IDLE;
}

/******************************************************************************
** Function:	get control output description string from \\CONFIG\CONTROL.TXT
**
** Notes:		description strings no longer than 10 char
*/
char * cop_get_description_string(int description)
{
	CFS_read_line((char *) CFS_config_path, (char *) CFS_control_name, description, cop_filename_str, 10);
	return cop_filename_str;
}

/******************************************************************************
** Function:	log and message actions
**
** Notes:		
*/
void cop_log_and_msg(int index, int description)
{
	uint8 mask = COP_config[index].report_enable_mask;

	if (mask == 0)																					// return if nothing enabled
		return;

	if (((mask & COP_ACTION_SMS) != 0) || ((mask & COP_ACTION_FTP) != 0))							// if SMS or FTP
	{
		sprintf(STR_buffer, "dCONTROL%d=%02X%02X%02X,%02X:%02X:%02X,%s,%s",							// create message string
		index + 1,
		RTC_now.day_bcd, RTC_now.mth_bcd, RTC_now.yr_bcd,
		RTC_now.hr_bcd, RTC_now.min_bcd, RTC_now.sec_bcd,
		COM_sitename, cop_get_description_string(description));

		if (mask & COP_ACTION_SMS)																	// if SMS
		{
			ALM_send_to_all_sms_numbers();															// use ALM global function to send to all SMS numbers
		}
		if (mask & COP_ACTION_FTP)																	// if FTP
		{
			// send control message to FTP
			sprintf(cop_filename_str, "ALARM_CONTROL%05d.TXT", cop_ftp_msg_index);
			MSG_send(MSG_TYPE_FTP_MSG, STR_buffer,  cop_filename_str);
			MSG_flush_outbox_buffer(true);															// immediate tx
			FTP_schedule();																			// generate up to date outgoing FTP reports
			cop_ftp_msg_index++;
		}
	}
	if (mask & COP_ACTION_LOG)
	{																								// enqueue data for logging
		LOG_enqueue_value(LOG_CONTROL_MASK + LOG_CONTROL_1_INDEX + index, LOG_CONTROL_TYPE, (int32)description);
	}
}

/******************************************************************************
** Function:	cop_trigger_output
**
** Notes:		performs immediate control output based on set up
*/
void cop_trigger_output(int i, int description)
{
	if ((COP_config[i].flags & COP_MASK_OUTPUT_ENABLED) == 0x00)								// if disabled
	{																							// do not trigger
		cop_stop_pulse_output(i);																// stop pulse output
		if (i == 0)																				// set output to 0
			HDW_CONTROL1_ON = 0;
		else if (i == 1)
			HDW_CONTROL2_ON = 0;
		return;
	}																							// else
	cop_log_and_msg(i, description);															// report immediately
	if ((COP_config[i].flags & COP_MASK_OUTPUT_TYPE) == 0x00)									// if pulse type
	{
		if (COP_config[i].pulse_interval_msec >= COP_config[i].pulse_width_msec)				// check we have valid pulse times
		{
																								// set timers to initial values
			cop_pulse_timers[i].cop_pulse_counter = COP_config[i].number_of_pulses;
			cop_pulse_timers[i].cop_pulse_interval_timer_x20ms = (COP_config[i].pulse_interval_msec + 10) / 20;
			cop_pulse_timers[i].cop_pulse_width_timer_x20ms = (COP_config[i].pulse_width_msec + 10) / 20;
			cop_state[i] = COP_IDLE_WAIT;
		}
		else
			cop_toggle_output(i);																// else return output to former state
	}
	else																						// else immediate
		cop_toggle_output(i);																	// toggle immediately
}

/******************************************************************************
** Function:	Check if schedule time is the next required
**
** Notes:		Updates COP_wakeup_time as necessary
*/
void cop_update_wakeup_time_sec(uint32 t)
{
	if (t <= RTC_time_sec)																		// don't schedule a wakeup if it's in the past
		return;

	if (t < COP_wakeup_time)
		COP_wakeup_time = t;
}

/******************************************************************************
** Function:	set COP_wakeup_time to lowest of all cop times
**
** Notes:
*/
void cop_set_wakeup_time(void)
{
	int i;

	COP_wakeup_time = SLP_NO_WAKEUP;															// default = no wakeup
	for (i = 0; i < COP_NUM_CHANNELS; i++)
	{
		cop_update_wakeup_time_sec(cop_time[i].timed_trigger);
		cop_update_wakeup_time_sec(cop_time[i].regular_window_trigger);
		cop_update_wakeup_time_sec(cop_time[i].auto_sample);
	}
}

/******************************************************************************
** Function:	test given event gate against state of inputs
**
** Notes:
*/
bool cop_event_gate_true(uint8 gate)
{
	return true;
}

/******************************************************************************
** Function:	sample values for auto trigger for given channel
**
** Notes:		returns sample value from identified channel using #RDA technique
**				relevant channel must be enabled and sampling at the same or higher rate
**				than the auto control output trigger sample rate for best effect
**				caller must specify whether digital rates are require per second or in displayed units
*/
float cop_sample_value(int i, bool per_second)
{
	//uint8 channel_id = COP_config[i].auto_channel_id;
	//uint8 sensor_type = channel_id & 0xc0;

#ifdef HDW_RS485_
	if (sensor_type == 0x80)
		return (DOP_immediate_value(channel_id));
#else
/*
	if (sensor_type == 0x00)
	{
		return (DIG_immediate_value(channel_id, per_second));
	}
*/
#endif
//	else
		return 0;
}

/******************************************************************************
** Function:	Process value for output trigger
**
** Notes:		Called at the configured auto sample rate.
*/
void cop_process_value(int index, float value)
{
	COP_config_type * p_config = &COP_config[index];										// set up pointers
	cop_auto_values_type * p_values = &cop_auto_values[index];

	if ((p_config->threshold_trigger_mask == 0) || (LOG_state != LOG_LOGGING))				// no point if not logging or enabled
		return;
																							// First look for falling below high threshold
	if ((p_values->mask & COP_MASK_IN_HIGH_ALARM) != 0)										// if already above high threshold
	{
		if (p_config->high_threshold == FLT_MAX)											// if no threshold
			p_values->mask &= ~COP_MASK_IN_HIGH_ALARM;										// re-arm for when we have a threshold
		else if (value < p_config->high_threshold - p_config->deadband)						// else if value is below the high threshold and deadband
		{
			p_values->mask &= ~COP_MASK_IN_HIGH_ALARM;										// re-arm
			if ((p_config->threshold_trigger_mask & COP_MASK_BELOW_HIGH_ALARM) != 0x00)		// if trigger enabled
			{
				p_values->mask |= COP_MASK_EXIT_HIGH_ALARM;									// indicate state
				cop_trigger_output(index, COP_AUTO_CH);										// trigger output
			}
		}
	}
																							// Look for rising above low threshold
	if ((p_values->mask & COP_MASK_IN_LOW_ALARM) != 0)										// if already below low threshold
	{
		if (p_config->low_threshold == FLT_MAX)												// if no threshold
			p_values->mask &= ~COP_MASK_IN_LOW_ALARM;										// re-arm for when we have a threshold
		else if (value > p_config->low_threshold + p_config->deadband)						// else if value is above low threshold and deadband
		{
			p_values->mask &= ~COP_MASK_IN_LOW_ALARM;										// re-arm
			if ((p_config->threshold_trigger_mask & COP_MASK_ABOVE_LOW_ALARM) != 0x00)		// if trigger enabled
			{
				p_values->mask |= COP_MASK_EXIT_LOW_ALARM;									// indicate state
				cop_trigger_output(index, COP_AUTO_CL);										// trigger output
			}
		}
	}

																							// look for rising above high threshold
	if ((p_config->high_threshold != FLT_MAX) &&											// high limit valid
		(value >= (p_config->high_threshold + p_config->deadband)))							// high limit exceeded
	{
		if ((p_values->mask & COP_MASK_IN_HIGH_ALARM) == 0)									// not yet above threshold
		{
			if (p_values->high_debounce_count == 0)											// load up debounce timer
				p_values->high_debounce_count = (uint8)
					(LOG_interval_sec[p_config->debounce_delay] / LOG_interval_sec[p_config->auto_window.interval] + 1);
			
			if (p_values->high_debounce_count > 1)											// decrement debounce timer
				p_values->high_debounce_count--;
			else																			// debounce timer expired (1 or 0)
			{
				p_values->high_debounce_count = 0;
				p_values->mask |= COP_MASK_IN_HIGH_ALARM;
				if ((p_config->threshold_trigger_mask & COP_MASK_ABOVE_HIGH_ALARM) != 0x00)	// if trigger enabled
				{
					p_values->mask |= COP_MASK_ENTER_HIGH_ALARM;							// indicate state
					cop_trigger_output(index, COP_AUTO_EH);									// trigger output
				}
			}
		}
	}
	else																					// below high limit plus deadband
		p_values->high_debounce_count = 0;													// restart debounce timer when limit exceeded

																							// Look for falling below low threshold
	if ((p_config->low_threshold != FLT_MAX) &&												// low limit valid
		(value <= (p_config->low_threshold - p_config->deadband)))							// low limit exceeded
	{
		if ((p_values->mask & COP_MASK_IN_LOW_ALARM) == 0)									// not yet below threshold
		{
			if (p_values->low_debounce_count == 0)											// load up debounce timer
				p_values->low_debounce_count = (uint8)
					(LOG_interval_sec[p_config->debounce_delay] / LOG_interval_sec[p_config->auto_window.interval] + 1);

			if (p_values->low_debounce_count > 1)											// decrement debounce timer
				p_values->low_debounce_count--;
			else																			// debounce timer expired
			{
				p_values->low_debounce_count = 0;
				p_values->mask |= COP_MASK_IN_LOW_ALARM;
				if ((p_config->threshold_trigger_mask & COP_MASK_BELOW_LOW_ALARM) != 0x00)
				{
					p_values->mask |= COP_MASK_ENTER_LOW_ALARM;								// indicate state
					cop_trigger_output(index, COP_AUTO_EL);									// trigger output
				}
			}
		}
	}
	else																					// above low limit minus deadband
		p_values->low_debounce_count = 0;													// restart debounce timer when limit exceeded
}

/******************************************************************************
** Function:	sample values for auto trigger for given channel
**
** Notes:
*/
void cop_auto_sample(int i)
{
	float new_value;

	if ((COP_config[i].flags & COP_MASK_QUANTITY) != 0x00)										// if triggering on quantity
	{
		new_value = cop_sample_value(i, true);													//   get new value in measurement units per second
		cop_auto_values[i].quantity_value +=	
			new_value * (float)LOG_interval_sec[COP_config[i].auto_window.interval];			//   add value*interval to quantity
		if (cop_auto_values[i].quantity_value >= COP_config[i].quantity)						//   if over quantity threshold
		{
			while (cop_auto_values[i].quantity_value >= COP_config[i].quantity)
				cop_auto_values[i].quantity_value -= COP_config[i].quantity;					//     save modulus
			cop_trigger_output(i, COP_AUTO_Q);													//     trigger output
		}																						//   endif
	}
	else																						// else not quantity
	{
		new_value = cop_sample_value(i, false);													//   get new value in measurement units
		cop_process_value(i, new_value);														//   process the value for threshold crossing
	}
	cop_auto_values[i].last_value = new_value;													// save new value
}

// public functions

																									// create the control log string
/******************************************************************************
** Function:	adds letters decoded from description to STR_buffer
**
** Notes:		Returns length of string + \r\n
*/
int COP_value_to_string(uint8 description)
{
	if (description > 9)
		description = 0;

	strcat(STR_buffer, cop_descriptions[description]);
	strcat(STR_buffer, "\r\n");

	return (cop_descriptions[description][1] == '\0') ? 3 : 4;
}

/******************************************************************************
** Function:	recalculate wakeup times when time of day changes
**
** Notes:
*/
void COP_recalc_wakeups(void)
{
	int i;

	for (i = 0; i < COP_NUM_CHANNELS; i++)
	{
		cop_time[i].regular_window_trigger = COM_next_window_time(&COP_config[i].regular_window);
		cop_time[i].auto_sample = COM_next_window_time(&COP_config[i].auto_window);
	}
	cop_set_wakeup_time();
}

/******************************************************************************
** Function:	COP_configure_channel
**
** Notes:		called when receive a valid set control output command
**				clear any activity and set current state
*/
void COP_configure_channel(int i)
{
	if (i == 0)																					// if channel 1
	{
		cop_state[0] = COP_IDLE;																// clear activity
		if ((COP_config[0].flags & COP_MASK_OUTPUT_ENABLED) == 0x00)							// if disabled
			HDW_CONTROL1_ON = 0;																// set output to 0
		else
		{																						// set output and rest state to current state
			if ((COP_config[0].flags & COP_MASK_CURRENT_STATE) == 0x00)
			{
				HDW_CONTROL1_ON = 0;
				COP_config[0].flags &= ~COP_MASK_REST_STATE;
			}
			else
			{
				HDW_CONTROL1_ON = 1;
				COP_config[0].flags |= COP_MASK_REST_STATE;
			}
		}
	}
	else if (i == 1)
	{
		cop_state[1] = COP_IDLE;																// clear activity
		if ((COP_config[1].flags & COP_MASK_OUTPUT_ENABLED) == 0x00)							// if disabled
			HDW_CONTROL2_ON = 0;																// set output to 0
		else
		{																						// set output and rest state to current state
			if ((COP_config[1].flags & COP_MASK_CURRENT_STATE) == 0x00)
			{
				HDW_CONTROL2_ON = 0;
				COP_config[1].flags &= ~COP_MASK_REST_STATE;
			}
			else
			{
				HDW_CONTROL2_ON = 1;
				COP_config[1].flags |= COP_MASK_REST_STATE;
			}
		}
	}
}

/******************************************************************************
** Function:	COP_act_on_trigger_channel
**
** Notes:		called when receive a valid trigger channel command
**				immediate or delayed action, or cancel
*/
void COP_act_on_trigger_channel(int i)
{
	if ((COP_config[i].flags & COP_MASK_IMMEDIATE_TRIGGER) != 0x00)							// if immediate
	{
		COP_config[i].flags &= ~COP_MASK_TIMED_TRIGGER;										// cancel timed trigger
		cop_trigger_output(i, COP_IMMEDIATE);												// trigger output
		COP_config[i].flags &= ~COP_MASK_IMMEDIATE_TRIGGER;									// clear immediate trigger flag
	}
	else if ((COP_config[i].flags & COP_MASK_TIMED_TRIGGER) != 0x00)						// else if timed (to happen at given time)
	{
		cop_time[i].timed_trigger = RTC_bcd_time_to_sec(COP_config[i].trigger_time.hr_bcd, 
														COP_config[i].trigger_time.min_bcd, 
														0);
		cop_set_wakeup_time();																// set time to wake up from configuration
	}
	else																					// else timed trigger is cancelled
	{
		cop_time[i].timed_trigger = SLP_NO_WAKEUP;											// clear trigger time
	}
}

/******************************************************************************
** Function:	start auto sampling on receipt of #ACOx=1
**
** Notes:		
*/
void COP_start_auto(int i)
{
	cop_auto_values[i].quantity_value = 0;
}

/******************************************************************************
** Function:	COP_can_sleep
**
** Notes:		return true if so
*/
bool COP_can_sleep(void)
{
	return ((cop_state[0] == COP_IDLE) && (cop_state[1] == COP_IDLE));
}

/******************************************************************************
** Function:	COP_task
**
** Notes:		called all the time processor is awake
**				processor may have been awakened by timed wakeup relevant to COP
*/
void COP_task(void)
{
	int i;
#ifndef HDW_RS485
	int chan, sub;
#endif

	if (TIM_20ms_tick)																		// count down pulse output timers
	{
		for (i = 0; i < COP_NUM_CHANNELS; i++)
		{
			if (cop_state[i] != COP_IDLE_WAIT)												// do not count down if waiting to start
			{
				if (cop_pulse_timers[i].cop_pulse_width_timer_x20ms != 0)
					cop_pulse_timers[i].cop_pulse_width_timer_x20ms--;
				if (cop_pulse_timers[i].cop_pulse_interval_timer_x20ms != 0)
					cop_pulse_timers[i].cop_pulse_interval_timer_x20ms--;
			}
		}
	}

	if (cop_day_bcd != RTC_now.day_bcd)														// date has changed (normally midnight)
		cop_new_day_task();																	// adjusts all cop wakeup times that have gone over midnight

	// Check for timed events
	if (COP_wakeup_time <= RTC_time_sec)													// if pending scheduled event
	{
		if (!CFS_open())																	// ensure file system is open
			return;
		for (i = 0; i < COP_NUM_CHANNELS; i++)
		{
			if ((COP_config[i].flags & COP_MASK_TIMED_TRIGGER) != 0x00)						// if timed trigger required
			{
				if (cop_time[i].timed_trigger <= RTC_time_sec)								// if time to do it now or in the past
				{
					cop_trigger_output(i, COP_TIMED);										// trigger it
					cop_time[i].timed_trigger = SLP_NO_WAKEUP;								// clear trigger time
					COP_config[i].flags &= ~COP_MASK_TIMED_TRIGGER;							// clear timed trigger flag
				}
			}
			if ((COP_config[i].flags2 & COP_MASK_REGULAR_ENABLED) != 0x00)					// if regular trigger enabled
			{
				if (cop_time[i].regular_window_trigger <= RTC_time_sec)						// if time to do it now or in the past
				{
					if (cop_event_gate_true(COP_config[i].regular_event_gate))				// if event gate allows trigger
					{
						cop_trigger_output(i, COP_REGULAR);									// trigger it
					}
				}
				cop_time[i].regular_window_trigger = COM_next_window_time(&COP_config[i].regular_window);
			}
			if ((COP_config[i].flags & COP_MASK_AUTO_ENABLED) != 0x00)						// if auto trigger enabled
			{
				if (cop_time[i].auto_sample <= RTC_time_sec)								// if time to sample
				{
					if (cop_event_gate_true(COP_config[i].auto_event_gate))					// if event gate allows trigger
					{
						cop_auto_sample(i);													// do sample
					}
				}
				cop_time[i].auto_sample = COM_next_window_time(&COP_config[i].auto_window);
			}
		}
		cop_set_wakeup_time();
	}

#ifndef HDW_RS485
	// check for interrupt events
	for (i = 0; i < COP_NUM_CHANNELS; i++)
	{
		if ((COP_config[i].flags2 & COP_MASK_EVENT_ENABLED) != 0x00)						// if event trigger enabled
		{
			chan = COP_config[i].event_channel_id / 2;
			sub = COP_config[i].event_channel_id % 2;

			if (DIG_channel[chan].sub[sub].event_flag != 0)									// if sub channel event has happened
			{
				if (!CFS_open())															// ensure file system is open
					return;
				cop_trigger_output(i, COP_EVENT);											// trigger event output
				DIG_channel[chan].sub[sub].event_flag = 0;									// clear sub channel event flag
			}
		}
	}
#endif

	switch(cop_state[0])																	// state machine for output 1
	{
	case COP_IDLE:
		break;																				// can sleep in this state

	case COP_IDLE_WAIT:																		// stays awake in this state
																							// wait for dig, ana, log and pdu to finish and file system open
		if (!LOG_busy() && !PDU_busy() && CFS_open())
		{
			cop_toggle_output(0);															// toggle output for start of pulse train
			cop_state[0] = COP_MARK;														// enter mark state
		}
		break;

	case COP_MARK:
		if (cop_pulse_timers[0].cop_pulse_width_timer_x20ms == 0)							// else if end of pulse width
		{
			cop_toggle_output(0);															// toggle output
			cop_state[0] = COP_SPACE;														// enter space state
		}
		break;

	case COP_SPACE:
		if (cop_pulse_timers[0].cop_pulse_interval_timer_x20ms == 0)						// else if end of pulse interval
		{
			cop_pulse_timers[0].cop_pulse_counter--;										// decrement pulse count
			if (cop_pulse_timers[0].cop_pulse_counter == 0)									// if pulse count is zero
			{
				cop_stop_pulse_output(0);													// stop output
			}
			else
			{
				cop_toggle_output(0);														// toggle output
																							// restart width and interval timers
				cop_pulse_timers[0].cop_pulse_interval_timer_x20ms = (COP_config[0].pulse_interval_msec + 10) / 20;
				cop_pulse_timers[0].cop_pulse_width_timer_x20ms = (COP_config[0].pulse_width_msec + 10) / 20;
				cop_state[0] = COP_MARK;													// enter mark state
			}
		}
		break;

	default:
		cop_stop_pulse_output(0);
		break;
	}

	switch(cop_state[1])																	// state machine for output 2
	{
	case COP_IDLE:
		break;																				// can sleep in this state

	case COP_IDLE_WAIT:																		// stays awake in this state
																							// wait for dig, ana, log and pdu to finish and file system open
		if (!LOG_busy() && !PDU_busy() && CFS_open())
		{
			cop_toggle_output(1);															// toggle output for start of pulse train
			cop_state[1] = COP_MARK;														// enter mark state
		}
		break;

	case COP_MARK:
		if (cop_pulse_timers[1].cop_pulse_width_timer_x20ms == 0)							// else if end of pulse width
		{
			cop_toggle_output(1);															// toggle output
			cop_state[1] = COP_SPACE;														// enter space state
		}
		break;

	case COP_SPACE:
		if (cop_pulse_timers[1].cop_pulse_interval_timer_x20ms == 0)						// else if end of pulse interval
		{
			cop_pulse_timers[1].cop_pulse_counter--;										// decrement pulse count
			if (cop_pulse_timers[1].cop_pulse_counter == 0)									// if pulse count is zero
			{
				cop_stop_pulse_output(1);													// stop output
			}
			else
			{
				cop_toggle_output(1);														// toggle output
																							// restart width and interval timers
				cop_pulse_timers[1].cop_pulse_interval_timer_x20ms = (COP_config[1].pulse_interval_msec + 10) / 20;
				cop_pulse_timers[1].cop_pulse_width_timer_x20ms = (COP_config[1].pulse_width_msec + 10) / 20;
				cop_state[1] = COP_MARK;													// enter mark state
			}
		}
		break;

	default:
		cop_stop_pulse_output(1);
		break;
	}
}

/******************************************************************************
** Function:	COP_init
**
** Notes:		set defaults
*/
void COP_init(void)
{
	int i;

	for (i=0; i<2; i++)
	{
		cop_state[i] = COP_IDLE;
		COP_config[i].flags = _B00000010;													// all disabled, changeover, low output
		COP_config[i].report_enable_mask = _B00000000;										// no logging or reports
		COP_config[i].pulse_width_msec = 0;													// no duration or repetition
		COP_config[i].pulse_interval_msec = 0;
		COP_config[i].number_of_pulses = 0;
		COP_config[i].auto_channel_id = 0;
		COP_config[i].auto_event_gate = 0;
		COP_config[i].auto_window.start.min_bcd = 0x00;
		COP_config[i].auto_window.start.hr_bcd = 0x00;
		COP_config[i].auto_window.stop.min_bcd = 0x00;
		COP_config[i].auto_window.stop.hr_bcd = 0x00;
		COP_config[i].auto_window.day_mask = 0x7F;											// day mask enabled - may be introduced later TBD
		COP_config[i].auto_window.interval = 0;
		COP_config[i].quantity = 0;
		COP_config[i].high_threshold = 0;
		COP_config[i].low_threshold = 0;
		COP_config[i].deadband = 0;
		COP_config[i].debounce_delay = 0;
		COP_config[i].threshold_trigger_mask = 0;
		COP_config[i].regular_event_gate = 0;
		COP_config[i].trigger_time.min_bcd = 0x00;
		COP_config[i].trigger_time.hr_bcd = 0x00;
		COP_config[i].regular_window.start.min_bcd = 0x00;
		COP_config[i].regular_window.start.hr_bcd = 0x00;
		COP_config[i].regular_window.stop.min_bcd = 0x00;
		COP_config[i].regular_window.stop.hr_bcd = 0x00;
		COP_config[i].regular_window.day_mask = 0x7F;										// day mask enabled - may be introduced later TBD
		COP_config[i].regular_window.interval = 0;

		cop_time[i].timed_trigger = SLP_NO_WAKEUP;
		cop_time[i].regular_window_trigger = SLP_NO_WAKEUP;
		cop_time[i].auto_sample = SLP_NO_WAKEUP;
	}

	cop_ftp_msg_index = 0;
	cop_day_bcd = RTC_now.day_bcd;

	HDW_CONTROL1_ON = 0;																	// set outputs
	HDW_CONTROL2_ON = 0;
}





