/******************************************************************************
** File:	Dig.c
**
** Notes:	Digital channels
**
** v2.49 250111 PB channel start calls PDU_synchronise_batch() and channel stop calls PDU_disable_batch()
**
** v2.59 010311 PB DEL63  new function DIG_insert_event_headers() for command #EC to call, to create new EVENT header
**       020311 PB DEL126 A) avoid needless wakeups if event logging:
**                        1) in dig_start_stop_channel() set channel sample time to max initially - it will be set later if necc
**                        2) in DIG_synchronise() remove setting of channel sample time - it calls start/stop
**                        B) don't pulse log an event sub-channel when the other sub-channel is pulse logging:
**                        1) in dig_log_pulse_counts(), dig_log_sms_data(), dig_update_min_max() & dig_update_alarm() do not log an event subchannel
**
** v2.62 110311 PB DEL63  correction to function DIG_insert_event_headers() is per SUBCHANNEL
**       160311 PB        only need to set next interval if pulse counting on subchannel A
**                        will be correct automatically for subchannel B if used
**
** v2.69 270411 PB DEL134 - in DIG_configure_channel() set new SNS_write_ff_width flag(s)
**  
** V2.82 280611 PB DEL132 - correction to totaliser update to use separate tcal values for A and B channels
** V2.83 270711 PB          HDW_CH1_POWER_OUT_ON and HDW_CH2_POWER_OUT_ON are the same sense in 3 or 9 channel logger v2.2 hardware
** V2.90 010811 PB          merge of version 2.82 and 2.83
**
** V3.00 210911 PB Waste Water - made digital input definitions global
**       051011                  add functions for logging and calculating values derived from events
**
** V3.03 201111 PB               add dummy function for DIG_print_immediate_derived_values()
**
** V3.04 221211					 compiler switch off if RS485 to save or reuse memory
**       100112                  code to cope with derived volume from normal flow
**
** V3.12 060612 PB				 New function dig_get_immediate_pump_flow() used to get immediate flow from pump rate table
**								 used for #IMV and #RDA immediate current clamp pump flows
**								 Use tcal_x10000 from totaliser to calculate volume pumped per current clamp pump 
**								 in dig_update_pump_volume_in_interval() & dig_update_system_volume_in_interval() 
**								 Use rate_enumeration supplied by #CEC when calculating average flow in event driven logged data
**
** V3.13 130612 PB				 need to divide pump flow by tcal to dconvert it back to flow units * time: dig_calc_log_pump_rate() & dig_calc_log_system_rate()
**
** V3.22 280513 PB				DEL175: only enqueue footers for pulse counting in dig_channel_midnight_task()
**								DEL180: in DIG_print_immediate_values() add event enabled test to event type > 0 test for event value printing
**		 290513 PB				DEL181: add dig_first_in_system() to find first enabled pump in a system for printing immediate values
**		 310513 PB				no longer any need for DIG_insert_event_headers() - done in dig_start_stop_logging()
**
** V3.24 030613 PB				Change method of calculating system pump flow rates and volumes - corrects methods defined on 060612 V3.12:
**								PFR is in units per second and defines units for flow rate logging - i.e. DCC units enum must match PFR
**								logged flow rate is modified by #CEC rate_enumeration
**								tcalx10000 is used only in volume logging (rate_enum = 0) and totalising (metering)
**								Rewrite event value logging to do this correction - keep everything in PFR units.
**								Use tcalx10000 when a volume is required to be displayed, logged or compared
**
** V3.28 030713 PB				DEL193: In dig_check_event_debounce() if both edge triggering - update expected interrupt edge from present state of input line
**								as we may have had a change of state during debounce time
**
** V3.29 151013 PB				DEL202: correction to dig_get_immediate_pump_flow() - move zero flow returned if pump off inside individual test
**								DEL203: in dig_check_event_log() move 'footer at midnight' test inside type tests to prevent spurious footers in pump system logs
**								DEL206: correction to DIG_immediate_value() to return units per second in all cases of rate enumeration
**
** V3.36 140114 PB  			remove calls to FTP_deactivate_retrieval_info()
**
** V4.02 100414 PB				correction to dig_check_event_alarm() - need to zero normal_alarm_amount, not normal_alarm_count
**
** V4.10 040614 PB				in channel task only set up event times if event sensor enabled and valid
**
*/

#include <math.h>
#include <float.h>

#include "custom.h"
#include "compiler.h"
#include "str.h"
#include "HardwareProfile.h"
#include "Sns.h"
#include "Rtc.h"
#include "Log.h"
#include "Tim.h"
#include "Cal.h"
#include "Pdu.h"
#include "Alm.h"
#include "Slp.h"
#include "Msg.h"
#include "Com.h"
#include "MDD File System/FSIO.h"
#include "Cfs.h"
#include "ftp.h"
#include "cop.h"
#include "gps.h"

#define extern
#include "dig.h"
#undef extern

#ifndef HDW_RS485

// Channel states
#define DIG_OFF			0
#define DIG_INIT		1
#define DIG_ON			2
#define DIG_SAMPLING	3

// Bit masks in dig_start_flags
#define DIG_START_MASK_LOG		_B00000001
#define DIG_START_MASK_SMS		_B00000010
#define DIG_START_MASK_MIN_MAX	_B00000100

// Bit masks in dig_sub_start_flags
#define DIG_START_MASK_E_LOG	_B00000001
#define DIG_START_MASK_E_SMS	_B00000010
#define DIGSTART_MASK_E_MIN_MAX	_B00000100
#define DIG_START_MASK_NORM_ALM	_B00001000
#define DIG_START_MASK_DRVD_ALM	_B00010000

#define DIG_START_MASK_ALL_LOG	_B00000111
#define DIG_START_MASK_ALL_SUB	_B00011111

#define DIG_EVENT_HIGH_MASK 0x08000000

#define DIG_SYSTEM_LOG_ONE_SHOT			_B00000001								// Bit positions in dig_system_interval_flags
#define DIG_SYSTEM_SMS_ONE_SHOT			_B00000010
#define DIG_SYSTEM_MINMAX_ONE_SHOT		_B00000100
#define DIG_SYSTEM_ALM_ONE_SHOT			_B00001000
#define DIG_ALL_SYSTEM_ONE_SHOT			_B00001111

// Hassle reduction variables:
int               dig_index;
DIG_channel_type *dig_p_channel;
DIG_config_type  *dig_p_config;
uint8			  dig_system_interval_flags;

#ifdef HDW_GPS
bool dig_gps_triggered;
#endif

typedef struct
{
	uint8	start;
	uint8	sub_start[2];
}
dig_start_flag_type;

dig_start_flag_type dig_start_flags[DIG_NUM_CHANNELS];							// start flag registers to prevent incomplete log after channel start

// Default flash fire periods
// Flash fire frequency = 7812.5 / (period + 1) Hz. Frequencies:
//	64,		128,	260,	521,	781,	1116,	2604,	3906
const uint8 dig_default_ff_period[8] =
{
	121,	60,		29,		14,		9,		6,		2,		1
};

/******************************************************************************
** Function:	Initialise digital channel
**
** Notes:	
*/
void DIG_init(void)
{
	int i, j;

	for (i = 0; i < DIG_NUM_CHANNELS; i++)
	{
		DIG_config[i].ff_pulse_width_x32us = 2;
		memcpy(DIG_config[i].ff_period, dig_default_ff_period, sizeof(DIG_config[i].ff_period));
		DIG_config[i].flags = 0;
		DIG_config[i].sensor_type = 0;
		DIG_config[i].log_interval = 0;
		DIG_config[i].min_max_sample_interval = 0;
		DIG_config[i].sms_data_interval = 0;
		DIG_config[i].rate_enumeration = 1;
		DIG_channel[i].sample_time = SLP_NO_WAKEUP;
		DIG_channel[i].log_time = SLP_NO_WAKEUP;
		DIG_channel[i].sms_time = SLP_NO_WAKEUP;
		DIG_channel[i].min_max_sample_time = SLP_NO_WAKEUP;
		for (j = 0; j < 2; j++)
		{
			DIG_config[i].ec[j].flags = 0;
			DIG_config[i].ec[j].sensor_type = 0;
			DIG_config[i].ec[j].log_interval = 0;
			DIG_config[i].ec[j].min_max_sample_interval = 0;
			DIG_config[i].ec[j].sms_data_interval = 0;
			DIG_config[i].ec[j].rate_enumeration = 3;
			DIG_channel[i].sub[j].event_flag = 0;
			DIG_channel[i].sub[j].normal_alarm_time = SLP_NO_WAKEUP;
			DIG_channel[i].sub[j].derived_alarm_time = SLP_NO_WAKEUP;
			DIG_channel[i].sub[j].event_time = SLP_NO_WAKEUP;
			DIG_channel[i].sub[j].event_log_time = SLP_NO_WAKEUP;
			DIG_channel[i].sub[j].event_sms_time = SLP_NO_WAKEUP;
			DIG_channel[i].sub[j].event_min_max_sample_time = SLP_NO_WAKEUP;
		}
	}
}

/******************************************************************************
** Function:	Combine flows according to sensor type
**
** Notes:
*/
float DIG_combine_flows(uint8 sensor_type, float a, float b)
{
	switch (sensor_type & 0x07)
	{
	case DIG_TYPE_FWD_A:
		return a;

	case DIG_TYPE_FWD_A_FWD_B:
		return a + b;

	case DIG_TYPE_BOTH_A_REV_B:
		return a - (b * 2);

	case DIG_TYPE_DIR_B_HIGH_FWD:
		return b - a;
	}

	// default cases: DIG_TYPE_FWD_A_REV_B and DIG_TYPE_DIR_B_HIGH_REV:
	return a - b;
}

/******************************************************************************
** Function:	Convert a volume to a flow rate according to given rate enumeration
**
** Notes:		Rate enumeration: 0 => leave as total, 1 => per second,
**				2 => per minute, 3=> per hour
*/
float DIG_volume_to_rate_enum(float value, uint8 time_enumeration, uint8 rate_enumeration)
{
	if (rate_enumeration == 0)								// leave as total
		return value;

	value /= (float)LOG_interval_sec[time_enumeration];		// get rate per second
	if (rate_enumeration == 2)
		value *= 60.0;										// get rate per minute
	else if (rate_enumeration == 3)
		value *= 3600.0;									// get rate per hour

	return value;
}

/******************************************************************************
** Function:	Log a value in a LOG file
**
** Notes:		given channel, subchannel, header timestamp and value
*/
void dig_log_value(int index, int sub, int32 time, float value)
{
	if ((LOG_header_mask & (1 << (1 + sub + (2 * index)))) == 0)										// if needs header
	{
		if (LOG_enqueue_value(1 + sub + (2 * index), LOG_BLOCK_HEADER_TIMESTAMP, time))					// enqueue it
			LOG_header_mask |= 1 << (1 + sub + (2 * index));
	}
	LOG_enqueue_value(1 + (2 * index) + sub, LOG_DATA_VALUE, *(uint32 *)&value);						// enqueue value
}

/******************************************************************************
** Function:	Log a derived value in a LOG file
**
** Notes:		given channel, subchannel, header timestamp and value
*/
void dig_log_derived_value(int index, int sub, int32 time, float value)
{
	if ((LOG_derived_header_mask & (1 << (1 + sub + (2 * index)))) == 0)										// if needs header
	{
		if (LOG_enqueue_value((1 + sub + (2 * index)) | LOG_DERIVED_MASK, LOG_BLOCK_HEADER_TIMESTAMP, time))	// enqueue it
			LOG_derived_header_mask |= 1 << (1 + sub + (2 * index));
	}
	LOG_enqueue_value((1 + (2 * index) + sub) | LOG_DERIVED_MASK, LOG_DATA_VALUE, *(uint32 *)&value);			// enqueue value
}

/******************************************************************************
** Function:	Check if pulse count logging is due, & if so, do it
**
** Notes:		Derived volume logging added Waste Water V3.04
*/
void dig_log_pulse_counts(void)
{
	float value;
	RTC_type time_stamp;
	int i;

	if (dig_p_channel->log_time != dig_p_channel->sample_time)											// get out if not time yet
		return;

	if ((dig_start_flags[dig_index].start & DIG_START_MASK_LOG) != 0)									// else check start flag for this channel
	{
		if ((dig_p_config->sensor_type & DIG_PULSE_MASK) != 0)											// if either channel pulse logging
		{
			dig_start_flags[dig_index].start &= ~DIG_START_MASK_LOG;									// if set clear it and do not log 
			LOG_set_next_time(&dig_p_channel->log_time, dig_p_config->log_interval, true);				// (still need to clear counters and set next time)
		}
	}
	else
	{																									// sub-channel A
		if ((dig_p_config->sensor_type & DIG_EVENT_A_MASK) == 0)										// only log if not an event subchannel
		{
			value = dig_p_channel->sub[0].log_count * dig_p_config->fcal_a;								// get counts * fcal A
			if ((dig_p_config->flags & DIG_MASK_COMBINE_SUB_CHANNELS) != 0)								// Combine pulse streams if necessary
				value = DIG_combine_flows(dig_p_config->sensor_type, value,
										  dig_p_channel->sub[1].log_count * dig_p_config->fcal_b);
			value = DIG_volume_to_rate_enum(value, dig_p_config->log_interval, dig_p_config->rate_enumeration);
																										// log the value and header if called for
			time_stamp.reg32[0] = LOG_get_timestamp(dig_p_channel->sample_time, dig_p_config->log_interval);
			dig_log_value(dig_index, 0, time_stamp.reg32[0], value);

			// see if time to log meter reading (if not midnight & time now is a multiple of totaliser interval):
			i = LOG_interval_sec[dig_p_channel->sub[0].totaliser.log_interval_enumeration];
			if ((i != 0) && ((dig_p_channel->log_time % i) == 0))
				LOG_enqueue_value(1 + (2 * dig_index), LOG_TOTALISER_TIMESTAMP, dig_p_channel->log_time);

			time_stamp.reg32[1] = RTC_now.reg32[1];														// already got time_stamp.reg32[0]
			FTP_update_retrieval_info(2 * dig_index, &time_stamp);										// normal data to be sent
			if ((dig_p_config->flags & DIG_MASK_DERIVED_VOL_ENABLED) != 0x00)							// check if deriving volume
			{																							// log volume for subchannel A or combined channels
				dig_log_derived_value(dig_index, 0, (int32)LOG_get_timestamp(dig_p_channel->sample_time, 
									  dig_p_config->log_interval), dig_p_channel->sub[0].derived_volume);
				dig_p_channel->sub[0].last_derived_volume = dig_p_channel->sub[0].derived_volume;		// save for use by IMV and RDA
				dig_p_channel->sub[0].derived_volume = 0;												// clear it ready for next logging interval
				FTP_update_retrieval_info((2 * dig_index) + FTP_DERIVED_DIG_INDEX, &time_stamp);		// derived data to be sent
			}
			LOG_set_next_time(&dig_p_channel->log_time, dig_p_config->log_interval, true);
		}
	
																										// sub-channel B
		if ((dig_p_config->sensor_type & DIG_EVENT_B_MASK) == 0)										// only log if not an event subchannel
		{
			if (((dig_p_config->flags & DIG_MASK_COMBINE_SUB_CHANNELS) == 0) &&							// enqueue sub-channel B if necessary:
				((dig_p_config->sensor_type & DIG_PULSE_MASK) > DIG_TYPE_FWD_A))
			{
				value = dig_p_channel->sub[1].log_count * dig_p_config->fcal_b;
				value = DIG_volume_to_rate_enum(value, dig_p_config->log_interval, dig_p_config->rate_enumeration);
																										// log the value and header if called for
				dig_log_value(dig_index, 1, (int32)LOG_get_timestamp(dig_p_channel->sample_time, dig_p_config->log_interval), value);

				// see if time to log meter reading (if not midnight & time now is a multiple of totaliser interval):
				i = LOG_interval_sec[dig_p_channel->sub[1].totaliser.log_interval_enumeration];
				if ((i != 0) && ((dig_p_channel->log_time % i) == 0))
					LOG_enqueue_value(1 + (2 * dig_index) + 1, LOG_TOTALISER_TIMESTAMP, dig_p_channel->log_time);

				FTP_update_retrieval_info((2 * dig_index) + 1, &time_stamp);							// normal data to be sent
				if ((dig_p_config->flags & DIG_MASK_DERIVED_VOL_ENABLED) != 0x00)						// check if deriving volume
				{																						// log volume for sub channel B
					dig_log_derived_value(dig_index, 1, (int32)LOG_get_timestamp(dig_p_channel->sample_time, 
										  dig_p_config->log_interval), dig_p_channel->sub[1].derived_volume);
					dig_p_channel->sub[1].last_derived_volume = dig_p_channel->sub[1].derived_volume;	// save for use by IMV and RDA
					dig_p_channel->sub[1].derived_volume = 0;											// clear it ready for next logging interval
					FTP_update_retrieval_info((2 * dig_index) + 1 + FTP_DERIVED_DIG_INDEX, &time_stamp);// derived data to be sent
				}
			}
		}
	}

	dig_p_channel->sub[0].log_count = 0;																// For all types of sensor, having logged the counters, clear them:
	dig_p_channel->sub[1].log_count = 0;
}

/******************************************************************************
** Function:	Log a value in an SMS file
**
** Notes:		given channel, subchannel, header timestamp and value
*/
void dig_log_sms_value(int index, int sub, int32 time, float value)
{
	if ((LOG_sms_header_mask & (1 << (1 + sub + (2 * index)))) == 0)										// if needs header
	{
		if (LOG_enqueue_value((1 + sub + (2 * index)) | LOG_SMS_MASK, LOG_BLOCK_HEADER_TIMESTAMP, time))	// enqueue it
			LOG_sms_header_mask |= 1 << (1 + sub + (2 * index));
	}
	LOG_enqueue_value((1 + (2 * index) + sub) | LOG_SMS_MASK, LOG_DATA_VALUE, *(uint32 *)&value);			// enqueue value
}

/******************************************************************************
** Function:	Log a derived value in an SMS file
**
** Notes:		given channel, subchannel, header timestamp and value
*/
void dig_log_derived_sms_value(int index, int sub, int32 time, float value)
{
	if ((LOG_derived_sms_header_mask & (1 << (1 + sub + (2 * index)))) == 0)												// if needs header
	{
		if (LOG_enqueue_value((1 + sub + (2 * index)) | LOG_SMS_MASK | LOG_DERIVED_MASK, LOG_BLOCK_HEADER_TIMESTAMP, time))	// enqueue it
			LOG_derived_sms_header_mask |= 1 << (1 + sub + (2 * index));
	}
	LOG_enqueue_value((1 + (2 * index) + sub) | LOG_SMS_MASK | LOG_DERIVED_MASK, LOG_DATA_VALUE, *(uint32 *)&value);		// enqueue value
}

/******************************************************************************
** Function:	Check if SMS data logging is due, & if so do it.
**
** Notes:	
*/
void dig_log_sms_data(void)
{
	float value;

	if (dig_p_config->sms_data_interval == 0)																// get out if no SMS logging
		return;

	// NB never wakes up for sms_time, only for sample_time
	if (dig_p_channel->sms_time != dig_p_channel->sample_time)												// get out if not time yet
		return;

	if ((dig_start_flags[dig_index].start & DIG_START_MASK_SMS) != 0)										// else check start flag for this channel
	{
		if ((dig_p_config->sensor_type & DIG_PULSE_MASK) != 0)												// if either channel pulse logging
		{
			dig_start_flags[dig_index].start &= ~DIG_START_MASK_SMS;										// if set clear it and do not log sms 
			LOG_set_next_time(&dig_p_channel->sms_time, dig_p_config->sms_data_interval, true);				// (still need to clear counters and set next time)
		}
	}
	else
	{
		// sub-channel A
		if ((dig_p_config->sensor_type & DIG_EVENT_A_MASK) == 0)											// only log if not an event subchannel
		{
			value = dig_p_channel->sub[0].sms_count * dig_p_config->fcal_a;									// Do sub-channel A, which may be A & B combined:
			if ((dig_p_config->flags & DIG_MASK_COMBINE_SUB_CHANNELS) != 0)									// Combine pulse streams if necessary
				value = DIG_combine_flows(dig_p_config->sensor_type, value,
										  dig_p_channel->sub[1].sms_count * dig_p_config->fcal_b);

			value = DIG_volume_to_rate_enum(value, dig_p_config->sms_data_interval, dig_p_config->rate_enumeration);
																											// log the value and header if called for
			dig_log_sms_value(dig_index, 0, (int32)LOG_get_timestamp(dig_p_channel->sample_time, dig_p_config->sms_data_interval), value);
																											// check for batch mode synchronisation
			if (PDU_time_for_batch(2 * dig_index))															// check for PDU batch mode
				PDU_schedule(2 * dig_index);
			if ((dig_p_config->flags & DIG_MASK_DERIVED_VOL_ENABLED) != 0x00)								// check if deriving volume
			{																								// log volume for subchannel A or combined channels
				dig_log_derived_sms_value(dig_index, 0, (int32)LOG_get_timestamp(dig_p_channel->sample_time, 
										  dig_p_config->log_interval), dig_p_channel->sub[0].derived_sms_volume);
				dig_p_channel->sub[0].derived_sms_volume = 0;												// clear it ready for next sms interval
			}
			LOG_set_next_time(&dig_p_channel->sms_time, dig_p_config->sms_data_interval, true);				// set next sms time
		}

																											// sub-channel B
		if ((dig_p_config->sensor_type & DIG_EVENT_B_MASK) == 0)											// only log if not an event subchannel
		{
			if (((dig_p_config->flags & DIG_MASK_COMBINE_SUB_CHANNELS) == 0) &&								// enqueue sub-channel B if necessary:
				((dig_p_config->sensor_type & DIG_PULSE_MASK) > DIG_TYPE_FWD_A))
			{
				value = DIG_volume_to_rate_enum(dig_p_channel->sub[1].sms_count * dig_p_config->fcal_b,		// Do sub-channel B:
											    dig_p_config->sms_data_interval, dig_p_config->rate_enumeration);
	
																											// log the value and header if called for
				dig_log_sms_value(dig_index, 1, (int32)LOG_get_timestamp(dig_p_channel->sample_time, dig_p_config->sms_data_interval), value);
																											// check for batch mode synchronisation
				if (PDU_time_for_batch((2 * dig_index) + 1))
					PDU_schedule((2 * dig_index) + 1);
				if ((dig_p_config->flags & DIG_MASK_DERIVED_VOL_ENABLED) != 0x00)							// check if deriving volume
				{																							// log volume for sub channel B
					dig_log_derived_sms_value(dig_index, 1, (int32)LOG_get_timestamp(dig_p_channel->sample_time, 
											  dig_p_config->log_interval), dig_p_channel->sub[1].derived_sms_volume);
					dig_p_channel->sub[1].derived_sms_volume = 0;											// clear it ready for next sms interval
				}
			}
		}
	}

	// for all type of sensor, having logged the counters, clear them:
	dig_p_channel->sub[0].sms_count = 0;
	dig_p_channel->sub[1].sms_count = 0;
}

/******************************************************************************
** Function:	Check for new derived min/max on a sub-channel, & record if so
**
** Notes:		given pointer to sub channel structure
*/
void dig_check_derived_min_max(DIG_sub_channel_type *p)
{
	if (LOG_state != LOG_LOGGING)
		return;

	if (p->derived_min_max_sample < p->min_derived_value)		// new min
	{
		p->min_derived_value = p->derived_min_max_sample;
		p->min_derived_time.hr_bcd = RTC_now.hr_bcd;
		p->min_derived_time.min_bcd = RTC_now.min_bcd;
		p->min_derived_time.sec_bcd = RTC_now.sec_bcd;
	}

	if (p->derived_min_max_sample > p->max_derived_value)		// new max
	{
		p->max_derived_value = p->derived_min_max_sample;		// Typo bug fixed here MA 27/03/15
		p->max_derived_time.hr_bcd = RTC_now.hr_bcd;
		p->max_derived_time.min_bcd = RTC_now.min_bcd;
		p->max_derived_time.sec_bcd = RTC_now.sec_bcd;
	}
}

/******************************************************************************
** Function:	Check for new min/max on an event logging sub-channel, & record if so
**
** Notes:	
*/
void dig_compare_min_max(DIG_sub_channel_type *p, uint8 sample_interval, uint8 rate_enum)
{
	float sample;

	if (LOG_state != LOG_LOGGING)
		return;

	sample = DIG_volume_to_rate_enum(p->min_max_sample, sample_interval, rate_enum);
	if (sample < p->min_value)					// new min
	{
		p->min_value = sample;
		p->min_time.hr_bcd = RTC_now.hr_bcd;
		p->min_time.min_bcd = RTC_now.min_bcd;
		p->min_time.sec_bcd = RTC_now.sec_bcd;
	}

	if (sample > p->max_value)					// new max
	{
		p->max_value = sample;
		p->max_time.hr_bcd = RTC_now.hr_bcd;
		p->max_time.min_bcd = RTC_now.min_bcd;
		p->max_time.sec_bcd = RTC_now.sec_bcd;
	}
}

/******************************************************************************
** Function:	Update min/max sample, & record new values if it's time to do so
**
** Notes:	
*/
void dig_update_min_max(void)
{
	float value;
																							// Accumulate flow volume into min_max_sample
																							// sub-channel A
	if ((dig_p_config->sensor_type & DIG_EVENT_A_MASK) == 0)								// only do it if not an event subchannel
	{
		value = dig_p_channel->sub[0].sample_count * dig_p_config->fcal_a;					// Do sub-channel A, which may be A & B combined:
		if ((dig_p_config->flags & DIG_MASK_COMBINE_SUB_CHANNELS) != 0)
			value = DIG_combine_flows(dig_p_config->sensor_type, value, dig_p_channel->sub[1].sample_count * dig_p_config->fcal_b);
		dig_p_channel->sub[0].min_max_sample += value;
	}
																							// sub-channel B
	if ((dig_p_config->sensor_type & DIG_EVENT_B_MASK) == 0)								// only do it if not an event subchannel
	{
																							// Do sub-channel B if necessary:
		if (((dig_p_config->flags & DIG_MASK_COMBINE_SUB_CHANNELS) == 0) && ((dig_p_config->sensor_type & DIG_PULSE_MASK) > DIG_TYPE_FWD_A))
			dig_p_channel->sub[1].min_max_sample += dig_p_channel->sub[1].sample_count * dig_p_config->fcal_b;
	}
	if ((dig_p_config->sensor_type & DIG_PULSE_MASK) != 0)									// if either channel pulse logging
	{
		if (dig_p_channel->min_max_sample_time == dig_p_channel->sample_time)
		{
			if ((dig_start_flags[dig_index].start & DIG_START_MASK_MIN_MAX) != 0)			// check start flag for this sub-channel
				dig_start_flags[dig_index].start &= ~DIG_START_MASK_MIN_MAX;				// if set clear it and do not calc min/max (still need to clear counters and set next time)
			else
			{
				dig_compare_min_max(&(dig_p_channel->sub[0]), dig_p_config->min_max_sample_interval, dig_p_config->rate_enumeration);
				if ((dig_p_config->flags & DIG_MASK_DERIVED_VOL_ENABLED) != 0x00)			// check if deriving volume
				{																			// check min and max for sub channel A
					dig_check_derived_min_max(&(dig_p_channel->sub[0]));
					dig_p_channel->sub[0].derived_min_max_sample = 0.0f;
				}
				if (((dig_p_config->flags & DIG_MASK_COMBINE_SUB_CHANNELS) == 0) &&
					((dig_p_config->sensor_type & DIG_PULSE_MASK) > DIG_TYPE_FWD_A))
				{
					dig_compare_min_max(&(dig_p_channel->sub[1]), dig_p_config->min_max_sample_interval, dig_p_config->rate_enumeration);
					if ((dig_p_config->flags & DIG_MASK_DERIVED_VOL_ENABLED) != 0x00)		// check if deriving volume
					{																		// check min and max for sub channel B
						dig_check_derived_min_max(&(dig_p_channel->sub[1]));
						dig_p_channel->sub[1].derived_min_max_sample = 0.0f;
					}
				}
			}	
			dig_p_channel->sub[0].min_max_sample = 0.0f;									// clear min/max flow accumulator
			dig_p_channel->sub[1].min_max_sample = 0.0f;									// clear min/max flow accumulator
			LOG_set_next_time(&dig_p_channel->min_max_sample_time, dig_p_config->min_max_sample_interval, true);
		}
	}
}

/******************************************************************************
** Function:	Check if normal alarm sampling is due & if so do it
**
** Notes:	
*/
void dig_update_normal_alarm(void)
{
	float f;
																								// ALM_config[0] = 1A, [1] = 1B, [2] = 2A, [3] = 2B, 
																								// dig_index = 0 or 1 for D1x or D2x
																								// sub-channel A
	if ((dig_p_config->sensor_type & DIG_EVENT_A_MASK) == 0)									// only do it if not an event subchannel
	{
		if (ALM_config[dig_index * 2].enabled)
		{
			if (dig_p_channel->sub[0].normal_alarm_time == SLP_NO_WAKEUP)						// if first time - alarm time set to sleep but alarm enabled
			{
				dig_p_channel->sub[0].normal_alarm_time = dig_p_channel->sample_time;
				LOG_set_next_time(&dig_p_channel->sub[0].normal_alarm_time, ALM_config[dig_index * 2].sample_interval, true);
				dig_p_channel->sub[0].normal_alarm_count = 0;
			}
			if (dig_p_channel->sub[0].normal_alarm_time == dig_p_channel->sample_time)			// Do sub-channel A, which may be A & B combined:
			{
				// check start flag for this sub-channel
				if ((dig_start_flags[dig_index].sub_start[0] & DIG_START_MASK_NORM_ALM) != 0)
				{
					dig_start_flags[dig_index].sub_start[0] &= ~DIG_START_MASK_NORM_ALM;		// if set clear it and do not calc alarm (still need to clear counters and set next time)
					if ((dig_p_config->flags & DIG_MASK_COMBINE_SUB_CHANNELS) != 0)
						dig_p_channel->sub[1].normal_alarm_count = 0;							// clear combined b count
				}
				else
				{
					f = dig_p_channel->sub[0].normal_alarm_count * dig_p_config->fcal_a;		// Combine pulse streams if necessary
					if ((dig_p_config->flags & DIG_MASK_COMBINE_SUB_CHANNELS) != 0)
					{
						f = DIG_combine_flows(dig_p_config->sensor_type, f,
											  dig_p_channel->sub[1].normal_alarm_count * dig_p_config->fcal_b);
						dig_p_channel->sub[1].normal_alarm_count = 0;							// having used the value, clear it
					}
		
					f = DIG_volume_to_rate_enum(f, ALM_config[dig_index * 2].sample_interval, dig_p_config->rate_enumeration);
																								// 2 * dig_index = LOG_DIGITAL_1A_INDEX or LOG_DIGITAL_2A_INDEX		
					ALM_process_value(2 * dig_index, f);										// process sub-channel A
				}	
				dig_p_channel->sub[0].normal_alarm_count = 0;									// having used the value, clear it
																								// ALM_config[0] = 1A, [1] = 1B, [2] = 2A, [3] = 2B, 
																								// dig_index = 0 or 1 for D1x or D2x
				LOG_set_next_time(&dig_p_channel->sub[0].normal_alarm_time, ALM_config[(dig_index * 2) + ALM_ALARM_DERIVED_CHANNEL0].sample_interval, true);
			}
		}
		else
			dig_p_channel->sub[1].normal_alarm_time = SLP_NO_WAKEUP;
	}
																								// sub-channel B
	if ((dig_p_config->sensor_type & DIG_EVENT_B_MASK) == 0)									// only do it if not an event subchannel
	{
		if (ALM_config[(dig_index * 2) + 1].enabled)
		{
			if (dig_p_channel->sub[1].normal_alarm_time == SLP_NO_WAKEUP)						// first one
			{
				dig_p_channel->sub[1].normal_alarm_time = dig_p_channel->sample_time;
				LOG_set_next_time(&dig_p_channel->sub[1].normal_alarm_time, ALM_config[(dig_index * 2) + 1].sample_interval, true);
				dig_p_channel->sub[1].normal_alarm_count = 0;
			}
			if (dig_p_channel->sub[1].normal_alarm_time == dig_p_channel->sample_time)			// Do sub-channel B if it's time:
			{
				if ((dig_start_flags[dig_index].sub_start[1] & DIG_START_MASK_NORM_ALM) != 0)	// check start flag for this sub-channel
					dig_start_flags[dig_index].sub_start[1] &= ~DIG_START_MASK_NORM_ALM;		// if set clear it and do not calc alarm (still need to clear counters and set next time)
				else
				{
					f = DIG_volume_to_rate_enum(dig_p_channel->sub[1].normal_alarm_count * dig_p_config->fcal_b,
							ALM_config[(dig_index * 2) + 1].sample_interval, dig_p_config->rate_enumeration);
																								// 2 * dig_index + 1 = LOG_DIGITAL_1B_INDEX or LOG_DIGITAL_2B_INDEX		
					ALM_process_value((2 * dig_index) + 1, f);									// process sub-channel B
				}	
				dig_p_channel->sub[1].normal_alarm_count = 0;									// having used the value, clear it
				LOG_set_next_time(&dig_p_channel->sub[1].normal_alarm_time, ALM_config[(dig_index * 2) + 1].sample_interval, true);
			}
		}
		else
			dig_p_channel->sub[1].normal_alarm_time = SLP_NO_WAKEUP;
	}
}

/******************************************************************************
** Function:	Check if derived alarm sampling is due & if so do it
**
** Notes:	
*/
void dig_update_derived_alarm(void)
{
	float f;
																								// ALM_config[0] = 1A, [1] = 1B, [2] = 2A, [3] = 2B, 
																								// dig_index = 0 or 1 for D1x or D2x
																								// sub-channel A
	if ((dig_p_config->sensor_type & DIG_EVENT_A_MASK) == 0)									// only do it if not an event subchannel
	{
		if (ALM_config[(dig_index * 2) + ALM_ALARM_DERIVED_CHANNEL0].enabled)
		{
			if (dig_p_channel->sub[0].derived_alarm_time == SLP_NO_WAKEUP)						// if first time - alarm time set to sleep but alarm enabled
			{
				dig_p_channel->sub[0].derived_alarm_time = dig_p_channel->sample_time;
				LOG_set_next_time(&dig_p_channel->sub[0].derived_alarm_time, ALM_config[(dig_index * 2) + ALM_ALARM_DERIVED_CHANNEL0].sample_interval, true);
			}
			if (dig_p_channel->sub[0].derived_alarm_time == dig_p_channel->sample_time)			// Do sub-channel A, which may be A & B combined:
			{
				// check start flag for this sub-channel
				if ((dig_start_flags[dig_index].sub_start[0] & DIG_START_MASK_DRVD_ALM) != 0)
				{
					dig_start_flags[dig_index].sub_start[0] &= ~DIG_START_MASK_DRVD_ALM;		// if set clear it and do not calc alarm
				}
				else
				{
					f = dig_p_channel->sub[0].derived_alarm_volume;								// Volume is already combined if channels are combined
					ALM_process_value((2 * dig_index) + ALM_ALARM_DERIVED_CHANNEL0, f);			// process sub-channel A
				}	
				dig_p_channel->sub[0].derived_alarm_volume = 0;									// having used the value, clear it
																								// ALM_config[0] = 1A, [1] = 1B, [2] = 2A, [3] = 2B, 
																								// dig_index = 0 or 1 for D1x or D2x
				LOG_set_next_time(&dig_p_channel->sub[0].derived_alarm_time, ALM_config[dig_index * 2].sample_interval, true);
			}
		}
		else
			dig_p_channel->sub[1].derived_alarm_time = SLP_NO_WAKEUP;
	}
																								// sub-channel B
	if ((dig_p_config->sensor_type & DIG_EVENT_B_MASK) == 0)									// only do it if not an event subchannel
	{
		if (ALM_config[(dig_index * 2) + 1 + ALM_ALARM_DERIVED_CHANNEL0].enabled)
		{
			if (dig_p_channel->sub[1].derived_alarm_time == SLP_NO_WAKEUP)						// first one
			{
				dig_p_channel->sub[1].derived_alarm_time = dig_p_channel->sample_time;
				LOG_set_next_time(&dig_p_channel->sub[1].derived_alarm_time, ALM_config[(dig_index * 2) + 1 + ALM_ALARM_DERIVED_CHANNEL0].sample_interval, true);
			}
			if (dig_p_channel->sub[1].derived_alarm_time == dig_p_channel->sample_time)			// Do sub-channel B if it's time:
			{
				if ((dig_start_flags[dig_index].sub_start[1] & DIG_START_MASK_DRVD_ALM) != 0)	// check start flag for this sub-channel
					dig_start_flags[dig_index].sub_start[1] &= ~DIG_START_MASK_DRVD_ALM;		// if set clear it and do not calc alarm (still need to clear counters and set next time)
				else
				{
					f = dig_p_channel->sub[1].derived_alarm_volume;
					ALM_process_value((2 * dig_index) + 1 + ALM_ALARM_DERIVED_CHANNEL0, f);		// process sub-channel B
				}	
				dig_p_channel->sub[1].derived_alarm_volume = 0;									// having used the value, clear it
				LOG_set_next_time(&dig_p_channel->sub[1].derived_alarm_time, ALM_config[(dig_index * 2) + 1].sample_interval, true);
			}
		}
		else
			dig_p_channel->sub[1].derived_alarm_time = SLP_NO_WAKEUP;
	}
}

/******************************************************************************
** Function:	Correct totaliser for overflow / underflow
**
** Notes:	
*/
void dig_correct_totaliser(long long int *p)
{
#define DIG_TOTALISER_MAX	99999999999999LL

	if (*p < 0)
		*p += 1 + DIG_TOTALISER_MAX;
	else if (*p > DIG_TOTALISER_MAX)
		*p -= 1 + DIG_TOTALISER_MAX;
}

/******************************************************************************
** Function:	Update the totalisers for pulse counting every sample
**
** Notes:		Combined channels still use separate Tcal values
*/
void dig_update_totalisers(void)
{
	long long int a64, b64;
	float volume;

	if ((DIG_config[dig_index].sensor_type & DIG_PULSE_MASK) == 0x00)							// only for pulse counting channels
		return;

	a64 = (long long int)dig_p_channel->sub[0].sample_count;									// Do sub-channel A, which may be A & B combined:
	a64 *= dig_p_channel->sub[0].totaliser.tcal_x10000;
	if ((DIG_config[dig_index].flags & DIG_MASK_COMBINE_SUB_CHANNELS) != 0)
	{
		b64 = (long long int)dig_p_channel->sub[1].sample_count;
		b64 *= dig_p_channel->sub[1].totaliser.tcal_x10000;
		switch (DIG_config[dig_index].sensor_type & 0x07)
		{
		case DIG_TYPE_FWD_A:
			break;

		case DIG_TYPE_FWD_A_FWD_B:
			a64 += b64;
			break;

		case DIG_TYPE_BOTH_A_REV_B:
			a64 -= 2 * b64;
			break;

		case DIG_TYPE_DIR_B_HIGH_FWD:
			a64 = b64 - a64;
			break;

		default:																				// DIG_TYPE_FWD_A_REV_B and DIG_TYPE_DIR_B_HIGH_REV:
			a64 -= b64;
			break;
		}
	}
	dig_p_channel->sub[0].totaliser.value_x10000 += a64;
	dig_correct_totaliser(&dig_p_channel->sub[0].totaliser.value_x10000);
	volume = ((float)a64) / 10000;																// increment derived volumes
	dig_p_channel->sub[0].derived_volume += volume;
	dig_p_channel->sub[0].derived_sms_volume += volume;
	dig_p_channel->sub[0].derived_alarm_volume += volume;
	dig_p_channel->sub[0].derived_min_max_sample += volume;

	if (((DIG_config[dig_index].flags & DIG_MASK_COMBINE_SUB_CHANNELS) == 0) &&					// Do sub-channel B if necessary:
		((DIG_config[dig_index].sensor_type & DIG_PULSE_MASK) > DIG_TYPE_FWD_A))
	{
		b64 = (long long int)dig_p_channel->sub[1].sample_count;;
		b64 *= dig_p_channel->sub[1].totaliser.tcal_x10000;
		dig_p_channel->sub[1].totaliser.value_x10000 += b64;
		dig_correct_totaliser(&dig_p_channel->sub[1].totaliser.value_x10000);
		volume = ((float)b64) / 10000;															// increment derived volumes
		dig_p_channel->sub[1].derived_volume += volume;
		dig_p_channel->sub[1].derived_sms_volume += volume;
		dig_p_channel->sub[1].derived_alarm_volume += volume;
		dig_p_channel->sub[1].derived_min_max_sample += volume;
	}
}

/******************************************************************************
** Function:	Update the totalisers for event logging every interval
**
** Notes:		uses totaliser and tcal for sub channel
*/
void dig_update_event_log_totaliser(int sub_index, int32 count)
{
	long long int a64;

	a64 = (long long int)count;
	a64 *= dig_p_channel->sub[sub_index].totaliser.tcal_x10000;
	dig_p_channel->sub[sub_index].totaliser.value_x10000 += a64;
	dig_correct_totaliser(&dig_p_channel->sub[sub_index].totaliser.value_x10000);
}

/******************************************************************************
** Function:	Update the totalisers for event pump system logging every interval
**
** Notes:		uses totaliser and tcal for sub channel
**				only called for first pump in a system
*/
void dig_update_event_pump_totaliser(int sub_index, float volume)
{
	long long int a64;

	a64 = (long long int)volume;
	a64 *= dig_p_channel->sub[sub_index].totaliser.tcal_x10000;
	dig_p_channel->sub[sub_index].totaliser.value_x10000 += a64;
	dig_correct_totaliser(&dig_p_channel->sub[sub_index].totaliser.value_x10000);
}

/******************************************************************************
** Function:	Initialise min/max values & times for channel given pointer to sub channel
**
** Notes:		must deal with derived min max as well, with supplied bool
*/
void dig_clear_min_max(DIG_sub_channel_type * p_sub, bool derived)
{
	if (derived)
	{
		p_sub->min_derived_value = FLT_MAX;
		p_sub->max_derived_value = -FLT_MAX;
		*(uint32 *)&p_sub->min_derived_time = 0;
		*(uint32 *)&p_sub->max_derived_time = 0;
	}
	else
	{
		p_sub->min_value = FLT_MAX;
		p_sub->max_value = -FLT_MAX;
		*(uint32 *)&p_sub->min_time = 0;
		*(uint32 *)&p_sub->max_time = 0;
	}
}

/******************************************************************************
** Function:	Print totaliser value to referenced buffer
**
** Notes:		
*/
int DIG_print_totaliser_int(char *buffer, long long int *p_value_x10000)
{
	return sprintf(buffer, "%010lld", *p_value_x10000 / 10000);
}

/******************************************************************************
** Function:	create footer for digital logdata file
**
** Notes:		channel is 0 or 1 for D1A / D1B, 2 or 3 for D2A / D2B
**				Add signal strength, batt volts & \r\n, after calling this.
*/
int DIG_create_block_footer(char * string, uint8 channel, bool sms, bool derived)
{
	int len;
	uint8 chan, sub_chan, min_max_interval, event_mask;
	DIG_sub_channel_type * p_sub;
	
	chan = channel / 2;						
	sub_chan = channel - (2* chan);
	p_sub = &(DIG_channel[chan].sub[sub_chan]);															// get correct sub channel pointer
	if (derived)																						// for derived
	{
		min_max_interval = DIG_config[chan].min_max_sample_interval;									// min max interval comes from main channel config
		if ((min_max_interval == 0) || sms)																// if min/max interval is zero or is sms data
			len = sprintf(string, "\r\n*");																// empty string
		else
		{
			len = LOG_print_footer_min_max(string, &p_sub->min_derived_time, p_sub->min_derived_value,
					&p_sub->max_derived_time, p_sub->max_derived_value);
			len += sprintf(&string[len], ",,");															// no totaliser or units
		}
	}
	else
	{
		event_mask = (sub_chan == 0) ? DIG_EVENT_A_MASK : DIG_EVENT_B_MASK;								// get correct min/max interval
		if ((DIG_config[chan].sensor_type & event_mask) != 0x00)										// if this sub channel is event logging
			min_max_interval = DIG_config[chan].ec[sub_chan].min_max_sample_interval;					// interval comes from sub channel config
		else
			min_max_interval = DIG_config[chan].min_max_sample_interval;								// else it comes from main channel config
	
		if ((min_max_interval == 0) || sms)																// if min/max interval is zero or is sms data
			len = sprintf(string, "\r\n*,,,,");															// will be followed by totaliser
		else
		{
			len = LOG_print_footer_min_max(string, &p_sub->min_time, p_sub->min_value, &p_sub->max_time, p_sub->max_value);
			string[len++] = ',';
			string[len] = '\0';																			// will be followed by totaliser
		}
		len += DIG_print_totaliser_int(&string[len], &p_sub->totaliser.value_x10000);
		len += sprintf(&string[len], ",%u", p_sub->totaliser.units_enumeration);							// end of digital footer
	}
	dig_clear_min_max(p_sub, derived);
	
	return len;
}

/******************************************************************************
** Function:	Log totaliser + min/max, then re-initialise for next day
**
** Notes:		Need to do A & B channels separately
**				Can enqueue value as pointer to subchannel data for convenience when creating footer
**				V3.22: only enqueue footer here if logging values from pulse counting 
*/
void dig_channel_midnight_task(void)
{
	bool derived = ((dig_p_config->flags & DIG_MASK_DERIVED_VOL_ENABLED) != 0x00);

	if ((DIG_config[dig_index].sensor_type & DIG_PULSE_MASK) > DIG_TYPE_NONE)					// if logging pulses on A
	{
		LOG_enqueue_value((2 * dig_index) + 1, LOG_BLOCK_FOOTER, 0);							// enqueue footer
		if (derived)																			// if derived
			LOG_enqueue_value(((2 * dig_index) + 1) | LOG_DERIVED_MASK, LOG_BLOCK_FOOTER, 0);	// enqueue derived footer
		if (DIG_config[dig_index].sms_data_interval != 0)										// if sms
		{
			LOG_enqueue_value(((2 * dig_index) + 1) | LOG_SMS_MASK, LOG_BLOCK_FOOTER, 0);		// enqueue sms footer
			if (derived)																		// if derived sms
				LOG_enqueue_value(((2 * dig_index) + 1) | LOG_SMS_MASK | LOG_DERIVED_MASK, 
								  LOG_BLOCK_FOOTER, 0);											// enqueue derived sms footer
		}
	}
																								// also do sub-channel B if necessary
	if (((DIG_config[dig_index].flags & DIG_MASK_COMBINE_SUB_CHANNELS) == 0) &&				
		((DIG_config[dig_index].sensor_type & DIG_PULSE_MASK) > DIG_TYPE_FWD_A))
	{
		LOG_enqueue_value((2 * dig_index) + 2, LOG_BLOCK_FOOTER, 0);
		if (derived)
			LOG_enqueue_value(((2 * dig_index) + 2) | LOG_DERIVED_MASK, LOG_BLOCK_FOOTER, 0);
		if (DIG_config[dig_index].sms_data_interval != 0)
		{
			LOG_enqueue_value(((2 * dig_index) + 2) | LOG_SMS_MASK, LOG_BLOCK_FOOTER, 0);
			if (derived)
				LOG_enqueue_value(((2 * dig_index) + 2) | LOG_SMS_MASK | LOG_DERIVED_MASK, 
								  LOG_BLOCK_FOOTER, 0);
		}
	}
}

/******************************************************************************
** Function:	Update sub-channel counters
**
** Notes:		Call with sub-channel sample counts = no. of pulses seen during last sample
*/
void dig_update_sub_channel(DIG_sub_channel_type *p)
{
	p->log_count += p->sample_count;
	p->sms_count += p->sample_count;
	p->normal_alarm_count += p->sample_count;

	p->previous_sample_count = p->sample_count;
}

/******************************************************************************
** Function:	return immediate flow of a current clamp pump from flow rate table
**
** Notes:		returns 0 if pump is off, system flow if pump is in a system
*/
float dig_get_immediate_pump_flow(int chan, int sub)
{
	int  pump, i, index;

	pump = (2 * chan) + sub;
 
	if (DIG_config[chan].ec[sub].sensor_type == 2)								// if individual pump
	{
		if (!DIG_system_pump[pump].on)											// return 0 if pump is off
			return 0.0;
		else
			return DIG_pfr_table[0x0001 << pump];								// get its immediate flow
	}
	else																		// else get immediate system flow
	{
		index = 0x0000;
#if (HDW_NUM_CHANNELS == 9)
		for (i = 0; i < 4; i++)
#else
		for (i = 0; i < 2; i++)
#endif
		{
			if (DIG_config[i / 2].ec[i % 2].sensor_type == 3)					// if system pump
			{
				if (DIG_system_pump[i].on)										// if pump is on
					index |= (0x0001 << i);										// calc index
			}
		}
		return DIG_pfr_table[index];
	}
}

/******************************************************************************
** Function:	update the running time given individual pump
**
** Notes:		update pump run time if ON every log interval and when pump switches off
**				after adding time to total, zero time on
*/
void dig_update_pump_running_time(int pump, uint32 time_elapsed_500ms)
{
	uint32 seconds;
	uint32 minutes;
	uint32 hours;

	seconds = (uint32)DIG_system_pump[pump].running_time.secs + (time_elapsed_500ms / 2);			// update this pump's running time
	minutes = seconds / 60l;
	DIG_system_pump[pump].running_time.secs = (uint8)(seconds - (minutes * 60l));
	minutes += (uint32)DIG_system_pump[pump].running_time.mins;
	hours = minutes / 60l;
	DIG_system_pump[pump].running_time.mins = (uint8)(minutes - (hours * 60l));
	DIG_system_pump[pump].running_time.hrs += hours;
	if (DIG_system_pump[pump].on)																	// if pump is on set turn on time to now
		DIG_system_pump[pump].time_on_500ms = (RTC_time_sec << 1) + RTC_half_sec;
}

/******************************************************************************
** Function:	update the volume pumped during current interval by given individual pump
**
** Notes:		because it has switched off or it is on and at end of interval
**				also updates running time
*/
void dig_update_pump_volume_in_interval(int pump)
{
	uint32 time_now_500ms;
	uint32 time_elapsed_500ms;

	if (RTC_time_sec != 0L)																// calculate time elapsed since last change or interval
		time_now_500ms = ((RTC_time_sec << 1) + RTC_half_sec);
	else																				// taking midnight into account
		time_now_500ms = RTC_SEC_PER_DAY * 2;
	time_elapsed_500ms = time_now_500ms - DIG_system_pump[pump].time_on_500ms;
	dig_update_pump_running_time(pump, time_elapsed_500ms);
	DIG_system_pump[pump].volume += (DIG_pfr_table[0x0001 << pump] * (float)time_elapsed_500ms) / 2;
}

/******************************************************************************
** Function:	update the volume pumped by the system of pumps during current interval
**
** Notes:		because a pump has changed state or at end of an interval
*/
void dig_update_system_volume_in_interval(void)
{
	uint32 time_now_500ms;
	uint32 time_elapsed_500ms;
	uint8  index = 0;

	if (RTC_time_sec != 0L)																// calculate time elapsed since last change or interval
		time_now_500ms = ((RTC_time_sec << 1) + RTC_half_sec);
	else																				// taking midnight into account
		time_now_500ms = RTC_SEC_PER_DAY * 2;
	time_elapsed_500ms = time_now_500ms - DIG_system_change_time_500ms;
	if (RTC_time_sec != 0L)																// record time of this change
		DIG_system_change_time_500ms = time_now_500ms;
	else																				// taking midnight into account
		DIG_system_change_time_500ms = 0L;
	if ((DIG_system_pump[0].on) && (DIG_config[0].ec[0].sensor_type == 3))				// calculate index into system table if pump is part of a system
	{
		index |= 0x01; 
		dig_update_pump_running_time(0, time_elapsed_500ms);
	}	
	if ((DIG_system_pump[1].on) && (DIG_config[0].ec[1].sensor_type == 3))
	{
		index |= 0x02; 
		dig_update_pump_running_time(1, time_elapsed_500ms);
	}	
#if (HDW_NUM_CHANNELS == 9)
	if ((DIG_system_pump[2].on) && (DIG_config[1].ec[0].sensor_type == 3))
	{
		index |= 0x04; 
		dig_update_pump_running_time(2, time_elapsed_500ms);
	}	
	if ((DIG_system_pump[3].on) && (DIG_config[1].ec[1].sensor_type == 3))
	{
		index |= 0x08; 
		dig_update_pump_running_time(3, time_elapsed_500ms);
	}	
#endif
	DIG_system_volume += (DIG_pfr_table[index] * (float)time_elapsed_500ms) / 2;
}

/******************************************************************************
** Function:	Calculate and log rate from amount received in one interval
**
** Notes:		given subchannel - dig_index holds main channel
**				also adds to totaliser
*/
void dig_calc_log_event_rate(int sub)
{
	float    amount;
	float    rate;
	RTC_type ftp_time_stamp;

	ftp_time_stamp.reg32[1] = RTC_now.reg32[1];										 		// set date of ftp timestamp of this data item
																							// calculate time of ftp timestamp
	ftp_time_stamp.reg32[sub] = LOG_get_timestamp(dig_p_channel->sub[sub].event_log_time, dig_p_config->ec[sub].log_interval);
	amount = (float)dig_p_channel->sub[sub].event_count * dig_p_config->ec[sub].cal;		// calculate amount in interval
	dig_p_channel->sub[sub].sms_amount += amount;											// add to total for sms
	dig_p_channel->sub[sub].min_max_sample += amount;										// add to total for min_max
	dig_p_channel->sub[sub].normal_alarm_amount += amount;									// add to total for alarm
	dig_update_event_log_totaliser(sub, dig_p_channel->sub[sub].event_count);				// update totaliser
	dig_p_channel->sub[sub].previous_event_count = dig_p_channel->sub[sub].event_count;		// record count as previous count
	dig_p_channel->sub[sub].event_count = 0;												// clear event count register for next interval
																							// calculate rate
	rate = DIG_volume_to_rate_enum(amount, dig_p_config->ec[sub].log_interval, dig_p_config->ec[sub].rate_enumeration);
																							// log the value and header if called for
	dig_log_value(dig_index, sub, (int32)LOG_get_timestamp(dig_p_channel->sub[sub].event_log_time, dig_p_config->ec[sub].log_interval), rate);
	FTP_update_retrieval_info((2 * dig_index) + sub, &ftp_time_stamp);						// normal data to be sent
}

/******************************************************************************
** Function:	Calculate and log rate from amount pumped by on/off pump in one interval at end of interval
**
** Notes:		given subchannel, dig_index holds main channel
**				also adds to totaliser
*/
void dig_calc_log_pump_rate(int sub)
{
	uint8    pump;
	float    volume;
	float    value;
	RTC_type ftp_time_stamp;

	ftp_time_stamp.reg32[1] = RTC_now.reg32[1];										 		// set date of ftp timestamp of this data item
																							// calculate time of ftp timestamp
	ftp_time_stamp.reg32[0] = LOG_get_timestamp(dig_p_channel->sub[sub].event_log_time, dig_p_config->ec[sub].log_interval);

	pump = (2*dig_index) + sub;
	volume = DIG_system_pump[pump].volume;													// get this pump's volume pumped this interval in pfr units
	dig_p_channel->sub[sub].sms_amount+= volume;											// add to total for sms
	dig_p_channel->sub[sub].min_max_sample+= volume;										// add to total for min_max
	dig_p_channel->sub[sub].normal_alarm_amount+= volume;									// add to total for alarm
	DIG_system_pump[pump].previous_volume = volume;											// save the pfr volume as previous volume
	DIG_system_pump[pump].volume = 0;														// clear the volume for the next interval
	dig_update_event_pump_totaliser(sub, volume);											// update totaliser in volume units
																							// calculate flow rate from pfr volume
	value = DIG_volume_to_rate_enum(volume, dig_p_config->ec[sub].log_interval, dig_p_config->ec[sub].rate_enumeration);
																							// log the value and header if called for
	dig_log_value(dig_index, sub, (int32)LOG_get_timestamp(dig_p_channel->sub[sub].event_log_time, dig_p_config->ec[sub].log_interval), value);
	FTP_update_retrieval_info((2 * dig_index) + sub, &ftp_time_stamp);						// normal data to be sent
}

/******************************************************************************
** Function:	Calculate and log rate from amount pumped by system pumps in one interval at end of interval
**
** Notes:		given subchannel of pump to log to, dig_index holds main channel
**				also adds to totaliser
**				DIG_system_volume holds the total pumped this interval by the system
**				only called for first pump in a system
*/
void dig_calc_log_system_rate(int sub)
{
	float    value;
	float    volume;
	RTC_type ftp_time_stamp;

	ftp_time_stamp.reg32[1] = RTC_now.reg32[1];										 		// set date of ftp timestamp of this data item
																							// calculate time of ftp timestamp
	ftp_time_stamp.reg32[0] = LOG_get_timestamp(dig_p_channel->sub[sub].event_log_time, dig_p_config->ec[sub].log_interval);
	volume = DIG_system_volume;																// get the system's volume pumped this interval
	dig_p_channel->sub[sub].sms_amount+= volume;											// add to total for sms
	dig_p_channel->sub[sub].min_max_sample+= volume;										// add to total for min_max
	dig_p_channel->sub[sub].normal_alarm_amount+= volume;									// add to total for alarm
	DIG_system_previous_volume  = DIG_system_volume;										// save the volume
	DIG_system_volume = 0;																	// clear the volume for the next interval
	dig_update_event_pump_totaliser(sub, volume);											// update totaliser
																							// calculate flow rate from pfr volume
	value = DIG_volume_to_rate_enum(volume, dig_p_config->ec[sub].log_interval, dig_p_config->ec[sub].rate_enumeration);
																							// log the value and header if called for
	dig_log_value(dig_index, sub, (int32)LOG_get_timestamp(dig_p_channel->sub[sub].event_log_time, dig_p_config->ec[sub].log_interval), value);
	FTP_update_retrieval_info((2 * dig_index) + sub, &ftp_time_stamp);						// normal data to be sent
}

/******************************************************************************
** Function:	Check all sub channel event log times
**
** Notes:		if time calculate and log rate value derived from an event
** 				dig_p_config set to address of channel config
**				dig_p_channel set to address of channel data
**				dig_index set to channel number
*/
void dig_check_event_log(int sub_index)
{
	uint8 sensor_type = dig_p_config->ec[sub_index].sensor_type;
	int channel_index = (2 * dig_index) + sub_index;

	if (dig_p_channel->sub[sub_index].event_log_time > RTC_time_sec)						// not yet time to log time channel dig_index subchannel
		return;

	if ((dig_start_flags[dig_index].sub_start[sub_index] & DIG_START_MASK_E_LOG) != 0)		// check start flag for this channel
	{
		dig_start_flags[dig_index].sub_start[sub_index] &= ~DIG_START_MASK_E_LOG;			// if set clear it and do not log
		if (sensor_type == 2)																// if individual on/off event logging
			DIG_system_pump[channel_index].volume = 0;										// clear pump volume per interval
		else if (sensor_type == 3)															// if system on/off event logging
			DIG_system_volume = 0;															// clear system volume per interval
	}
	else if (sensor_type == 1)																// else if rate from event counts sensor type
	{
		dig_calc_log_event_rate(sub_index);												// calculate and log rate from number of events received
		if (dig_p_channel->sub[sub_index].event_log_time == 0)							// check for midnight when event logging
			LOG_enqueue_value(1 + channel_index, LOG_BLOCK_FOOTER, 0);					// log file footer
	}
	else if (sensor_type == 2)															// else if volume from on/off individual type
	{
		if (DIG_system_pump[channel_index].on)											// if pump is on at end of interval
			dig_update_pump_volume_in_interval(channel_index);							// update volume pumped by this pump in interval
		dig_calc_log_pump_rate(sub_index);												// calculate and log rate from volume pumped in interval
		if (dig_p_channel->sub[sub_index].event_log_time == 0)							// check for midnight when event logging
			LOG_enqueue_value(1 + channel_index, LOG_BLOCK_FOOTER, 0);					// log file footer
	}
	else if (sensor_type == 3)															// else if system on/off sensor
	{
		if ((dig_system_interval_flags & DIG_SYSTEM_LOG_ONE_SHOT) != 0x00)				// if first time through
		{
			dig_update_system_volume_in_interval();										// update volume pumped by system this interval
			dig_calc_log_system_rate(sub_index);										// calculate and log rate pumped by system in this interval
			if (dig_p_channel->sub[sub_index].event_log_time == 0)						// check for midnight when event logging
				LOG_enqueue_value(1 + channel_index, LOG_BLOCK_FOOTER, 0);				// log file footer
			dig_system_interval_flags &= ~DIG_SYSTEM_LOG_ONE_SHOT;						// clear the one shot flag
		}
	}
	
	LOG_set_next_time(&dig_p_channel->sub[sub_index].event_log_time, dig_p_config->ec[sub_index].log_interval, false);
}

/******************************************************************************
** Function:	Check all sub channel event sms times
**
** Notes:		if time log rate value derived from an event to SMS file
** 				dig_p_config set to address of channel config
**				dig_p_channel set to address of channel data
**				dig_index set to channel number
*/
void dig_check_event_sms(int sub_index)
{
	float rate;

	if (dig_p_channel->sub[sub_index].event_sms_time > RTC_time_sec)								// not yet time to log sms time channel dig_index subchannel
		return;

	if ((dig_start_flags[dig_index].sub_start[sub_index] & DIG_START_MASK_E_SMS) != 0)			// check start flag for this channel
		dig_start_flags[dig_index].sub_start[sub_index] &= ~DIG_START_MASK_E_SMS;				// if set clear it and do not log
	else
	{																							// calculate rate for sms log from sms amount
		if ((dig_p_config->ec[sub_index].sensor_type != 3) || 
		    ((dig_system_interval_flags & DIG_SYSTEM_SMS_ONE_SHOT) != 0x00))					// but only first time through a pump system
		{
			rate = DIG_volume_to_rate_enum(dig_p_channel->sub[sub_index].sms_amount, 
										   dig_p_config->ec[sub_index].sms_data_interval, 
										   dig_p_config->ec[sub_index].rate_enumeration);
			dig_p_channel->sub[sub_index].sms_amount = 0.0f;									// clear sms amount
																								// log the sms rate and header if called for as sms
			dig_log_sms_value(dig_index, 
							  sub_index, 
							  (int32)LOG_get_timestamp(dig_p_channel->sub[sub_index].event_sms_time, 
													   dig_p_config->ec[sub_index].sms_data_interval), 
							  rate);
			if (dig_p_config->ec[sub_index].sensor_type == 3)									// if a system pump 
			    dig_system_interval_flags &= ~DIG_SYSTEM_SMS_ONE_SHOT;							// clear its one shot sms flag
			if (dig_p_channel->sub[sub_index].event_sms_time == 0)								// check for midnight when event sms logging and log file footer
				LOG_enqueue_value((1 + sub_index + (2 * dig_index)) | LOG_SMS_MASK, LOG_BLOCK_FOOTER, 0);			
		}
	}

	LOG_set_next_time(&dig_p_channel->sub[sub_index].event_sms_time, dig_p_config->ec[sub_index].sms_data_interval, true);
}

/******************************************************************************
** Function:	Check all sub channel event min_max times
**
** Notes:		if time check rate value derived from an event against min and max values
** 				dig_p_config set to address of channel config
**				dig_p_channel set to address of channel data
**				dig_index set to channel number
*/
void dig_check_event_min_max(int sub_index)
{
	if (dig_p_channel->sub[sub_index].event_min_max_sample_time > RTC_time_sec)					// not yet time to do min_max
		return;

	if ((dig_p_config->ec[sub_index].sensor_type != 3) || 
	    ((dig_system_interval_flags & DIG_SYSTEM_MINMAX_ONE_SHOT) != 0x00))						// only first time through a pump system
	{
		if ((dig_start_flags[dig_index].sub_start[sub_index] & DIG_START_MASK_MIN_MAX) != 0)	// check start flag for this sub-channel
			dig_start_flags[dig_index].sub_start[sub_index] &= ~DIG_START_MASK_MIN_MAX;			// if set clear it and do not calc min/max 
																								// (still need to clear counters and set next time)
		else
			dig_compare_min_max(&dig_p_channel->sub[sub_index], 
									dig_p_config->ec[sub_index].min_max_sample_interval,
									dig_p_config->ec[sub_index].rate_enumeration);
		dig_p_channel->sub[sub_index].min_max_sample = 0.0f;									// clear min/max flow accumulator
		if (dig_p_config->ec[sub_index].sensor_type == 3)										// if a system pump 
		    dig_system_interval_flags &= ~DIG_SYSTEM_MINMAX_ONE_SHOT;							// clear its one shot min max flag
	}

	LOG_set_next_time(&dig_p_channel->sub[sub_index].event_min_max_sample_time, dig_p_config->ec[sub_index].min_max_sample_interval, true);
}

/******************************************************************************
** Function:	Check all sub channel event alarm times
**
** Notes:		if time check rate value derived from an event for alarm conditions
** 				dig_p_config set to address of channel config
**				dig_p_channel set to address of channel data
**				dig_index set to channel number
*/
void dig_check_event_alarm(int sub_index)
{
	int alarm_index = (dig_index * 2) + sub_index;
	float value;

	if (!ALM_config[alarm_index].enabled)
	{
		dig_p_channel->sub[sub_index].normal_alarm_time = SLP_NO_WAKEUP;
		return;
	}

	if ((dig_p_config->ec[sub_index].sensor_type != 3) || 
	    ((dig_system_interval_flags & DIG_SYSTEM_ALM_ONE_SHOT) != 0x00))							// but only first time through a pump system
	{
		if (dig_p_channel->sub[sub_index].normal_alarm_time == SLP_NO_WAKEUP)						// first one
		{
			dig_p_channel->sub[sub_index].normal_alarm_time = RTC_time_sec;
			LOG_set_next_time(&dig_p_channel->sub[sub_index].normal_alarm_time, ALM_config[alarm_index].sample_interval, true);
			dig_p_channel->sub[sub_index].normal_alarm_amount = 0;									// clear volume accumulator
		}

		if (dig_p_channel->sub[sub_index].normal_alarm_time <= RTC_time_sec)						// if time
		{
			if ((dig_start_flags[dig_index].sub_start[sub_index] & DIG_START_MASK_NORM_ALM) != 0)	// check start flag for this sub-channel
				dig_start_flags[dig_index].sub_start[sub_index] &= ~DIG_START_MASK_NORM_ALM;		// if set clear it and do not calc alarm 
																									// (still need to clear counters and set next time)
			else
			{
				value = DIG_volume_to_rate_enum(dig_p_channel->sub[sub_index].normal_alarm_amount, 
												ALM_config[alarm_index].sample_interval,
												dig_p_config->ec[sub_index].rate_enumeration);
				ALM_process_value(alarm_index, value);												// process sub-channel
			}	
			dig_p_channel->sub[sub_index].normal_alarm_amount = 0.0f;								// having used the value, clear it
			LOG_set_next_time(&dig_p_channel->sub[sub_index].normal_alarm_time, ALM_config[alarm_index].sample_interval, true);
			if (dig_p_config->ec[sub_index].sensor_type == 3)										// if a system pump 
			    dig_system_interval_flags &= ~DIG_SYSTEM_ALM_ONE_SHOT;								// clear its one shot alarm flag
		}
	}
}

/******************************************************************************
** Function:	Check all event debounce times
**
** Notes:		if any elapsed enable appropriate event interrupt control
**				DEL193 - if both edge triggering - update expected interrupt edge from present state of input line
**				as we may have had a change of state during debounce time
*/
void dig_check_event_debounce(void)
{
	uint8  sensor_type = DIG_config[dig_index].sensor_type;

	if (dig_index == 0)
	{
		if (DIG_channel[0].sub[0].event_time <= RTC_time_sec)									// debounce elapsed
		{
			DIG_channel[0].sub[0].event_time = SLP_NO_WAKEUP;									// don't do it again until next event
			if ((sensor_type & DIG_EVENT_A_MASK) == DIG_EVENT_A_BOTH)							// else if both edges are triggering
			{																					// look at present state of INT1 line
				if (DIG_INT1_LINE == 1)															// if high last edge was rising next edge will be falling
					_INT1EP = true;																// set falling interrupt on INT1
				else																			// else next edge will be rising
					_INT1EP = false;															// set rising interrupt on INT1
			}
			DIG_INT1_active = true;																// set interrupt active (will be enabled if we go to sleep)
			_INT1IF = false;																	// clear interrupt flag
		}
		if (DIG_channel[0].sub[1].event_time <= RTC_time_sec)									// debounce elapsed
		{
			DIG_channel[0].sub[1].event_time = SLP_NO_WAKEUP;									// don't do it again until next event
			if ((sensor_type & DIG_EVENT_B_MASK) == DIG_EVENT_B_BOTH)							// else if both edges are triggering
			{																					// look at present state of INT2 line
				if (DIG_INT2_LINE == 1)															// if high last edge was rising next edge will be falling
					_INT2EP = true;																// set falling interrupt on INT2
				else																			// else next edge will be rising
					_INT2EP = false;															// set rising interrupt on INT2
			}
			DIG_INT2_active = true;																// set interrupt active (will be enabled if we go to sleep)
			_INT2IF = false;																	// clear interrupt flag
		}
	}
	else
	{
		if (DIG_channel[1].sub[0].event_time <= RTC_time_sec)									// debounce elapsed
		{
			DIG_channel[1].sub[0].event_time = SLP_NO_WAKEUP;									// don't do it again until next event
#if (HDW_NUM_CHANNELS == 9)
			if ((sensor_type & DIG_EVENT_A_MASK) == DIG_EVENT_A_BOTH)							// else if both edges are triggering
			{																					// look at present state of INT3 line
				if (DIG_INT3_LINE == 1)															// if high last edge was rising next edge will be falling
					INTCON2 |= 0x0008;															// set falling interrupt on INT3
				else																			// else next edge will be rising
					INTCON2 &= 0xFFF7;															// set rising interrupt on INT3
			}
			DIG_INT3_active = true;																// set interrupt active (will be enabled if we go to sleep)
			_INT3IF = false;																	// clear interrupt flag
#endif
		}
		if (DIG_channel[1].sub[1].event_time <= RTC_time_sec)									// debounce elapsed
		{
			DIG_channel[1].sub[1].event_time = SLP_NO_WAKEUP;									// don't do it again until next event
#if (HDW_NUM_CHANNELS == 9)
			if ((sensor_type & DIG_EVENT_B_MASK) == DIG_EVENT_B_BOTH)							// else if both edges are triggering
			{																					// look at present state of INT4 line
				if (DIG_INT4_LINE == 1)															// if high last edge was rising next edge will be falling
					INTCON2 |= 0x0010;															// set falling interrupt on INT4
				else																			// else next edge will be rising
					INTCON2 &= 0xFFEF;															// set rising interrupt on INT4
			}
			DIG_INT4_active = true;																// set interrupt active (will be enabled if we go to sleep)
			_INT4IF = false;																	// clear interrupt flag
#endif
		}
	}
}

/******************************************************************************
** Function:	Set an event debounce time
**
** Notes:		sets time to wakeup and re-enable interrupts
**				dig_index contains channel (0 or 1)
*/
void dig_set_event_debounce(uint8 sub_channel)
{
	uint8 period = DIG_config[dig_index].event_log_min_period;							// look at minimum period in configure
	if ((period == 0) || (period > 24)) return;											// if continuous or too short just exit
																						// else
																						// disable event interrupt control and set wakeup time
	if (dig_index == 0)
	{
		if (sub_channel == 0)
			DIG_INT1_active = false;
		else
			DIG_INT2_active = false;
		DIG_channel[0].sub[sub_channel].event_time = RTC_time_sec + LOG_interval_sec[period];
	}
	else
	{
#if (HDW_NUM_CHANNELS == 9)
		if (sub_channel == 0)
			DIG_INT3_active = false;
		else
			DIG_INT4_active = false;
		DIG_channel[1].sub[sub_channel].event_time = RTC_time_sec + LOG_interval_sec[period];
#else
		DIG_channel[1].sub[sub_channel].event_time = SLP_NO_WAKEUP;
#endif
	}
}

/******************************************************************************
** Function:	Enqueue an event log
**
** Notes:		checks header flag and enqueues header if necc
*/
void dig_enqueue_event(uint8 event_index, uint32 event_value)
{
	uint16 	 header_mask;
	RTC_type time_stamp;	

	time_stamp.reg32[0] = RTC_now.reg32[0] & 0x00ffffff;									// set time stamp to now
	time_stamp.reg32[1] = RTC_now.reg32[1];
	header_mask = 1 << event_index;															// enqueue header if flagged
	if ((LOG_header_mask & header_mask) == 0)												// needs event header
	{
		if (LOG_enqueue_value(event_index, LOG_EVENT_HEADER, time_stamp.reg32[0]))
			LOG_header_mask |= header_mask;													// done
	}
	LOG_enqueue_value(event_index, LOG_EVENT_TIMESTAMP, event_value);						// enqueue event
	FTP_update_retrieval_info(event_index - LOG_EVENT_1A_INDEX, &time_stamp);				// normal data to be sent
	if (ALM_config[event_index - LOG_EVENT_1A_INDEX].enabled)								// check whether alarm enabled
		ALM_process_event(event_index - LOG_EVENT_1A_INDEX,									// process event alarm 
						  (event_value & DIG_EVENT_HIGH_MASK) == DIG_EVENT_HIGH_MASK);
}

/******************************************************************************
** Function:	Check event interrupt flags and act on them
**
** Notes:		dig_index contains channel (0 or 1)
**				NB There is an inversion in the logic levels between the measurement connector
**				and the interrupt port on the micro, so:
**				Rising edges are a transition from 1 to 0 at the connector
**				Falling edges are a transition from 0 to 1 at the connector
*/
void dig_check_events(void)
{
	uint32 event_value;
	uint8  sensor_type = DIG_config[dig_index].sensor_type;

																							// exit if not an event sensor, or disabled
	if (((DIG_config[dig_index].flags & DIG_MASK_CHANNEL_ENABLED) == 0) || (sensor_type < 16))
		return;

	event_value = RTC_time_sec << 7;														// set event timestamp to nearest half second
	if (RTC_half_sec != 0)
		event_value += 50;

	if (dig_index == 1)		// DIG2A & DIG2B
	{
#if (HDW_NUM_CHANNELS == 9)
																							// channels 2A and 2B
		if (_INT3IF && DIG_INT3_active)														// check INT3 flag if active
		{
			dig_set_event_debounce(0);
			DIG_channel[1].sub[0].event_flag = 1;											// set event flag
			_INT3IF = false;																// clear INT3 flag
			if ((sensor_type & DIG_EVENT_A_MASK) == 0) return;								// double check A channel is a valid event channel
																							// find which edge to record - set flag if rising
			if ((sensor_type & DIG_EVENT_A_MASK) == DIG_EVENT_A_RISE)						// if rising edge required
				event_value |= DIG_EVENT_HIGH_MASK;											// add value 1 flag to 28 bit timestamp value
			else if ((sensor_type & DIG_EVENT_A_MASK) == DIG_EVENT_A_BOTH)					// else if both edges
			{
																							// look at present state of INT3 line
				if (DIG_INT3_LINE == 1)														// if high last edge was rising
				{
																							// next edge will be falling
					INTCON2 |= 0x0008;														// set falling interrupt on INT3
				}
				else
				{
					event_value |= DIG_EVENT_HIGH_MASK;										// add value 1 flag
																							// next edge will be rising
					INTCON2 &= 0xFFF7;														// set rising interrupt on INT3
				}
			}
			if (DIG_config[1].ec[0].sensor_type == 0)										// if logging plain events
				dig_enqueue_event(LOG_EVENT_2A_INDEX, event_value);							// enqueue event
			else if (DIG_config[1].ec[0].sensor_type == 1)									// else if deriving values from the event
				DIG_channel[1].sub[0].event_count++;										// count event per interval
																							// else if on off independent or system
			else if ((DIG_config[1].ec[0].sensor_type == 2) || (DIG_config[1].ec[0].sensor_type == 3))
			{
				if (DIG_INT3_LINE == 1)														// record state and time of change
				{
					if (DIG_config[1].ec[0].sensor_type == 2)								// if pump is individual
						dig_update_pump_volume_in_interval(2);								// update volume pumped by this pump in interval
					if (DIG_config[1].ec[0].sensor_type == 3)								// if pump is part of a system
						dig_update_system_volume_in_interval();								// update volume pumped by system in interval
					DIG_system_pump[2].on = false;											// pump has turned off
				}
				else
				{
					if (DIG_config[1].ec[0].sensor_type == 3)								// if pump is part of a system
						dig_update_system_volume_in_interval();								// update volume pumped by system in interval
					DIG_system_pump[2].on = true;											// pump has turned on
					DIG_system_pump[2].time_on_500ms = (RTC_time_sec << 1) + RTC_half_sec;	// set its turn on time
				}
			}
		}
		event_value &= 0x07FFFFFF;															// remove edge flag from timestamp value 
																							// in case subchannel B has gone off with a falling edge at the same time
		if (_INT4IF && DIG_INT4_active)														// check INT4 flag if active
		{
			dig_set_event_debounce(1);
			DIG_channel[1].sub[1].event_flag = 1;											// set event flag
			_INT4IF = false;																// clear INT4 flag
			if ((sensor_type & DIG_EVENT_B_MASK) == 0) return;								// double check B channel is a valid event channel
																							// find which edge to record - set flag if rising
			if ((sensor_type & DIG_EVENT_B_MASK) == DIG_EVENT_B_RISE)						// if rising edge required
				event_value |= DIG_EVENT_HIGH_MASK;											// add value 1 flag to 28 bit timestamp value
			else if ((sensor_type & DIG_EVENT_B_MASK) == DIG_EVENT_B_BOTH)					// else if both edges
			{
																							// look at present state of INT4 line
				if (DIG_INT4_LINE == 1)														// if high last edge was rising
				{
																							// next edge will be falling
					INTCON2 |= 0x0010;														// set falling interrupt on INT4
				}
				else
				{
					event_value |= DIG_EVENT_HIGH_MASK;										// add value 1 flag
																							// next edge will be rising
					INTCON2 &= 0xFFEF;														// set rising interrupt on INT4
				}
			}
			if (DIG_config[1].ec[1].sensor_type == 0)										// if logging plain events
				dig_enqueue_event(LOG_EVENT_2B_INDEX, event_value);							// enqueue event
			else if (DIG_config[1].ec[1].sensor_type == 1)									// else if deriving values from the event
				DIG_channel[1].sub[1].event_count++;										// count event per interval
																							// else if on off independent or system
			else if ((DIG_config[1].ec[1].sensor_type == 2) || (DIG_config[1].ec[1].sensor_type == 3))
			{
				if (DIG_INT4_LINE == 1)														// record state and time of change
				{
					if (DIG_config[1].ec[1].sensor_type == 2)								// if pump is individual
						dig_update_pump_volume_in_interval(3);								// update volume pumped by this pump in interval
					if (DIG_config[1].ec[1].sensor_type == 3)								// if pump is part of a system
						dig_update_system_volume_in_interval();								// update volume pumped by system in interval
					DIG_system_pump[3].on = false;											// pump has turned off
				}
				else
				{
					if (DIG_config[1].ec[1].sensor_type == 3)								// if pump is part of a system
						dig_update_system_volume_in_interval();								// update volume pumped by system in interval
					DIG_system_pump[3].on = true;											// pump has turned on
					DIG_system_pump[3].time_on_500ms = (RTC_time_sec << 1) + RTC_half_sec;	// set its turn on time
				}
			}
		}
#endif
	}
	else
	{
																							// channels 1A and 1B
		if (_INT1IF && DIG_INT1_active)														// check INT1 flag if active
		{
			dig_set_event_debounce(0);
			DIG_channel[0].sub[0].event_flag = 1;											// set event flag
			_INT1IF = false;																// clear INT1 flag
			if ((sensor_type & DIG_EVENT_A_MASK) == 0) return;								// double check A channel is a valid event channel
																							// find which edge to record - set flag if rising
			if ((sensor_type & DIG_EVENT_A_MASK) == DIG_EVENT_A_RISE)						// if rising edge required
				event_value |= DIG_EVENT_HIGH_MASK;											// add value 1 flag to 28 bit timestamp value
			else if ((sensor_type & DIG_EVENT_A_MASK) == DIG_EVENT_A_BOTH)					// else if both edges
			{
																							// look at present state of INT1 line
				if (DIG_INT1_LINE == 1)														// if high last edge was rising
				{
																							// next edge will be falling
					_INT1EP = true;															// set falling interrupt on INT1
				}
				else
				{
					event_value |= DIG_EVENT_HIGH_MASK;										// add value 1 flag
																							// next edge will be rising
					_INT1EP = false;														// set rising interrupt on INT1
				}
			}
			if (DIG_config[0].ec[0].sensor_type == 0)										// if logging plain events
				dig_enqueue_event(LOG_EVENT_1A_INDEX, event_value);							// enqueue event
			else if (DIG_config[0].ec[0].sensor_type == 1)									// else if deriving values from the event
				DIG_channel[0].sub[0].event_count++;										// count event per interval
																							// else if on off independent or system
			else if ((DIG_config[0].ec[0].sensor_type == 2) || (DIG_config[0].ec[0].sensor_type == 3))
			{
				if (DIG_INT1_LINE == 1)														// record state and time of change
				{
					if (DIG_config[0].ec[0].sensor_type == 2)								// if pump is individual
						dig_update_pump_volume_in_interval(0);								// update volume pumped by this pump in interval
					if (DIG_config[0].ec[0].sensor_type == 3)								// if pump is part of a system
						dig_update_system_volume_in_interval();								// update volume pumped by system in interval
					DIG_system_pump[0].on = false;											// pump has turned off
				}
				else
				{
					if (DIG_config[0].ec[0].sensor_type == 3)								// if pump is part of a system
						dig_update_system_volume_in_interval();								// update volume pumped by system in interval
					DIG_system_pump[0].on = true;											// pump has turned on
					DIG_system_pump[0].time_on_500ms = (RTC_time_sec << 1) + RTC_half_sec;	// set its turn on time
				}
			}
		}
		event_value &= 0x07FFFFFF;															// remove edge flag from timestamp value in case 
																							// subchannel B has gone off with a falling edge at the same time
		if (_INT2IF && DIG_INT2_active)														// check INT2 flag if active
		{
			dig_set_event_debounce(1);
			DIG_channel[0].sub[1].event_flag = 1;											// set event flag
			_INT2IF = false;																// clear INT2 flag
			if ((sensor_type & DIG_EVENT_B_MASK) == 0) return;								// double check B channel is a valid event channel
																							// find which edge to record - set flag if rising
			if ((sensor_type & DIG_EVENT_B_MASK) == DIG_EVENT_B_RISE)						// if rising edge required
				event_value |= DIG_EVENT_HIGH_MASK;											// add value 1 flag to 28 bit timestamp value
			else if ((sensor_type & DIG_EVENT_B_MASK) == DIG_EVENT_B_BOTH)					// else if both edges
			{
																							// look at present state of INT2 line
				if (DIG_INT2_LINE == 1)														// if high last edge was rising
				{
																							// next edge will be falling
					_INT2EP = true;															// set falling interrupt on INT2
				}
				else
				{
					event_value |= DIG_EVENT_HIGH_MASK;										// add value 1 flag
																							// next edge will be rising
					_INT2EP = false;														// set rising interrupt on INT2
				}
			}
			if (DIG_config[0].ec[1].sensor_type == 0)										// if logging plain events
				dig_enqueue_event(LOG_EVENT_1B_INDEX, event_value);							// enqueue
			else if (DIG_config[0].ec[1].sensor_type == 1)									// else if deriving values from the event
				DIG_channel[0].sub[1].event_count++;										// count event per interval
																							// else if on off independent or system
			else if ((DIG_config[0].ec[1].sensor_type == 2) || (DIG_config[0].ec[1].sensor_type == 3))
			{
				if (DIG_INT2_LINE == 1)														// record state and time of change
				{
					if (DIG_config[1].ec[1].sensor_type == 2)								// if pump is individual
						dig_update_pump_volume_in_interval(1);								// update volume pumped by this pump in interval
					if (DIG_config[0].ec[1].sensor_type == 3)								// if pump is part of a system
						dig_update_system_volume_in_interval();								// update volume pumped by system in interval
					DIG_system_pump[1].on = false;											// pump has turned off
				}
				else
				{
					if (DIG_config[0].ec[1].sensor_type == 3)								// if pump is part of a system
						dig_update_system_volume_in_interval();								// update volume pumped by system in interval
					DIG_system_pump[1].on = true;											// pump has turned on
					DIG_system_pump[1].time_on_500ms = (RTC_time_sec << 1) + RTC_half_sec;	// set its turn on time
				}
			}
		}
	}
}

/******************************************************************************
** Function:	Task for events in an individual digital channel
**
** Notes:		dig_p_config set to address of channel config
**				dig_p_channel set to address of channel data
**				dig_index set to channel number
*/
void dig_events_task(void)
{
	int i;
	uint8 event_mask;

	dig_check_events();																			// first check all event interrupt flags
	dig_check_event_debounce();																	// check all event debounce timeouts
	if ((dig_p_config->sensor_type & DIG_EVENT_MASK) == 0x00)									// only for event channels
		return;
	
	for (i = 0; i < 2; i++)																		// for both sub channels
	{
		event_mask = (i == 0) ? DIG_EVENT_A_MASK : DIG_EVENT_B_MASK;
		if (((dig_p_config->sensor_type & event_mask) != 0x00) &&								// only if this sub-channel is event &
			(dig_p_config->ec[i].sensor_type > 0) && 											// only if sub channel enabled and event value logging
			((dig_p_config->ec[i].flags & DIG_MASK_CHANNEL_ENABLED) != 0x00))
		{
			dig_check_event_log(i);																// check event log timeouts
			if (dig_p_config->ec[i].sms_data_interval > 0)										// only if SMS logging
				dig_check_event_sms(i);															// check event sms timeouts
			dig_check_event_min_max(i);															// check event min max timeouts
			dig_check_event_alarm(i);															// check event alarms
		}
	}
}

/******************************************************************************
** Function:	Task for individual digital channel
**
** Notes:		dig_p_config set to address of channel config
**				dig_p_channel set to address of channel data
**				dig_index set to channel number
*/
void dig_channel_task(void)
{
	int i;

	dig_events_task();																						// call all event handling

																											// If the sample time is past, 
																											// must ensure that it gets updated or we'll keep
																											// the CPU awake all the time.
	if (dig_p_channel->sample_time > RTC_time_sec)															// check pulse logging - nothing to do yet
		return;
	switch (dig_p_channel->state)
	{
	case DIG_OFF:
		dig_p_channel->sample_time = SLP_NO_WAKEUP;
		break;

	case DIG_INIT:
#if (HDW_NUM_CHANNELS == 9)
		if (dig_index == 1)
		{
			if (SNS_read_digital_counters_2 || SNS_command_in_progress)	// leave sample time alone & remain awake
				break;
		}
		else
#endif
		if (SNS_read_digital_counters || SNS_command_in_progress)	// leave sample time alone & remain awake
			break;
																											// else:
		dig_p_channel->sub[0].sample_count = SNS_counters.channel_a;										// set initial sample counts
		dig_p_channel->sub[1].sample_count = SNS_counters.channel_b;

		for (i = 0; i < 2; i++)																					// set sub channel stuff
		{
			dig_p_channel->sub[i].previous_sample_count = 0;
			dig_p_channel->sub[i].log_count = 0;
			dig_p_channel->sub[i].sms_count = 0;
			dig_p_channel->sub[i].normal_alarm_count = 0;
			dig_clear_min_max(&dig_p_channel->sub[i], true);
			dig_clear_min_max(&dig_p_channel->sub[i], false);
			if ((dig_p_config->ec[i].sensor_type > 0) && 													// only if sub channel enabled and event value logging
				((dig_p_config->ec[i].flags & DIG_MASK_CHANNEL_ENABLED) != 0x00))
			{
				dig_p_channel->sub[i].event_log_time = dig_p_channel->sample_time;
				LOG_set_next_time(&dig_p_channel->sub[i].event_log_time, dig_p_config->ec[i].log_interval, false);
				dig_p_channel->sub[i].event_sms_time = dig_p_channel->sample_time;
				LOG_set_next_time(&dig_p_channel->sub[i].event_sms_time, dig_p_config->ec[i].sms_data_interval, true);
				dig_p_channel->sub[i].event_min_max_sample_time = dig_p_channel->sample_time;
				LOG_set_next_time(&dig_p_channel->sub[i].event_min_max_sample_time, dig_p_config->ec[i].min_max_sample_interval, true);
			}
			else
			{
				dig_p_channel->sub[i].event_log_time = SLP_NO_WAKEUP;
				dig_p_channel->sub[i].event_sms_time = SLP_NO_WAKEUP;
				dig_p_channel->sub[i].event_min_max_sample_time = SLP_NO_WAKEUP;
			}
			dig_p_channel->sub[i].normal_alarm_time = SLP_NO_WAKEUP;
			dig_p_channel->sub[i].derived_alarm_time = SLP_NO_WAKEUP;
		}
																											// set single channel stuff
		dig_p_channel->log_time = dig_p_channel->sample_time;
		LOG_set_next_time(&dig_p_channel->log_time, dig_p_config->log_interval, true);
		dig_p_channel->min_max_sample_time = dig_p_channel->sample_time;
		LOG_set_next_time(&dig_p_channel->min_max_sample_time, dig_p_config->min_max_sample_interval, true);

		dig_p_channel->sms_time = dig_p_channel->sample_time;
		LOG_set_next_time(&dig_p_channel->sms_time, dig_p_config->sms_data_interval, true);

		LOG_set_next_time(&dig_p_channel->sample_time, dig_p_config->sample_interval, false);				// Set time for next sample:

		dig_p_channel->state = DIG_ON;

#ifdef HDW_GPS
		if (dig_index == 0)
			dig_gps_triggered = false;
#endif
		break;

	case DIG_ON:
#if (HDW_NUM_CHANNELS == 9)
		if (dig_index == 1)
			SNS_read_digital_counters_2 = true;
		else
#endif
			SNS_read_digital_counters = true;

		dig_p_channel->state = DIG_SAMPLING;
		break;

	case DIG_SAMPLING:
#if (HDW_NUM_CHANNELS == 9)
		if (dig_index == 1)
		{
			if (SNS_read_digital_counters_2 || SNS_command_in_progress)	// leave sample time alone & remain awake
				break;
		}
		else
#endif
		if (SNS_read_digital_counters || SNS_command_in_progress)	// leave sample time alone & remain awake
			break;
																											// else:
		dig_p_channel->state = DIG_ON;
		
		// Temporarily set sub-channel sample counts to no. of pulses seen this sample.
		// After we've finished with the counts, set them to the values from the sensor PIC ready for next sample.
		dig_p_channel->sub[0].sample_count = SNS_counters.channel_a - dig_p_channel->sub[0].sample_count;
		dig_p_channel->sub[1].sample_count = SNS_counters.channel_b - dig_p_channel->sub[1].sample_count;
		dig_update_sub_channel(&dig_p_channel->sub[0]);														// accumulate number of counts during this sample interval
		dig_update_sub_channel(&dig_p_channel->sub[1]);														// into sub-channel counts:

		dig_update_totalisers();																			// totalisers and derived volumes are updated every sample
		dig_log_pulse_counts();																				// Check if logging has started & if so update values
		dig_log_sms_data();
		dig_update_min_max();
		dig_update_normal_alarm();
		dig_update_derived_alarm();

#ifdef HDW_GPS
		if ((dig_index == 0) && GPS_config.truck_mode)
		{
			if (!dig_gps_triggered)												// looking for forward flow
			{
				if (dig_p_channel->sub[0].sample_count >= GPS_TRIGGER_COUNT)
				{
					if (!GPS_is_on)
						GPS_on();

					dig_gps_triggered = true;
				}
			}
			else if (dig_p_channel->sub[0].sample_count == 0)					// flow just gone to 0 (dig_gps_triggered true)
			{
				if (!GPS_is_on)
					GPS_on();

				dig_gps_triggered = false;																		// trigger again next time flow seen
			}
		}
#endif

		if (dig_p_channel->sample_time == 0)																// have just done our midnight logs
			dig_channel_midnight_task();

		dig_p_channel->sub[0].sample_count = SNS_counters.channel_a;										// now set counter values into sample_count 
		dig_p_channel->sub[1].sample_count = SNS_counters.channel_b;										// so we get the difference next time

		LOG_set_next_time(&dig_p_channel->sample_time, dig_p_config->sample_interval, false);				// Set time for next sample:
		break;
	}
}

/******************************************************************************
** Function:	Check if any digital channels currently busy taking measurements
**
** Notes:	
*/
bool DIG_busy(void)
{
	for (dig_index = 0; dig_index < CAL_build_info.num_digital_channels; dig_index++)
	{
		if ((DIG_channel[dig_index].sample_time <= RTC_time_sec) || (DIG_channel[dig_index].state == DIG_SAMPLING))
			return true;
	}

	return false;
}

/******************************************************************************
** Function:	Digital task
**
** Notes:	
*/
void DIG_task(void)
{
	dig_system_interval_flags = DIG_ALL_SYSTEM_ONE_SHOT;								// set flags to control system flow calculations and logging per interval
	for (dig_index = 0; dig_index < CAL_build_info.num_digital_channels; dig_index++)
	{
		dig_p_channel = &DIG_channel[dig_index];
		dig_p_config = &DIG_config[dig_index];
		dig_channel_task();
	}
}

/******************************************************************************
** Function:	Start/stop logging on specified channel
**
** Notes:		May also leave transducer powered but not logging (pre-logging state)
*/
void dig_start_stop_channel(int index)
{
	DIG_config_type *p_config;
	uint8 sensor_type;
	int i;

	p_config = &DIG_config[index];
	sensor_type = p_config->sensor_type;
																					// disable event interrupts here
																					// they will be re-enabled if this channel is to be an event channel
	if (index == 0)
	{
																					// disable event interrupts INT1, 2
		_INT1IE = false;
		DIG_INT1_active = false;													// set INT1 inactive
		_INT2IE = false;
		DIG_INT2_active = false;													// set INT2 inactive
	}
#if (HDW_NUM_CHANNELS == 9)															// don't do anything for 3-ch shadow channel
	else																			// index = 1
	{
																					// disable event interrupts INT3, 4
		_INT3IE = false;
		DIG_INT3_active = false;													// set INT3 inactive
		_INT4IE = false;
		DIG_INT4_active = false;													// set INT4 inactive			
	}
#endif

																					// stop any event or interval logging until set up below
	for (i=0; i<2; i++)
	{
		DIG_channel[index].sub[i].event_time = SLP_NO_WAKEUP;
		DIG_channel[index].sub[i].event_log_time = SLP_NO_WAKEUP;
	}
	DIG_channel[index].sample_time = SLP_NO_WAKEUP;
	DIG_channel[index].log_time = SLP_NO_WAKEUP;
	DIG_channel[index].sms_time = SLP_NO_WAKEUP;
	DIG_channel[index].min_max_sample_time = SLP_NO_WAKEUP;

																					// Check if transducer needs to be on:
	if (((p_config->flags & DIG_MASK_CHANNEL_ENABLED) != 0) && (sensor_type != 0) && (LOG_state > LOG_STOPPED))
	{
		dig_start_flags[index].start = DIG_START_MASK_ALL_LOG;						// set start flags to prevent logging incomplete data first time
		dig_start_flags[index].sub_start[0] = DIG_START_MASK_ALL_SUB;
		dig_start_flags[index].sub_start[1] = DIG_START_MASK_ALL_SUB;

		for (i=0; i<2; i++)
		{
			DIG_channel[index].sub[i].derived_volume = 0;							// clear derived volume amounts
			DIG_channel[index].sub[i].last_derived_volume = 0;
			DIG_channel[index].sub[i].derived_sms_volume = 0;
			DIG_channel[index].sub[i].derived_alarm_volume = 0;
			DIG_channel[index].sub[i].derived_min_max_sample = 0;
		}
																					// Power the transducer as required
		if (index == 0)
		{
			HDW_CH1_POWER_OUT_ON = ((p_config->flags & DIG_MASK_CONTINUOUS_POWER) != 0);
#ifdef HDW_PRIMELOG_PLUS
			HDW_V_DOUBLE = ((p_config->flags & DIG_MASK_VOLTAGE_DOUBLER) != 0);
#endif
			HDW_CH1_FLOW_CTRL = ((sensor_type == DIG_TYPE_DIR_B_HIGH_REV) ||
								 (sensor_type == DIG_TYPE_DIR_B_HIGH_FWD));

			SNS_flash_firing_active = true;
			SNS_pulse_count_a_active = ((sensor_type & 0x07) != 0);
			SNS_pulse_count_b_active = ((sensor_type & 0x07) > DIG_TYPE_FWD_A);

			SNS_write_digital_config = true;
		}
#if (HDW_NUM_CHANNELS == 9)															// don't do for shadow channel on 3-ch HW
		else																		// index = 1
		{
			HDW_CH2_POWER_OUT_ON = ((p_config->flags & DIG_MASK_CONTINUOUS_POWER) != 0);
			HDW_CH2_FLOW_CTRL = ((sensor_type == DIG_TYPE_DIR_B_HIGH_REV) ||
								 (sensor_type == DIG_TYPE_DIR_B_HIGH_FWD));

			SNS_flash_firing_active = true;
			SNS_pulse_count_a_active = ((sensor_type & 0x07) != 0);
			SNS_pulse_count_b_active = ((sensor_type & 0x07) > DIG_TYPE_FWD_A);

			SNS_write_digital_config_2 = true;
		}
#endif

		DIG_channel[index].sample_time = RTC_time_sec;								// perform dig_channel_task immediately
		DIG_channel[index].state = DIG_INIT;
																					// Sensor PIC counter for channel D1A (or D2A) misses the first pulse after
																					// re-configuration (probably something to do with the prescaler).
																					// So a single interrupt on the main CPU for that flow channel 
																					// is used to detect the first pulse.
		if (index == 0)
		{
			_INT1EP = false;														// interrupt on +ve edge
			_INT1IF = false;														// goes true on first pulse
			SNS_pic1_pulse_on_a = false;											// haven't seen first pulse yet
																					// leave _INT1IE disabled. We don't need to wake up on the first pulse.
			SNS_read_digital_counters = true;
		}
#if (HDW_NUM_CHANNELS == 9)															// don't do this for shadow channel on 3-ch HW
		else																		// index = 1
		{
			// _INT3EP = false;														// interrupt on +ve edge
			INTCON2 &= 0xFFF7;														// implements the above line (compiler omits _INT3EP)
			_INT3IF = false;														// goes true on first pulse
			SNS_pic2_pulse_on_a = false;											// haven't seen first pulse yet
																					// leave _INT3IE disabled. We don't need to wake up on the first pulse.
			SNS_read_digital_counters_2 = true;
		}
#endif

		FTP_activate_retrieval_info(index * 2);										// activate retrieval data

																					// if there is a sub channel B 
		if (((p_config->flags & DIG_MASK_COMBINE_SUB_CHANNELS) == 0) && ((sensor_type & 0x07) >= DIG_TYPE_FWD_A_FWD_B))
			FTP_activate_retrieval_info((index * 2) + 1);							// activate retrieval data

		// event logging
		// NB There is an inversion in the logic between the measurement connector and the interrupt port lines
		// so when a rising edge is required in the configuration, a falling interrupt must be set
		// and vice versa
		if (index == 1)																// set up 2A and 2B
		{
#if (HDW_NUM_CHANNELS == 9)
			if ((sensor_type & DIG_EVENT_A_MASK) != 0)								// if event logging on sub-channel A
			{
				if ((sensor_type & DIG_PULSE_MASK) == 0)							// if not pulse counting on channel
				{
					if ((sensor_type & DIG_EVENT_A_MASK) == DIG_EVENT_A_FALL)		// if falling edge
						INTCON2 &= 0xFFF7;											// set rising interrupt on INT3
					else if ((sensor_type & DIG_EVENT_A_MASK) == DIG_EVENT_A_RISE)	// else if rising edge
						INTCON2 |= 0x0008;											// set falling interrupt on INT3
					else															// else must be both edges
					{
																					// look at present state of INT3 line
						if (DIG_INT3_LINE == 0)										// if low
							INTCON2 &= 0xFFF7;										// set rising interrupt on INT3
						else
							INTCON2 |= 0x0008;										// set falling interrupt on INT3
					}
					_INT3IF = false;												// clear INT3 flag
					DIG_INT3_active = true;											// set INT3 active when sleeping
					if (p_config->ec[0].sensor_type == 0)							// if plain event logging
						LOG_header_mask &= ~(1 << LOG_EVENT_2A_INDEX);				// initiate an event log header
					else if (p_config->ec[0].sensor_type == 1)						// if amount per event logging
						DIG_channel[index].sub[0].event_count = 0;					// clear event count per interval
					else if ((p_config->ec[0].sensor_type == 2) || (p_config->ec[0].sensor_type == 3))
					{
						DIG_system_pump[2 * index].on = (DIG_INT3_LINE == 0);		// set on state from condition of interrupt line
						if (DIG_system_pump[2 * index].on)							// if pump is on
						{															// set its turn on time to now
							DIG_system_pump[2 * index].time_on_500ms = (RTC_time_sec << 1) + RTC_half_sec;
							DIG_system_pump[2 * index].volume = 0;					// clear its pumped volume
						}
					}
				}
			}
			if ((sensor_type & DIG_EVENT_B_MASK) != 0)								// if event logging on sub-channel B
			{
				if ((sensor_type & DIG_PULSE_MASK) <= DIG_TYPE_FWD_A)				// if not pulse counting on channel or only channel A pulse counting
				{
					FTP_activate_retrieval_info((index * 2) + 1);					// activate retrieval data (have already done A)
					if ((sensor_type & DIG_EVENT_B_MASK) == DIG_EVENT_B_FALL)		// if falling edge
						INTCON2 &= 0xFFEF;											// set rising interrupt on INT4
					else if ((sensor_type & DIG_EVENT_B_MASK) == DIG_EVENT_B_RISE)	// else if rising edge
						INTCON2 |= 0x0010;											// set falling interrupt on INT4
					else															// else must be both edges
					{
																					// look at present state of INT4 line
						if (DIG_INT4_LINE == 0)										// if low
							INTCON2 &= 0xFFEF;										// set rising interrupt on INT4
						else
							INTCON2 |= 0x0010;										// set falling interrupt on INT4
					}
					_INT4IF = false;												// clear INT4 flag
					DIG_INT4_active = true;											// set INT4 active when sleeping
					if (p_config->ec[1].sensor_type == 0)							// if plain event logging
						LOG_header_mask &= ~(1 << LOG_EVENT_2B_INDEX);				// initiate an event log header
					else if (p_config->ec[1].sensor_type == 1)						// if amount per event logging
						DIG_channel[index].sub[1].event_count = 0;					// clear event count per interval
					else if ((p_config->ec[1].sensor_type == 2) || (p_config->ec[1].sensor_type == 3))
					{
						DIG_system_pump[(2 * index) + 1].on = (DIG_INT4_LINE == 0);	// set on state from condition of interrupt line
						if (DIG_system_pump[(2 * index) + 1].on)					// if pump is on
						{															// set its turn on time to now
							DIG_system_pump[(2 * index) + 1].time_on_500ms = (RTC_time_sec << 1) + RTC_half_sec;
							DIG_system_pump[(2 * index) + 1].volume = 0;			// clear its pumped volume
						}
					}
				}
			}
#endif
		}
		else																		// set up 1A and 1B
		{
			if ((sensor_type & DIG_EVENT_A_MASK) != 0)								// if event logging on sub-channel A
			{
				if ((sensor_type & DIG_PULSE_MASK) == 0)							// if not pulse counting on channel
				{
					if ((sensor_type & DIG_EVENT_A_MASK) == DIG_EVENT_A_FALL)		// if falling edge
						_INT1EP = false;											// set rising interrupt on INT1
					else if ((sensor_type & DIG_EVENT_A_MASK) == DIG_EVENT_A_RISE)	// else if rising edge
						_INT1EP = true;												// set falling interrupt on INT1
					else															// else must be both edges
						_INT1EP = (DIG_INT1_LINE != 0);								// look for rising/falling edge depending on state of INT1 line

					_INT1IF = false;												// clear INT1 flag
					DIG_INT1_active = true;											// set INT1 active when sleeping
					if (p_config->ec[0].sensor_type == 0)							// if plain event logging
						LOG_header_mask &= ~(1 << LOG_EVENT_1A_INDEX);				// initiate an event log header
					else if (p_config->ec[0].sensor_type == 1)						// if amount per event logging
						DIG_channel[index].sub[0].event_count = 0;					// clear event count per interval
					else if ((p_config->ec[0].sensor_type == 2) || (p_config->ec[0].sensor_type == 3))
					{
						DIG_system_pump[2 * index].on = (DIG_INT1_LINE == 0);		// set on state from condition of interrupt line
						if (DIG_system_pump[2 * index].on)							// if pump is on
						{															// set its turn on time to now
							DIG_system_pump[2 * index].time_on_500ms = (RTC_time_sec << 1) + RTC_half_sec;
							DIG_system_pump[2 * index].volume = 0;					// clear its pumped volume
						}
					}
				}
			}
			if ((sensor_type & DIG_EVENT_B_MASK) != 0)								// if event logging on sub-channel B
			{
				if ((sensor_type & DIG_PULSE_MASK) <= DIG_TYPE_FWD_A)				// if not pulse counting on channel or only channel A pulse counting
				{
					FTP_activate_retrieval_info((index * 2) + 1);					// activate retrieval data (have already done A)
					if ((sensor_type & DIG_EVENT_B_MASK) == DIG_EVENT_B_FALL)		// if falling edge
						_INT2EP = false;											// set rising interrupt on INT2
					else if ((sensor_type & DIG_EVENT_B_MASK) == DIG_EVENT_B_RISE)	// else if rising edge
						_INT2EP = true;												// set falling interrupt on INT2
					else															// else must be both edges
						_INT2EP = (DIG_INT2_LINE != 0);

					_INT2IF = false;												// clear INT2 flag
					DIG_INT2_active = true;											// set INT2 active when sleeping
					if (p_config->ec[1].sensor_type == 0)							// if plain event logging
						LOG_header_mask &= ~(1 << LOG_EVENT_1B_INDEX);				// initiate an event log header
					else if (p_config->ec[1].sensor_type == 1)						// else if counting events per interval
						DIG_channel[index].sub[1].event_count = 0;					// clear event count per interval
					else if ((p_config->ec[1].sensor_type == 2) || (p_config->ec[1].sensor_type == 3))
					{
						DIG_system_pump[(2 * index) + 1].on = (DIG_INT2_LINE == 0);	// set on state from condition of interrupt line
						if (DIG_system_pump[(2 * index) + 1].on)					// if pump is on
						{															// set its turn on time to now
							DIG_system_pump[(2 * index) + 1].time_on_500ms = (RTC_time_sec << 1) + RTC_half_sec;
							DIG_system_pump[(2 * index) + 1].volume = 0;			// clear its pumped volume
						}
					}
				}
			}
		}
	}
	else																			// transducer needs to be off
	{
#if (HDW_NUM_CHANNELS == 9)
		SNS_flash_firing_active = false;
		SNS_pulse_count_a_active = false;
		SNS_pulse_count_b_active = false;

		if (index == 1)
		{
			HDW_CH2_POWER_OUT_ON = false;
			HDW_CH2_FLOW_CTRL = false;
			SNS_write_digital_config_2 = true;
		}
		else
		{
			HDW_CH1_POWER_OUT_ON = false;
			HDW_CH1_FLOW_CTRL = false;
			SNS_write_digital_config = true;
		}
#else																				// 3-ch HW
		if (index == 0)
		{
			HDW_CH1_POWER_OUT_ON = false;
			HDW_CH1_FLOW_CTRL = false;

			SNS_flash_firing_active = false;
			SNS_pulse_count_a_active = false;
			SNS_pulse_count_b_active = false;
		
			SNS_write_digital_config = true;
		}
#endif
		
		DIG_channel[index].state = DIG_OFF;
		DIG_channel[index].sample_time = SLP_NO_WAKEUP;
	}
}

/******************************************************************************
** Function:	Start/stop logging on all digital channels
**
** Notes:	
*/
void DIG_start_stop_logging(void)
{
	for (dig_index = 0; dig_index < CAL_build_info.num_digital_channels; dig_index++)
		dig_start_stop_channel(dig_index);
}

/******************************************************************************
** Function:	Insert new headers for the specified channel
**
** Notes:	
*/
void DIG_insert_headers(int index)
{
	// Index 0 -> LOG_DIGITAL_1A_INDEX & LOG_DIGITAL_1B_INDEX plus SMS equivalents
	// Index 1 -> LOG_DIGITAL_2A_INDEX & LOG_DIGITAL_2B_INDEX plus SMS equivalents
	if (index == 0)
	{
		LOG_header_mask &= ~((1 << LOG_DIGITAL_1A_INDEX) | (1 << LOG_DIGITAL_1B_INDEX));
		LOG_sms_header_mask &= ~((1 << LOG_DIGITAL_1A_INDEX) | (1 << LOG_DIGITAL_1B_INDEX));
		LOG_derived_header_mask &= ~((1 << LOG_DIGITAL_1A_INDEX) | (1 << LOG_DIGITAL_1B_INDEX));
		LOG_derived_sms_header_mask &= ~((1 << LOG_DIGITAL_1A_INDEX) | (1 << LOG_DIGITAL_1B_INDEX));
	}
	else	// index 1
	{
		LOG_header_mask &= ~((1 << LOG_DIGITAL_2A_INDEX) | (1 << LOG_DIGITAL_2B_INDEX));
		LOG_sms_header_mask &= ~((1 << LOG_DIGITAL_2A_INDEX) | (1 << LOG_DIGITAL_2B_INDEX));
		LOG_derived_header_mask &= ~((1 << LOG_DIGITAL_2A_INDEX) | (1 << LOG_DIGITAL_2B_INDEX));
		LOG_derived_sms_header_mask &= ~((1 << LOG_DIGITAL_2A_INDEX) | (1 << LOG_DIGITAL_2B_INDEX));
	}
}

/******************************************************************************
** Function:	Configure digital channel
**
** Notes:		May go into pre-logging or logging state
*/
void DIG_configure_channel(int index)
{
#if (HDW_NUM_CHANNELS == 3)		// variables needed for shadow channel:
	DIG_config_type *p0;
	DIG_config_type *p1;
#endif

	if (((DIG_config[index].sensor_type & DIG_PULSE_MASK) == DIG_TYPE_DIR_B_HIGH_FWD) ||
		((DIG_config[index].sensor_type & DIG_PULSE_MASK) == DIG_TYPE_DIR_B_HIGH_REV))
	{
		DIG_config[index].fcal_b = DIG_config[index].fcal_a;
	}

	if ((DIG_config[index].flags & DIG_MASK_CONTINUOUS_POWER) != 0)	// don't allow voltage doubler
		DIG_config[index].flags &= ~DIG_MASK_VOLTAGE_DOUBLER;		// can't have in continuous power mode

	SNS_ff_table_index = 0;

#if (HDW_NUM_CHANNELS == 9)

	if (index == 1)
	{
		SNS_write_ff_table_2 = true;
		SNS_write_ff_width_2 = true;
	}
	else
	{
		SNS_write_ff_table = true;
		SNS_write_ff_width = true;
	}

	dig_start_stop_channel(index);

	// When values are first logged with new config, ensure a header is inserted.
	DIG_insert_headers(index);

#else	// 3-ch HW

	// only write FF table and FF width if main channel, not shadow.
	SNS_write_ff_table = (index == 0);
	SNS_write_ff_width = (index == 0);

	// 3-ch HW can have digital shadow channel on ch. 2. Need to copy some of main channel config
	// to shadow & start or stop it logging
	p0 = &DIG_config[0];
	p1 = &DIG_config[1];

	if ((p0->flags & DIG_MASK_CHANNEL_ENABLED) == 0)	// main disabled, so disable shadow
		p1->flags &= ~DIG_MASK_CHANNEL_ENABLED;

	p1->flags &= ~(DIG_MASK_CONTINUOUS_POWER | DIG_MASK_VOLTAGE_DOUBLER);
	p1->flags |= p0->flags & (DIG_MASK_CONTINUOUS_POWER | DIG_MASK_VOLTAGE_DOUBLER);

	p1->sensor_type = p0->sensor_type & DIG_PULSE_MASK;		// no events on shadow
	p1->pit_min_period = 0;
	p1->sensor_index = p0->sensor_index;
	p1->ff_pulse_width_x32us = p0->ff_pulse_width_x32us;
	memcpy(p1->ff_period, p0->ff_period, sizeof(p0->ff_period));

	dig_start_stop_channel(index);
	DIG_insert_headers(index);
	
	// if we've just changed a main channel, change its shadow
	if (index == 0)
	{
		dig_start_stop_channel(1);
		DIG_insert_headers(1);
	}
#endif
}

/******************************************************************************
** Function:	Print digital config to buffer
**
** Notes:		Returns no. of chars written.
*/
int DIG_print_config(int index, char * s)
{
	DIG_config_type *p;
	int len;
	int shadow;
	uint8 rate;

#if (HDW_NUM_CHANNELS == 9)																		// 9 channel
	shadow = 0;																					// no shadow
#else																							// 3-ch
	shadow = (index > 0);
#endif

	p = &DIG_config[index];

	len = sprintf(s, "%d,%u,%d,%d,%d,%d,%u,%u,%u,",
		((p->flags & DIG_MASK_CHANNEL_ENABLED) != 0) ? 1 : 0,
		p->sensor_type,
		((p->flags & DIG_MASK_CONTINUOUS_POWER) != 0) ? 1 : 0,
		((p->flags & DIG_MASK_VOLTAGE_DOUBLER) != 0) ? 1 : 0,
		((p->flags & DIG_MASK_COMBINE_SUB_CHANNELS) != 0) ? 1 : 0,
		((p->flags & DIG_MASK_MESSAGING_ENABLED) != 0) ? 1 : 0,
		p->sample_interval, p->log_interval, p->min_max_sample_interval);
			
	len += sprintf(&s[len], "%u,%u,%u,",														// Do up to ff pulse width (inclusive):
			p->sms_data_interval, p->event_log_min_period, p->pit_min_period);
	len += STR_print_float(&s[len], p->fcal_a);
	s[len++] = ',';
	len += STR_print_float(&s[len], p->fcal_b);

	rate = p->rate_enumeration;																	// combine rate enumeration and derived volume enable flag
	if ((p->flags & DIG_MASK_DERIVED_VOL_ENABLED) != 0)
		rate |= 0x04;
	len += sprintf(&s[len], ",%u,%u,%02X,%u,%u,%u",
			rate, p->sensor_index, p->sms_message_type,
			p->description_index, p->units_index, p->ff_pulse_width_x32us);

	for (index = 0; index < 8; index++)															// Flash fire frequencies:
		len += sprintf(&s[len], ",%u", p->ff_period[index]);

	len += sprintf(&s[len], ",%d", shadow);

	return len;
}

/******************************************************************************
** Function:	return true if specified unit is not a system unit or is first system unit
**
** Notes:		for use by DG_print_immediate_values
*/
bool dig_first_in_system(int chan, int sub)
{
	int i = 9999;

	if (DIG_config[chan].ec[sub].sensor_type != 3)												// can print value if this is not a system pump
		return true;
																								// find first enabled pump in a system if exists
	if ((DIG_config[0].ec[0].sensor_type == 3) && 
		((DIG_config[0].ec[0].flags & DIG_MASK_CHANNEL_ENABLED) != 0x00))
		i = 0;
	else if ((DIG_config[0].ec[1].sensor_type == 3) &&
			 ((DIG_config[0].ec[1].flags & DIG_MASK_CHANNEL_ENABLED) != 0x00))
		i = 1;
#if (HDW_NUM_CHANNELS == 9)
	else if ((DIG_config[1].ec[0].sensor_type == 3) &&
			 ((DIG_config[1].ec[0].flags & DIG_MASK_CHANNEL_ENABLED) != 0x00))
		i = 2;
	else if ((DIG_config[1].ec[1].sensor_type == 3) &&
			 ((DIG_config[1].ec[1].flags & DIG_MASK_CHANNEL_ENABLED) != 0x00))
		i = 3;
#endif
	return (i == ((2 * chan) + sub));															// can print value if this is the first enabled system pump
}

/******************************************************************************
** Function:	return rate value from event sensors
**
** Notes:		common function for immediate and print immediate functions
*/
float dig_get_event_logging_rate(int chan, int sub)
{
	float value = 0;
	DIG_sub_channel_type * p_subchannel;
	DIG_sub_event_config_type * p_subconfig;

	p_subchannel = &DIG_channel[chan].sub[sub];
	p_subconfig = &DIG_config[chan].ec[sub];

	if (p_subconfig->sensor_type == 1)															// if event value logging
	{
		value = (float)p_subchannel->previous_event_count * p_subconfig->cal;					// calculate amount in previous interval
		value = DIG_volume_to_rate_enum(value, 													// convert to rate
										p_subconfig->log_interval, 
										p_subconfig->rate_enumeration);
	}
	else if (p_subconfig->sensor_type == 2)														// else if individual on/off pump
		value = dig_get_immediate_pump_flow(chan, sub);
	else if (p_subconfig->sensor_type == 3)														// else if on/off pump system
		value = dig_get_immediate_pump_flow(chan, sub);
																								// more added here
	return value;
}

/******************************************************************************
** Function:	Print immediate flow or event port values to STR_buffer
**
** Notes:		for 3 channel, index will be 0, for 9 channel, index may be 0 or 1
*/
void DIG_print_immediate_values(int index, bool units_as_strings)
{
	float value;
	int i, v;
	char * filename_ptr;
	char * line_ptr;

	DIG_config_type * p_config;
	DIG_channel_type * p_channel;

	p_config = &DIG_config[index];
	p_channel = &DIG_channel[index];																		// Create string in STR_buffer, use STR_buffer[200] for filename, 
																											// read contents of line into STR_buffer[400]
	filename_ptr = &STR_buffer[200];
	line_ptr = &STR_buffer[400];
																											// Digital 1A or 2A:
	if (((p_config->flags & DIG_MASK_CHANNEL_ENABLED) != 0) && (LOG_state > LOG_STOPPED))
	{	
		if ((p_config->sensor_type & DIG_EVENT_A_MASK) > DIG_EVENT_A_NONE)									// event sensor subchannel A
		{
			if ((p_config->ec[0].sensor_type > 0) && 														// if enabled and event value logging
				((p_config->ec[0].flags & DIG_MASK_CHANNEL_ENABLED) != 0x00))
			{
				if (dig_first_in_system(index, 0))															// if can print it if a system unit or non system
				{
					value = dig_get_event_logging_rate(index, 0);											// get rate from event sensor types
					if (units_as_strings)
					{
						i = sprintf(STR_buffer, "%s=", LOG_channel_id[(2 * index) + 1]);
						i += STR_print_float(&STR_buffer[i], value);
						CFS_read_line((char *)CFS_config_path, (char *)CFS_units_name, p_config->ec[0].output_units_index + 1, &STR_buffer[400], 8);
						i += sprintf(&STR_buffer[i], "%s,",&STR_buffer[400]);
					}
					else
					{
						i = STR_print_float(STR_buffer, value);
						i += sprintf(&STR_buffer[i], ",%u,", p_config->ec[0].output_units_index);
					}
				}
				else
				{
					strcpy(STR_buffer, units_as_strings ? "," : ",,");
					i = strlen(STR_buffer);
				}
			}
			else																							// else pure event logging
			{
				if (index == 0)																				// digital event 1A
					v =(DIG_INT1_LINE == 0) ? 1 : 0;														// look at present state of INT1 line - value is INVERSE state of line
				else																						// digital event 2A
					v =(DIG_INT3_LINE == 0) ? 1 : 0;														// look at present state of INT3 line - value is INVERSE state of line
				if (units_as_strings)
				{
					sprintf(filename_ptr, "ALM%s.TXT", LOG_channel_id[(index * 2) + 1]);					// need to use text from ALMxxx.TXT file for 0 or 1
					if (v == 1)																				// line 2 is text for high value, line 3 is text for low value
					{
						if (CFS_read_line((char *)CFS_config_path, filename_ptr, 2, line_ptr, 16) < 1)
							i = sprintf(STR_buffer, "%s=1,", LOG_channel_id[(2 * index) + 1]);				// default to numeric if no file or line
						else
							i = sprintf(STR_buffer, "%s=%s,", LOG_channel_id[(2 * index) + 1], line_ptr);
					}
					else
					{
						if (CFS_read_line((char *)CFS_config_path, filename_ptr, 3, line_ptr, 16) < 1)
							i = sprintf(STR_buffer, "%s=0,", LOG_channel_id[(2 * index) + 1]);				// default to numeric if no file or line
						else
							i = sprintf(STR_buffer, "%s=%s,", LOG_channel_id[(2 * index) + 1], line_ptr);
					}
				}
				else
					i = sprintf(STR_buffer, "%d,,", v);
			}
		}
		else if ((p_channel->sample_time != 0) &&
				 ((p_config->sensor_type & DIG_PULSE_MASK) >= DIG_TYPE_FWD_A))
		{
			value = p_channel->sub[0].previous_sample_count * p_config->fcal_a;
			if ((p_config->flags & DIG_MASK_COMBINE_SUB_CHANNELS) != 0)										// Combine pulse streams if necessary
				value = DIG_combine_flows(p_config->sensor_type, value,
										  p_channel->sub[1].previous_sample_count * p_config->fcal_b);
			if (units_as_strings)
			{
				i = sprintf(STR_buffer, "%s=", LOG_channel_id[(2 * index) + 1]);
				i += STR_print_float(&STR_buffer[i], DIG_volume_to_rate_enum(value, p_config->sample_interval, p_config->rate_enumeration));
				CFS_read_line((char *)CFS_config_path, (char *)CFS_units_name, p_config->units_index + 1, &STR_buffer[400], 8);
				i += sprintf(&STR_buffer[i], "%s,",&STR_buffer[400]);
			}
			else
			{
				i = STR_print_float(STR_buffer, DIG_volume_to_rate_enum(value, p_config->sample_interval, p_config->rate_enumeration));
				i += sprintf(&STR_buffer[i], ",%u,", p_config->units_index);
			}
		}
		else
		{
			strcpy(STR_buffer, units_as_strings ? "," : ",,");
			i = strlen(STR_buffer);
		}
	
		if ((p_config->flags & DIG_MASK_COMBINE_SUB_CHANNELS) == 0)											// Print a value for B as well?
		{
			if ((p_config->sensor_type & DIG_EVENT_B_MASK) > DIG_EVENT_B_NONE)
			{
				if ((p_config->ec[1].sensor_type > 0) && 													// if enabled and event value logging
					((p_config->ec[1].flags & DIG_MASK_CHANNEL_ENABLED) != 0x00))
				{
					if (dig_first_in_system(index, 1))														// if can print it if a system unit or non system
					{
						value = dig_get_event_logging_rate(index, 1);										// get rate from event sensor types
						if (units_as_strings)
						{
							i += sprintf(&STR_buffer[i], "%s=", LOG_channel_id[(2 * index) + 2]);
							i += STR_print_float(&STR_buffer[i], value);
							CFS_read_line((char *)CFS_config_path, (char *)CFS_units_name, p_config->ec[1].output_units_index + 1, &STR_buffer[400], 8);
							i += sprintf(&STR_buffer[i], "%s,",&STR_buffer[400]);
						}
						else
						{
							i += STR_print_float(&STR_buffer[i], value);
							i += sprintf(&STR_buffer[i], ",%u,", p_config->ec[1].output_units_index);
						}
					}
					else
						strcat(STR_buffer, units_as_strings ? "," : ",,");
				}
				else																						// else pure event logging
				{
					if (index == 0)																			// digital event 1B
						v =(DIG_INT2_LINE == 0) ? 1 : 0;													// look at present state of INT2 line - value is INVERSE state of line
					else																					// digital event 2B
						v =(DIG_INT4_LINE == 0) ? 1 : 0;													// look at present state of INT4 line - value is INVERSE state of line
					if (units_as_strings)
					{
						sprintf(filename_ptr, "ALM%s.TXT", LOG_channel_id[(index * 2) + 2]);				// need to use text from ALMxxx.TXT file for 0 or 1
						if (v == 1)																			// line 2 is text for high value, line 3 is text for low value
						{
							if (CFS_read_line((char *)CFS_config_path, filename_ptr, 2, line_ptr, 16) < 1)
								i += sprintf(&STR_buffer[i], "%s=1,", LOG_channel_id[(2 * index) + 2]);		// default to numeric if no file or line
							else
								i += sprintf(&STR_buffer[i], "%s=%s,", LOG_channel_id[(2 * index) + 2], line_ptr);
						}
						else
						{
							if (CFS_read_line((char *)CFS_config_path, filename_ptr, 3, line_ptr, 16) < 1)
								i += sprintf(&STR_buffer[i], "%s=0,", LOG_channel_id[(2 * index) + 2]);		// default to numeric if no file or line
							else
								i += sprintf(&STR_buffer[i], "%s=%s,", LOG_channel_id[(2 * index) + 2], line_ptr);
						}
					}
					else
						i += sprintf(&STR_buffer[i], "%d,,", v);
				}
			}
			else if ((p_channel->sample_time != 0) &&
					 ((p_config->sensor_type & DIG_PULSE_MASK) >= DIG_TYPE_FWD_A_FWD_B))
			{
				value = p_channel->sub[1].previous_sample_count * p_config->fcal_b;
	
				if (units_as_strings)																		// have already read units string above
				{
					i += sprintf(&STR_buffer[i], "%s=", LOG_channel_id[(2 * index) + 2]);
					i += STR_print_float(&STR_buffer[i], DIG_volume_to_rate_enum(value, p_config->sample_interval, p_config->rate_enumeration));
					i += sprintf(&STR_buffer[i], "%s,", &STR_buffer[400]);
				}
				else
				{
					i += STR_print_float(&STR_buffer[i], DIG_volume_to_rate_enum(value, p_config->sample_interval, p_config->rate_enumeration));
					i += sprintf(&STR_buffer[i], ",%u,", p_config->units_index);
				}
			}
			else
				strcat(STR_buffer, units_as_strings ? "," : ",,");
		}
		else
			strcat(STR_buffer, units_as_strings ? "," : ",,");
	}
	else																									// no flow values or units
		strcpy(STR_buffer, units_as_strings ? ",," : ",,,,");
}

/******************************************************************************
** Function:	Print immediate derived values to STR_buffer
**
** Notes:		
*/
void DIG_print_immediate_derived_values(int index, bool units_as_strings)
{
	float value;
	int i;
	char * filename_ptr;
	char * line_ptr;

	DIG_config_type * p_config;
	DIG_channel_type * p_channel;

	p_config = &DIG_config[index];
	p_channel = &DIG_channel[index];																		// Create string in STR_buffer, use STR_buffer[200] for filename, 
																											// read contents of line into STR_buffer[400]
	filename_ptr = &STR_buffer[200];
	line_ptr = &STR_buffer[400];
																											// Digital 1A or 2A:
	if (((p_config->flags & DIG_MASK_CHANNEL_ENABLED) != 0) && 
		((p_config->flags & DIG_MASK_DERIVED_VOL_ENABLED) != 0) && 
		(LOG_state > LOG_STOPPED))
	{	
		if ((p_config->sensor_type & DIG_EVENT_A_MASK) > DIG_EVENT_A_NONE)									// event sensor subchannel A
			strcat(STR_buffer, units_as_strings ? "," : ",,");												// no derived values
		else if ((p_channel->sample_time != 0) &&
				 ((p_config->sensor_type & DIG_PULSE_MASK) >= DIG_TYPE_FWD_A))
		{
			value = p_channel->sub[0].last_derived_volume;
			if ((p_config->flags & DIG_MASK_COMBINE_SUB_CHANNELS) != 0)										// Add volumes if necessary
				value += p_channel->sub[1].last_derived_volume;
			if (units_as_strings)
			{
				i = sprintf(STR_buffer, "%s=", LOG_channel_id[(2 * index) + 12]);
				i += STR_print_float(&STR_buffer[i], value);
				CFS_read_line((char *)CFS_config_path, (char *)CFS_volunits_name, p_channel->sub[0].totaliser.units_enumeration + 1, &STR_buffer[400], 8);
				i += sprintf(&STR_buffer[i], "%s,",&STR_buffer[400]);
			}
			else
			{
				i = STR_print_float(STR_buffer, value);
				i += sprintf(&STR_buffer[i], ",%u,", p_channel->sub[0].totaliser.units_enumeration);
			}
		}
		else
		{
			strcpy(STR_buffer, units_as_strings ? "," : ",,");
			i = strlen(STR_buffer);
		}
	
		if ((p_config->flags & DIG_MASK_COMBINE_SUB_CHANNELS) == 0)											// Print a value for B as well?
		{
			if ((p_config->sensor_type & DIG_EVENT_B_MASK) > DIG_EVENT_B_NONE)
			{
				strcat(STR_buffer, units_as_strings ? "," : ",,");											// no derived values
			}
			else if ((p_channel->sample_time != 0) &&
					 ((p_config->sensor_type & DIG_PULSE_MASK) >= DIG_TYPE_FWD_A_FWD_B))
			{
				value = p_channel->sub[1].last_derived_volume;
	
				if (units_as_strings)																		// have already read units string above
				{
					i += sprintf(&STR_buffer[i], "%s=", LOG_channel_id[(2 * index) + 13]);
					i += STR_print_float(&STR_buffer[i], value);
					i += sprintf(&STR_buffer[i], "%s,", &STR_buffer[400]);
				}
				else
				{
					i += STR_print_float(&STR_buffer[i], value);
					i += sprintf(&STR_buffer[i], ",%u,", p_channel->sub[1].totaliser.units_enumeration);
				}
			}
			else
				strcat(STR_buffer, units_as_strings ? "," : ",,");
		}
		else
			strcat(STR_buffer, units_as_strings ? "," : ",,");
	}
	else																									// no flow values or units
		strcpy(STR_buffer, units_as_strings ? ",," : ",,,,");
}

/******************************************************************************
** Function:	return immediate value of given channel in required units
**
** Notes:		returns 0 or 1 for event channels unless deriving data
**				only called by control output triggering on volume or threshold
**				if per_second is required, adjusts rate with rate enumerator
*/
float DIG_immediate_value(uint8 channel_id, bool per_second)
{
	float value = 0;
	uint8 index = (channel_id & 0x03) - 1;
	uint8 sub_index = (channel_id & 0x20) >> 5;

	if (index >= DIG_NUM_CHANNELS)																				// check for valid channels and sub channels
		return 0;
	if (sub_index > 1)
		return 0;

	dig_index = index;																							// needed by dig_volume_to_rate
	if (((DIG_config[index].flags & DIG_MASK_CHANNEL_ENABLED) != 0) && (LOG_state > LOG_STOPPED))				// if enabled and logging
	{	
		if (sub_index == 0)																						// if sub channel A
		{
			if ((DIG_config[index].sensor_type & DIG_EVENT_A_MASK) > DIG_EVENT_A_NONE)							// if event logging
			{
				if (index == 0)																					// digital event 1A
				{
					if (DIG_config[0].ec[0].sensor_type == 0)													// if not event value logging
						value = (DIG_INT1_LINE == 0) ? 1.0f : 0.0f;												// look at present state of INT1 line - value is INVERSE state of line
					else
					{
						value = dig_get_event_logging_rate(0, 0);												// else get rate from event sensor types in units per enumeration
						if (per_second)
						{
							if (DIG_config[0].ec[0].rate_enumeration == 2)										// if per minute
								value /= 60;																	// get rate per second
							else if (DIG_config[0].ec[0].rate_enumeration == 3)									// if per hour
								value /= 3600;																	// get rate per second
						}
					}
				}
				else																							// digital event 2A
				{
					if (DIG_config[1].ec[0].sensor_type == 0)													// if not event value logging
						value =(DIG_INT3_LINE == 0) ? 1.0f : 0.0f;												// look at present state of INT3 line - value is INVERSE state of line
					else
					{
						value = dig_get_event_logging_rate(1, 0);												// else get rate from event sensor types
						if (per_second)
						{
							if (DIG_config[1].ec[0].rate_enumeration == 2)										// if per minute
								value /= 60;																	// get rate per second
							else if (DIG_config[1].ec[0].rate_enumeration == 3)									// if per hour
								value /= 3600;																	// get rate per second
						}
					}
				}
			}
			else if ((DIG_channel[index].sample_time != 0) &&													// else if sampling
					 ((DIG_config[index].sensor_type & DIG_PULSE_MASK) >= DIG_TYPE_FWD_A))						// and if pulse sensor
			{
				if ((channel_id & 0x08) == 0)																	// if normal value required
				{ 
					value = DIG_channel[index].sub[0].previous_sample_count * DIG_config[index].fcal_a;
																												// Combine pulse streams if necessary
					if ((DIG_config[index].flags & DIG_MASK_COMBINE_SUB_CHANNELS) != 0)
						value = DIG_combine_flows(DIG_config[index].sensor_type, value,
												  DIG_channel[index].sub[1].previous_sample_count * DIG_config[index].fcal_b);
					value = DIG_volume_to_rate_enum(value, DIG_config[index].sample_interval, 1);
				}
				else																							// else derived volume is required
				{
					value = DIG_channel[index].sub[0].last_derived_volume;
					if ((DIG_config[index].flags & DIG_MASK_COMBINE_SUB_CHANNELS) != 0)							// add volumes if required
						value += DIG_channel[index].sub[1].last_derived_volume;
				}
			}
		}
		else																									// else sub channel B
		{
			if ((DIG_config[index].sensor_type & DIG_EVENT_B_MASK) > DIG_EVENT_B_NONE)							// if event logging
			{
				if (index == 0)																					// digital event 1B
				{
					if (DIG_config[0].ec[1].sensor_type == 0)													// if not event value logging
						value =(DIG_INT2_LINE == 0) ? 1.0f : 0.0f;												// look at present state of INT2 line - value is INVERSE state of line
					else
					{
						value = dig_get_event_logging_rate(0, 1);												// else get rate from event sensor types
						if (per_second)
						{
							if (DIG_config[0].ec[1].rate_enumeration == 2)										// if per minute
								value /= 60;																	// get rate per second
							else if (DIG_config[0].ec[1].rate_enumeration == 3)									// if per hour
								value /= 3600;																	// get rate per second
						}
					}
				}
				else																							// digital event 2B
				{
					if (DIG_config[1].ec[1].sensor_type == 0)													// if not event value logging
						value =(DIG_INT4_LINE == 0) ? 1.0f : 0.0f;												// look at present state of INT4 line - value is INVERSE state of line
					else
					{
						value = dig_get_event_logging_rate(1, 1);												// else get rate from event sensor types
						if (per_second)
						{
							if (DIG_config[1].ec[1].rate_enumeration == 2)										// if per minute
								value /= 60;																	// get rate per second
							else if (DIG_config[1].ec[1].rate_enumeration == 3)									// if per hour
								value /= 3600;																	// get rate per second
						}
					}
				}
			}
			else if ((DIG_channel[index].sample_time != 0) &&													// else if sampling
					 ((DIG_config[index].sensor_type & DIG_PULSE_MASK) >= DIG_TYPE_FWD_A_FWD_B))				// and if pulse sensor
			{
				if ((channel_id & 0x08) == 0)																	// if normal value required
				{ 
					value = DIG_channel[index].sub[1].previous_sample_count * DIG_config[index].fcal_b;	
					value = DIG_volume_to_rate_enum(value, DIG_config[index].sample_interval, 1);
				}
				else																							// else sub channel B derived volume is required
					value = DIG_channel[index].sub[1].last_derived_volume;
			}
		}
	}

	return value;	
}

/******************************************************************************
** Function:	Synchronise digital timers to time of day
**
** Notes:	
*/
void DIG_synchronise(void)
{
	int index;

	// for each channel present
	for (index = 0; index < CAL_build_info.num_digital_channels; index++)
	{
		DIG_channel[index].state = DIG_OFF;
		dig_start_stop_channel(index);
	}
}

#endif
