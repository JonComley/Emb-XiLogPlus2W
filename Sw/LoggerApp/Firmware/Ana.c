/******************************************************************************
** File:	Ana.c
**
** Notes:	Analogue channels
**
** Voltage channels: amplifier gain and offset from calibration file to get x in V.
** Current channels: amplifier gain and offset from calibration file to get x in mA.
** External pressure channels: amplifier gain from calibration file to get x in mV.
** All external transducers: value = p0 + ((x - e0) / (e1 - e0)) * (p1 - p0)) in physical units
**
** Internal transducer: gain, offset and e0 read from calibration file. e1 = bar rating.
** x = N - N0, where N = ADC value for signal, N0 = ADC value for 0.
** y = (e0 * x^2) + (gain * x) + offset, in Bar
** value = y * p1 / e1 in user-selected physical units
**
** v2.49 250111 PB channel start calls PDU_synchronise_batch() and channel stop calls PDU_disable_batch()
**
** V3.03 201111 PB Waste Water - add calculations for derived data and improve functions to return it
**
** V3.07 270112 PB changed format of FTABLE.CAL for non linear input data - ana_calc_derived_value()
**
** V3.11 240412 PB correction to extracting second value from line in conversion table - ana_get_ftable_values()
**
** V3.12 080712 PB correction to calculations for derived data, both formulae and ftable
**
** V3.14 130612 PB subtracting higher from lower in FTABLE.CAL depth to flow conversion - ana_calc_derived_value()
**				   take conversion values from config memory rather than file system
**				   add function to read conversion data from file system at config time - ANA_retrieve_derived_conversion_data()
**
** V3.16 210612 PB derived_config data only held for main channels - config data held for main and shadow channels - ANA_retrieve_derived_conversion_data()
**				   allow shadow channels to use derived config data in 3 channel hardware - ana_calc_derived_value()
**
** V3.22 280513 PB DEL188: error in line 673 corrected to read point_value from file
**
** V3.23 310513 PB add ANA_insert_derived_header()
**
** V3.25 110613 PB test for insertion of derived header independent of ordinary header
**
** V3.36 140114 PB remove calls to FTP_deactivate_retrieval_info()
**
** V4.00 220114 PB disable if HDW_GPS defined
*/

#include <float.h>

#include "custom.h"
#include "compiler.h"
#include "math.h"
#include "MDD File System/SD-SPI.h"
#include "MDD File System/FSIO.h"

#include "cfs.h"
#include "str.h"
#include "HardwareProfile.h"
#include "Sns.h"
#include "Rtc.h"
#include "Log.h"
#include "Tim.h"
#include "Cal.h"
#include "Slp.h"
#include "Pdu.h"
#include "Msg.h"
#include "Alm.h"
#include "ftp.h"
#include "Com.h"
#include "Usb.h"

#define extern
#include "Ana.h"
#undef extern

#ifndef HDW_GPS

// Get mask for an analogue channel (in ana_adc_read_required_mask)
#define ANA_MASK(INDEX)	(1 << (INDEX))

// ADC states:
#define ANA_ADC_IDLE			0
#define ANA_ADC_POWERING		1
#define ANA_ADC_BOOSTING		2
#define ANA_ADC_GET_VREF		3
#define ANA_ADC_GET_ZERO		4
#define ANA_ADC_SWITCHING		5
#define ANA_ADC_GET_SIGNAL		6

// Hassle reduction variables:
int ana_index;
ANA_channel_type *ana_p_channel;
ANA_config_type  *ana_p_config;

uint8 ana_flags;
#define ANA_BOOST_MASK		_B00000001
#define ANA_GOT_VREF_MASK	_B00000010

uint8 ana_adc_state;
uint8 ana_adc_read_required_mask;
uint8 ana_adc_read_in_progress_mask;
uint8 ana_adc_index;

uint16 ana_timer;
uint16 ana_boost_timer_x20ms;

#if (HDW_NUM_CHANNELS == 9)
const uint8 ana_adc_address[ANA_NUM_CHANNELS] =
{
	// Sensor PIC 1:
	SNS_ADC_ADDRESS_VOLTAGE_1,
	SNS_ADC_ADDRESS_VOLTAGE_2,
	SNS_ADC_ADDRESS_VOLTAGE_3,
	SNS_ADC_ADDRESS_VOLTAGE_4,

	// Sensor PIC 2:
	SNS_ADC_ADDRESS_CURRENT_1,
	SNS_ADC_ADDRESS_CURRENT_2,
	SNS_ADC_ADDRESS_CURRENT_3
};
#endif

/******************************************************************************
** Function:	Check if analogue channel exists
**
** Notes:	
*/
bool ANA_channel_exists(int index)
{
	char c;

	if (index >= ANA_NUM_CHANNELS)
		return false;
	
	// channel does not exist if off the end of the string, or type = '0'.
	c = CAL_build_info.analogue_channel_types[index];
	return ((c != '\0') && (c != '0'));
}

/******************************************************************************
** Function:	Set pending ADC read
**
** Notes:
*/
void ANA_start_adc_read(int index)
{
	uint8 mask;

#if (HDW_NUM_CHANNELS == 9)				// 9-channel: read specified channel
	mask = ANA_MASK(index);
#else									// 3-channel: read main and shadow
	// MUST set shadow to read as well as main, otherwise ANA_get_adc_values will
	// report conversion complete before it has actually started.
	// Index 0 or 2: mask = 0101. Index 1 or 3: mask = 1010
	mask = ((index & 0x01) == 0) ? _B00000101 : _B00001010;
#endif

	ana_adc_read_required_mask |= mask;
	ana_adc_read_required_mask &= ANA_MAX_CHANNEL_MASK;

	if ((ANA_config[index].flags & ANA_MASK_POWER_TRANSDUCER) != 0)
		ana_flags |= ANA_BOOST_MASK;
}

/******************************************************************************
** Function:	Check if immediate ADC values have been read for this channel
**
** Notes:		Returns true when done for this channel
*/
bool ANA_get_adc_values(int index)
{
	return ((ana_adc_read_required_mask & (1 << index)) == 0);
}

/******************************************************************************
** Function:	get one or two values from the line in the ftable file
**
** Notes:		returns false if no such line
*/
bool ana_get_ftable_values(uint8 channel, int line, float * input, float * output)
{
	char * p_line = STR_buffer;
	char filename[16];

	sprintf(filename, "%u%s", channel+1, (char *)CFS_ftable_name);
	if (CFS_read_line((char *)CFS_config_path, filename, line, STR_buffer, 140) > 0)
	{
		sscanf(STR_buffer, "%f", input);															// get input value
		*output = 0.0F;																				// second value is after comma if it exists
		while((*p_line != ',') && (*p_line != '\0'))												// find comma or end of line
		{
			p_line++;
		};
		if (*p_line == '\0')																		// if no comma 
			return true;
		p_line++;																					// else skip comma
		sscanf(p_line, "%f", output);																// get output value
		return true;
	}
	else
		return false;
}

/******************************************************************************
** Function:	Calculate a derived value given input value and derived type
**
** Notes:		Uses derived depth to flow conversion config data
**				Only called when derived data flag is set for current channel
**              May be expanded in future for other derived data
*/
float ana_calc_derived_value(int index, float value, uint8 derived_type)
{
	int   point;
	int	  table_points;
	float scale, remainder;
	float lower_input;
	float upper_input;
	float lower_value;
	float upper_value;
	float result = 0;

	ANA_derived_config_type * p_pointer;

#if (HDW_NUM_CHANNELS == 3)
	if (index > 3)
		return 0;
	if ((index >1) && (index < 4))																// check for shadow channels - use same conversion as main
		index -= 2;
#endif

	p_pointer = &ANA_derived_config[index];

	if (value <= 0)																				// reject zero or negative input
		return 0;
	if (value > p_pointer->max_value)															// limit input value
		value = p_pointer->max_value;
	else if (value < p_pointer->min_value)
		value = p_pointer->min_value;
	if (derived_type == ANA_DEPTH_TO_FLOW_RECTANGULAR)											// check on configured calculation
		result = p_pointer->K_value * pow(value + p_pointer->k_value, 1.5);						// calculate using rectangular formula
	else if (derived_type == ANA_DEPTH_TO_FLOW_V_NOTCH)
		result = p_pointer->K_value * pow(value + p_pointer->k_value, 2.5);						// calculate using v notch formula
	else if (derived_type == ANA_DEPTH_TO_FLOW_VENTURI)
		result = p_pointer->K_value * pow(value + p_pointer->k_value, 1.5);						// calculate using venturi formula
	else if (derived_type == ANA_DEPTH_TO_FLOW_TABLE)
	{
		table_points = (int)(p_pointer->K_value);
		if (table_points == 0)																	// test for zero points
			return result;
		if ((p_pointer->max_value == p_pointer->min_value) || (table_points == 1))				// catch min == max or a single entry table
			return p_pointer->point_value[0];
		else
		{
			point = 0;
			while (point < table_points)														// look for input values that straddle value
			{
				lower_input = p_pointer->input_value[point];
				upper_input = p_pointer->input_value[point + 1];
				lower_value = p_pointer->point_value[point];
				upper_value = p_pointer->point_value[point + 1];
				if (value == lower_input)														// catch on-point values
					return lower_value; 
				else if (value == upper_input)
					return upper_value;
				else if ((value > lower_input) && (value < upper_input))						// else if value between input values
				{
					remainder = value - lower_input;											//  linearly scale between values
					scale = upper_input - lower_input;
					return (lower_value + ((upper_value - lower_value) * (remainder / scale)));
				}
				point++;																		// else move up table
			};
			return result;
		}
	}
	return result;
}

/******************************************************************************
** Function:	Convert ADC counts to electrical then physical units
**
** Notes:		Sets value in sample_value. Don't use ana_p_channel, as may be
**				called from cmd
*/
void ana_counts_to_value(int index)
{
	ANA_config_type * p_config;
	ANA_channel_type * p_channel;

	p_config = &ANA_config[index];
	p_channel = &ANA_channel[index];

	p_channel->sample_value = (float)SNS_adc_value;												// Minimise the number of int to float conversions:

	if (((p_config->flags & ANA_MASK_CHANNEL_ENABLED) == 0) || (p_config->sensor_type == ANA_SENSOR_NONE))
	{
		p_channel->sample_value = FLT_MAX;														// no value
		if ((p_config->flags & ANA_MASK_DERIVED_DATA_ENABLED) != 0)								// if derived data enabled
			p_channel->derived_sample_value = FLT_MAX;
		return;
	}
																								// else:
																								// Combine ADC values into ANA_sample first:
	if (p_config->sensor_type == ANA_SENSOR_DIFF_MV)											// subtract zero value from transducer value
		p_channel->sample_value -= (float)ANA_zero_counts;
	else if (ANA_vref_counts == 0)																// trap divide by 0
	{
		p_channel->sample_value = FLT_MAX;
		if ((p_config->flags & ANA_MASK_DERIVED_DATA_ENABLED) != 0)								// if derived data enabled
			p_channel->derived_sample_value = FLT_MAX;
		return;
	}
	else																						// scale according to voltage ref:
		p_channel->sample_value /= (float)ANA_vref_counts;

	if (CAL_build_info.analogue_channel_types[index] == 'P')									// internal pressure channel
	{ 
																								// Do quadratic conversion into bar: y = (e0 * x^2) + (gain * x) + offset
		p_channel->sample_value = (p_config->e0 * p_channel->sample_value * p_channel->sample_value) +
			(p_channel->amplifier_gain * p_channel->sample_value) + p_channel->amplifier_offset;
		if (p_config->e1 == 0.0)																// convert to user units (trap divide by 0):
		{
			p_channel->sample_value = FLT_MAX;
			if ((p_config->flags & ANA_MASK_DERIVED_DATA_ENABLED) != 0)							// if derived data enabled
				p_channel->derived_sample_value = FLT_MAX;
			return;
		}
																								// else:
		p_channel->sample_value *= p_config->p1 / p_config->e1;
	}
	else																						// external transducer, mV, V or mA
	{
		p_channel->sample_value *= p_channel->amplifier_gain;									// scale value into electrical units:
		p_channel->sample_value += p_channel->amplifier_offset;
		p_channel->sample_value -= p_config->e0;												// Transform electrical value into physical units
		if (p_config->e1 == p_config->e0)														// trap divide by 0
		{
			p_channel->sample_value = FLT_MAX;
			if ((p_config->flags & ANA_MASK_DERIVED_DATA_ENABLED) != 0)							// if derived data enabled
				p_channel->derived_sample_value = FLT_MAX;
			return;
		}
																								// else:
		p_channel->sample_value *= (p_config->p1 - p_config->p0) / (p_config->e1 - p_config->e0);
		p_channel->sample_value += p_config->p0;
	}
	p_channel->sample_value += p_config->user_offset + p_config->auto_offset;					// Add in user offset + autocal offset:
	if ((p_config->flags & ANA_MASK_DERIVED_DATA_ENABLED) != 0)									// if derived data enabled
		p_channel->derived_sample_value = ana_calc_derived_value(index,
																 p_channel->sample_value,
																 p_config->derived_type);		// calculate derived data
}

/******************************************************************************
** Function:	Update an analogue register & return average value
**
** Notes:		Exits with p->count at 0 if a log or other action is due
*/
float ana_update_register(ANA_register_type *p, uint8 time_enumeration)
{
	float v;

	p->total += ana_p_channel->sample_value;
	p->count++;
	
	v = 0.0;									// by default
	if (p->time == ana_p_channel->sample_time)	// action is due
	{
		v = p->total / (float)p->count;

		p->total = 0.0;
		p->count = 0;
		LOG_set_next_time(&p->time, time_enumeration, true);
	}

	return v;
}

/******************************************************************************
** Function:	Process sample for logged value
**
** Notes:	
*/
void ana_update_log_value(void)
{
	float    value, derived_value;
	RTC_type time_stamp;
	
	value = ana_update_register(&ana_p_channel->log, ANA_config[ana_index].log_interval);
	if (ana_p_channel->log.count == 0)																// if time to log
	{
		if ((LOG_header_mask & (1 << (LOG_ANALOGUE_1_INDEX + ana_index))) == 0)						// if needs header
		{
			LOG_enqueue_value(LOG_ANALOGUE_1_INDEX + ana_index,										// enqueue header 
							  LOG_BLOCK_HEADER_TIMESTAMP,
							  (int32)LOG_get_timestamp(ana_p_channel->sample_time, 
							  ANA_config[ana_index].log_interval));
			LOG_header_mask |= 1 << (LOG_ANALOGUE_1_INDEX + ana_index);								// clear header mask bit
		}
		if ((ana_p_config->flags & ANA_MASK_DERIVED_DATA_ENABLED) != 0)								// if derived data enabled
		{
			if ((LOG_derived_header_mask & (1 << (LOG_ANALOGUE_1_INDEX + ana_index))) == 0)			// if needs derived_header
			{
				LOG_enqueue_value((LOG_ANALOGUE_1_INDEX + ana_index) | LOG_DERIVED_MASK,			// enqueue derived header
								  LOG_BLOCK_HEADER_TIMESTAMP,
								  (int32)LOG_get_timestamp(ana_p_channel->sample_time, ANA_config[ana_index].log_interval));
				LOG_derived_header_mask |= 1 << (LOG_ANALOGUE_1_INDEX + ana_index);					// clear derived header mask bit
			}
		}

		time_stamp.reg32[1] = RTC_now.reg32[1]; 													// calculate timestamp of this data item
		time_stamp.reg32[0] = LOG_get_timestamp(ana_p_channel->sample_time, ANA_config[ana_index].log_interval);
		LOG_enqueue_value(LOG_ANALOGUE_1_INDEX + ana_index, 										// enqueue value
						  LOG_DATA_VALUE, 
						  *(int32 *)&value);
		FTP_update_retrieval_info(FTP_ANA_FTPR_INDEX + ana_index, &time_stamp);						// save ftp retrieval info for normal data
		if ((ana_p_config->flags & ANA_MASK_DERIVED_DATA_ENABLED) != 0)								// if derived data enabled
		{
			derived_value = ana_calc_derived_value(ana_index, value, ana_p_config->derived_type);	// calculate derived data
			LOG_enqueue_value((LOG_ANALOGUE_1_INDEX + ana_index) | LOG_DERIVED_MASK,				// enqueue derived value
							  LOG_DATA_VALUE, 
							  *(int32 *)&derived_value);
			FTP_update_retrieval_info(FTP_DERIVED_ANA_INDEX + ana_index, &time_stamp);				// save ftp retrieval info for derived data
		}
	}
}

/******************************************************************************
** Function:	Process sample for SMS value
**
** Notes:	
*/
void ana_update_sms_value(void)
{
	float value, derived_value;
	
	if (ANA_config[ana_index].sms_data_interval == 0)
		return;

	value = ana_update_register(&ana_p_channel->sms, ANA_config[ana_index].sms_data_interval);
	
	if (ana_p_channel->sms.count == 0)
	{
		if ((LOG_sms_header_mask & (1 << (LOG_ANALOGUE_1_INDEX + ana_index))) == 0)					// needs sms header
		{
			LOG_enqueue_value((LOG_ANALOGUE_1_INDEX + ana_index) | LOG_SMS_MASK,					// enqueue sms header 
							  LOG_BLOCK_HEADER_TIMESTAMP,
							  (int32)LOG_get_timestamp(ana_p_channel->sample_time, ANA_config[ana_index].sms_data_interval));
			LOG_sms_header_mask |= 1 << (LOG_ANALOGUE_1_INDEX + ana_index);							// clear sms header mask bit
			if ((ana_p_config->flags & ANA_MASK_DERIVED_DATA_ENABLED) != 0)							// if derived data enabled
			{																						// enqueue derived sms header 
				if ((LOG_derived_sms_header_mask & (1 << (LOG_ANALOGUE_1_INDEX + ana_index))) == 0)	// needs derived sms header
				{
					LOG_enqueue_value((LOG_ANALOGUE_1_INDEX + ana_index) | LOG_SMS_MASK | LOG_DERIVED_MASK,
									  LOG_BLOCK_HEADER_TIMESTAMP,
									  (int32)LOG_get_timestamp(ana_p_channel->sample_time, ANA_config[ana_index].sms_data_interval));
					LOG_derived_sms_header_mask |= 1 << (LOG_ANALOGUE_1_INDEX + ana_index);			// clear derived sms header mask bit
				}
			}
		}

		LOG_enqueue_value((LOG_ANALOGUE_1_INDEX + ana_index) | LOG_SMS_MASK,						// enqueue sms value 
						  LOG_DATA_VALUE, 
						  *(int32 *)&value);
		if (PDU_time_for_batch(ana_index + PDU_AN0_CHANNEL_NUM))									// check for batch mode synchronisation
			PDU_schedule(ana_index + PDU_AN0_CHANNEL_NUM);
		if ((ana_p_config->flags & ANA_MASK_DERIVED_DATA_ENABLED) != 0)								// if derived data enabled
		{																						 
			derived_value = ana_calc_derived_value(ana_index, value, ana_p_config->derived_type);	// calculate derived data
			LOG_enqueue_value((LOG_ANALOGUE_1_INDEX + ana_index) | LOG_SMS_MASK | LOG_DERIVED_MASK,	// enqueue derived sms value
							  LOG_DATA_VALUE, 
							  *(int32 *)&derived_value);
			if (PDU_time_for_batch(ana_index + PDU_DERIVED_AN0_CHANNEL_NUM))						// check for batch mode synchronisation for derived data
				PDU_schedule(ana_index + PDU_DERIVED_AN0_CHANNEL_NUM);
		}
	}
}

/******************************************************************************
** Function:	Process sample for min/max value
**
** Notes:	
*/
void ana_update_min_max_value(void)
{
	float value, derived_value;

	value = ana_update_register(&ana_p_channel->min_max, ANA_config[ana_index].min_max_sample_interval);

	if (ana_p_channel->min_max.count == 0)															// if time to sample min max
	{
		if (value > ana_p_channel->max_value)
		{
			ana_p_channel->max_value = value;
			ana_p_channel->max_time.hr_bcd = RTC_now.hr_bcd;
			ana_p_channel->max_time.min_bcd = RTC_now.min_bcd;
			ana_p_channel->max_time.sec_bcd = RTC_now.sec_bcd;
		}

		if (value < ana_p_channel->min_value)
		{
			ana_p_channel->min_value = value;
			ana_p_channel->min_time.hr_bcd = RTC_now.hr_bcd;
			ana_p_channel->min_time.min_bcd = RTC_now.min_bcd;
			ana_p_channel->min_time.sec_bcd = RTC_now.sec_bcd;
		}
		if ((ana_p_config->flags & ANA_MASK_DERIVED_DATA_ENABLED) != 0)								// if derived data enabled
		{ 
			derived_value = ana_calc_derived_value(ana_index, value, ana_p_config->derived_type);	// calculate derived data
			if (derived_value > ana_p_channel->max_derived_value)
			{
				ana_p_channel->max_derived_value = derived_value;
				ana_p_channel->max_derived_time.hr_bcd = RTC_now.hr_bcd;
				ana_p_channel->max_derived_time.min_bcd = RTC_now.min_bcd;
				ana_p_channel->max_derived_time.sec_bcd = RTC_now.sec_bcd;
			}
	
			if (derived_value < ana_p_channel->min_derived_value)
			{
				ana_p_channel->min_derived_value = derived_value;
				ana_p_channel->min_derived_time.hr_bcd = RTC_now.hr_bcd;
				ana_p_channel->min_derived_time.min_bcd = RTC_now.min_bcd;
				ana_p_channel->min_derived_time.sec_bcd = RTC_now.sec_bcd;
			}
		}
	}
}

/******************************************************************************
** Function:	Process sample for normal alarm
**
** Notes:	
*/
void ana_update_normal_alarm(void)
{
	float value;

	if (ALM_config[LOG_ANALOGUE_1_INDEX - 1 + ana_index].enabled)
	{
		if (ana_p_channel->normal_alarm.time == SLP_NO_WAKEUP)										// if first time - alarm time set to sleep but alarm enabled
		{
			ana_p_channel->normal_alarm.time = ana_p_channel->sample_time;
			LOG_set_next_time(&ana_p_channel->normal_alarm.time, 
							  ALM_config[LOG_ANALOGUE_1_INDEX - 1 + ana_index].sample_interval, 
							  true);
			ana_p_channel->normal_alarm.count = 0;
			ana_p_channel->normal_alarm.total = 0.0;
		}
		else
		{
			value = ana_update_register(&ana_p_channel->normal_alarm, ALM_config[LOG_ANALOGUE_1_INDEX - 1 + ana_index].sample_interval);
			if (ana_p_channel->normal_alarm.count == 0)												// if time to process alarm
				ALM_process_value(LOG_ANALOGUE_1_INDEX - 1 + ana_index, value);
		}
	}
	else
		ana_p_channel->normal_alarm.time = SLP_NO_WAKEUP;											// alarm not enabled; send alarm to sleep
}

/******************************************************************************
** Function:	Process sample for derived alarm
**
** Notes:	
*/
void ana_update_derived_alarm(void)
{
	float value, derived_value;

	if ((ALM_config[LOG_ANALOGUE_1_INDEX - 1 + ana_index + ALM_ALARM_DERIVED_CHANNEL0].enabled) && 
		((ana_p_config->flags & ANA_MASK_DERIVED_DATA_ENABLED) != 0))
	{
		if (ana_p_channel->derived_alarm.time == SLP_NO_WAKEUP)											// if first time - alarm time set to sleep but alarm enabled
		{
			ana_p_channel->derived_alarm.time = ana_p_channel->sample_time;
			LOG_set_next_time(&ana_p_channel->derived_alarm.time, 
							  ALM_config[LOG_ANALOGUE_1_INDEX - 1 + ana_index + ALM_ALARM_DERIVED_CHANNEL0].sample_interval, 
							  true);
			ana_p_channel->derived_alarm.count = 0;
			ana_p_channel->derived_alarm.total = 0.0;
		}
		else
		{
			value = ana_update_register(&ana_p_channel->derived_alarm, 
										ALM_config[LOG_ANALOGUE_1_INDEX - 1 + ana_index + ALM_ALARM_DERIVED_CHANNEL0].sample_interval);
			if (ana_p_channel->derived_alarm.count == 0)												// if time to process alarm
			{
				derived_value = ana_calc_derived_value(ana_index, value, ana_p_config->derived_type);	// calculate derived data
				ALM_process_value(LOG_ANALOGUE_1_INDEX - 1 + ana_index + ALM_ALARM_DERIVED_CHANNEL0, 
								  derived_value);
			}
		}
	}
	else
		ana_p_channel->derived_alarm.time = SLP_NO_WAKEUP;												// alarm not enabled; send alarm to sleep
}

/******************************************************************************
** Function:	Initialise min/max values & times given pointer to channel
**
** Notes:		must take derived into account
*/
void ana_clear_min_max(ANA_channel_type * p_channel, bool derived)
{
	if (derived)
	{
		p_channel->min_derived_value = FLT_MAX;
		p_channel->max_derived_value = -FLT_MAX;
		*(uint32 *)&p_channel->min_derived_time = 0;
		*(uint32 *)&p_channel->max_derived_time = 0;
	}
	else
	{
		p_channel->min_value = FLT_MAX;
		p_channel->max_value = -FLT_MAX;
		*(uint32 *)&p_channel->min_time = 0;
		*(uint32 *)&p_channel->max_time = 0;
	}
}

/******************************************************************************
** Function:	Retrieve derived conversion data from file system given derived type
**
** Notes:		Initially for depth to flow conversion config data
**              May be expanded in future for other derived data
**				called by #CEC command
**				returns false if no file or problem reading it
*/
bool ANA_retrieve_derived_conversion_data(uint8 channel, uint8 derived_type)
{
	int   point, table_points;
	float temp_value;
	ANA_derived_config_type * p_pointer;
	char filename[16];

	p_pointer = &ANA_derived_config[channel];

	if ((derived_type == ANA_DEPTH_TO_FLOW_RECTANGULAR) ||											// set up filename
		(derived_type == ANA_DEPTH_TO_FLOW_V_NOTCH) ||
		(derived_type == ANA_DEPTH_TO_FLOW_VENTURI))
	{
		if (derived_type == ANA_DEPTH_TO_FLOW_RECTANGULAR)
			sprintf(filename, "%u%s", channel+1, (char *)CFS_rect_name);
		else if (derived_type == ANA_DEPTH_TO_FLOW_V_NOTCH)
			sprintf(filename, "%u%s", channel+1, CFS_vnotch_name);
		else
			sprintf(filename, "%u%s", channel+1, (char *)CFS_venturi_name);
		if (CFS_file_exists((char *)CFS_config_path, filename))										// check calc file exists
		{																							// get four values for formula from file
			if (CFS_read_line((char *)CFS_config_path, filename, 1, STR_buffer, 140) > 0)
				sscanf(STR_buffer, "%f", &(p_pointer->min_value));
			else
				return false;
			if (CFS_read_line((char *)CFS_config_path, filename, 2, STR_buffer, 140) > 0)
				sscanf(STR_buffer, "%f", &(p_pointer->max_value));
			else
				return false;
			if (CFS_read_line((char *)CFS_config_path, filename, 3, STR_buffer, 140) > 0)
				sscanf(STR_buffer, "%f", &(p_pointer->K_value));
			else
				return false;
			if (CFS_read_line((char *)CFS_config_path, filename, 4, STR_buffer, 140) > 0)
				sscanf(STR_buffer, "%f", &(p_pointer->k_value));
			else
				return false;
		}
		else
			return false;
	}
	else if (derived_type == ANA_DEPTH_TO_FLOW_TABLE)
	{
		sprintf(filename, "%u%s", channel+1, (char *)CFS_ftable_name);
		if (CFS_file_exists((char *)CFS_config_path, filename))										// check table file exists
		{																							// table is min, max, n points, lines 0 to n-1: input,output 
			if (!ana_get_ftable_values(channel, 1, &(p_pointer->min_value), &temp_value))
				return false;
			if (!ana_get_ftable_values(channel, 2, &(p_pointer->max_value), &temp_value))
				return false;
			if (CFS_read_line((char *)CFS_config_path, filename, 3, STR_buffer, 140) > 0)
				sscanf(STR_buffer, "%d", &table_points);
			else
				return false;
			if (table_points > ANA_DEPTH_TO_FLOW_POINTS)											// test for too many points
				return false;
			p_pointer->K_value = (float)table_points;
			point = 0;
			while (point < table_points)															// get table entries
			{
				if (!ana_get_ftable_values(channel, 4 + point, &(p_pointer->input_value[point]), &(p_pointer->point_value[point])))
					return false;
				point++;																			// move up table
			};
		}
		else
			return false;
	}
	return true;
}

/******************************************************************************
** Function:	create file footer in string supplied, for analogue channel
**
** Notes:		channel is 0 to 6, derived bool supplied
**				Add signal strength, batt volts & \r\n, after calling this.
*/
int  ANA_create_block_footer(char * string, uint8 channel, bool derived)
{
	int len;
	ANA_channel_type * p_channel = &ANA_channel[channel];

	if (ANA_config[channel].min_max_sample_interval == 0)									// if min/max interval is zero
		len = sprintf(string, "\r\n*,,,");
	else
		len = derived ?
			LOG_print_footer_min_max(string, &p_channel->min_derived_time, p_channel->min_derived_value,
					&p_channel->max_derived_time, p_channel->max_derived_value) :
			LOG_print_footer_min_max(string, &p_channel->min_time, p_channel->min_value,
					&p_channel->max_time, p_channel->max_value);

	ana_clear_min_max(p_channel, derived);

	return len;
}

/******************************************************************************
** Function:	Midnight task
**
** Notes:	
*/
void ana_channel_midnight_task(void)
{
	uint8 channel_number;

	channel_number = LOG_ANALOGUE_1_INDEX + ana_index;
	LOG_enqueue_value(channel_number, LOG_BLOCK_FOOTER, 0);
	if ((ana_p_config->flags & ANA_MASK_DERIVED_DATA_ENABLED) != 0)
		LOG_enqueue_value(channel_number | LOG_DERIVED_MASK, LOG_BLOCK_FOOTER, 0);
}

/******************************************************************************
** Function:	Task for an individual analogue channel
**
** Notes:		ana_p_channel & ana_index already set. Actions when timer has
**				expired are as follows:
**				read_required	read_in_progress
**				0				0					start conversion
**				1				1					performing ADC conversion
**				0				1					conversion complete - process ADC result
**				1				0					immediate value required - don't process result
*/
void ana_channel_task(void)
{
	uint8 mask;
																			// If the sample time is past, must ensure that it gets updated or we'll keep
																			// the CPU awake all the time.
	if (ana_p_channel->sample_time > RTC_time_sec)							// nothing to do yet
		return;
																			// else:
	mask = ANA_MASK(ana_index);
	if ((ana_adc_read_in_progress_mask & mask) == 0)						// start measurement
	{
		ANA_start_adc_read(ana_index);
		ana_adc_read_in_progress_mask |= mask;
	}
	else if (ANA_get_adc_values(ana_index))									// finished conversion
	{
		ana_adc_read_in_progress_mask &= ~mask;

		ana_update_log_value();												// log data
		ana_update_sms_value();												// log sms data
		ana_update_min_max_value(); 										// check min and max
		ana_update_normal_alarm();											// check alarms
		ana_update_derived_alarm();

		if (ana_p_channel->sample_time == 0)								// have just done our midnight logs
			ana_channel_midnight_task();

		LOG_set_next_time(&ana_p_channel->sample_time, ana_p_config->sample_interval, false);
	}
}

/******************************************************************************
** Function:	Check if any analogue channels currently busy taking measurements
**
** Notes:	
*/
bool ANA_busy(void)
{
	int index;

	for (index = 0; index < ANA_NUM_CHANNELS; index++)
	{
		if (CAL_build_info.analogue_channel_types[index] == '\0')
			break;

		if (ANA_channel[index].sample_time <= RTC_time_sec)
			return true;
	}

	return false;
}

/******************************************************************************
** Function:	Turn analogue power off
**
** Notes:
*/
void ana_power_off(void)
{
	// If running on external power or USB, allow each line to remain on if configured
	if (USB_state > USB_DISCONNECTED)
	{
		HDW_AN_BOOST_ON = ANA_boost_on_perm_flag;
		HDW_TURN_AN_ON = ANA_on_perm_flag;
	}
	else											// both lines off
	{
		HDW_AN_BOOST_ON = false;
		HDW_TURN_AN_ON = false;
	}

#if (HDW_NUM_CHANNELS == 3)
	HDW_DIFF1_SW1_ON = false;
	HDW_DIFF1_SW2_ON = false;
  #ifndef HDW_PRIMELOG_PLUS			// only XiLog+ 3ch has DIFF2_SW
	HDW_DIFF2_SW1_ON = false;
	HDW_DIFF2_SW2_ON = false;
  #endif
#endif

	// will need to read Vref next time if current or voltage transducer
	ana_flags &= ~ANA_GOT_VREF_MASK;
	ana_adc_state = ANA_ADC_IDLE;
}

/******************************************************************************
** Function:	Go into get_ref_or_zero state
**
** Notes:	Called from powering and boosting states
*/
void ana_get_vref_or_zero(void)
{
	// Set up sensor PIC ADC address and command flag:
	SNS_adc_address = SNS_ADC_ADDRESS_VREF;		// by default, unless overridden for pressure channels
	ana_adc_state = ANA_ADC_GET_VREF;			// by default, unless overridden for pressure channels

#if (HDW_NUM_CHANNELS == 9)
	// Channels 1-4 = voltage, 5-7 = current
	if (ana_adc_index < 4)
		SNS_read_adc = true;
	else
		SNS_read_adc_2 = true;
#else	// 3-ch hardware
	if (ANA_config[ana_adc_index].sensor_type == ANA_SENSOR_DIFF_MV)
	{
		SNS_adc_address = ((ana_adc_index & 1) == 0) ? SNS_ADC_ADDRESS_DIFF1 : SNS_ADC_ADDRESS_DIFF2;
		ana_adc_state = ANA_ADC_GET_ZERO;
	}

	SNS_read_adc = true;
#endif
}

/******************************************************************************
** Function:	Go into read signal state
**
** Notes:	
*/
void ana_get_signal(void)
{
#if (HDW_NUM_CHANNELS == 9)

		SNS_adc_address = ana_adc_address[ana_adc_index];
		// Channels 1-4 = voltage, 5-7 = current
		if (ana_adc_index < 4)
			SNS_read_adc = true;
		else
			SNS_read_adc_2 = true;

		ana_adc_state = ANA_ADC_GET_SIGNAL;

#else	// 3-ch hardware

	switch (ANA_config[ana_adc_index].sensor_type)
	{
	case ANA_SENSOR_DIFF_MV:			// switch to signal only if mV (pressure) channel
		if ((ana_adc_index & 1) == 0)	// channels AN1 or AN3
		{
			HDW_DIFF1_SW1_ON = true;
			HDW_DIFF1_SW2_ON = false;
		}
#ifndef HDW_PRIMELOG_PLUS				// only XiLog+ 3ch has DIFF2_SW
		else							// AN2 or AN4
		{
			HDW_DIFF2_SW1_ON = true;
			HDW_DIFF2_SW2_ON = false;
		}
#endif
		TIM_START_TIMER(ana_timer);		// start switching timer (after switches have been set)
		ana_adc_state = ANA_ADC_SWITCHING;
		break;

	case ANA_SENSOR_CURRENT:
		SNS_adc_address = SNS_ADC_ADDRESS_CURRENT;
		ana_adc_state = ANA_ADC_GET_SIGNAL;
		SNS_read_adc = true;
		break;

	default:	// voltage
		SNS_adc_address = SNS_ADC_ADDRESS_VOLTAGE;
		ana_adc_state = ANA_ADC_GET_SIGNAL;
		SNS_read_adc = true;
		break;
	}
#endif
}

//*****************************************************************************
// Function:	External power disconnected
//
// Notes:		Regardless of permanent power flags, ensures analogue power lines
//				are off if ADC not currently in use
//
void ANA_ext_power_disconnected(void)
{
	if (ana_adc_state == ANA_ADC_IDLE)
	{
		HDW_AN_BOOST_ON = false;
		HDW_TURN_AN_ON = false;
	}
}

/******************************************************************************
** Function:	Analogue task
**
** Notes:	
*/
void ANA_task(void)
{
	uint8 c;

	for (ana_index = 0; ana_index < ANA_NUM_CHANNELS; ana_index++)
	{
		if (ANA_channel_exists(ana_index))
		{
			ana_p_channel = &ANA_channel[ana_index];
			ana_p_config = &ANA_config[ana_index];
			ana_channel_task();
		}
	}

	// now do any pending A/D tasks
	if ((ana_adc_read_required_mask & ANA_MAX_CHANNEL_MASK) == 0)	// nothing to do
	{
#ifdef HDW_PRIMELOG_PLUS											// needs Vref to read battery voltages
		if (HDW_TURN_AN_ON && !HDW_BATTEST_INT_ON)					// power's on - it shouldn't be!
			ana_power_off();
#else
		if (HDW_TURN_AN_ON)											// power's on - it shouldn't be!
			ana_power_off();										// check & turn off if necessary
#endif
		return;
	}
	// else:

#if (HDW_NUM_CHANNELS == 9)
	if (SNS_read_adc || SNS_read_adc_2 || SNS_command_in_progress)	// Sensor PIC(s) ADC busy
	{
		if (SNS_read_adc)						// should never both be busy
			SNS_read_adc_2 = false;

		return;
	}
#else
	if (SNS_read_adc || SNS_command_in_progress)	// Sensor PIC ADC busy
		return;
#endif
	// else:

	// Measure Vref while switching diff input from zero to signal (saves waiting for switching delay).
	switch (ana_adc_state)
	{
	case ANA_ADC_IDLE:
		// set ana_adc_index according to required read mask:
		// start from the last channel we converted + 1, so the ana state machine can't
		// lock up waiting for channel 7 to convert while we keep running out of time
		// doing 1..6. NB termination of this loop relies on
		// (ana_adc_read_required_mask & ANA_MAX_CHANNEL_MASK) != 0, as checked above
		c = ANA_MASK(ana_adc_index);
		do
		{
			ana_adc_index++; 
			c <<= 1;
			c &= ANA_MAX_CHANNEL_MASK;
			if (c == 0)					// exceeded number of implemented channels
			{
				ana_adc_index = 0;
				c = _B00000001;
			}
		} while ((ana_adc_read_required_mask & c) == 0);

		ana_adc_state = ANA_ADC_POWERING;	// default: apply power or switch zero in for 10ms

		c = !HDW_TURN_AN_ON;			// c = false if already on, true if switching on			
		HDW_TURN_AN_ON = true;			// turn on

		TIM_START_TIMER(ana_timer);		// time the power-up, the boost, or diff zero switching
		
		if (!HDW_AN_BOOST_ON && ((ana_flags & ANA_BOOST_MASK) != 0))	// boost required, but not yet on
		{
			HDW_AN_BOOST_ON = true;
			ana_boost_timer_x20ms = ANA_boost_time_ms / 20;
			ana_adc_state = ANA_ADC_BOOSTING;							// ensure boost time applied
			c = true;													// go through switching delay
		}
		ana_flags &= ~ANA_BOOST_MASK;

		if (!c && (ANA_config[ana_adc_index].sensor_type != ANA_SENSOR_DIFF_MV))
		{
			// The power is already on, and this is a voltage or current sensor.
			// Go straight to reading either Vref or the signal.
			if ((ana_flags & ANA_GOT_VREF_MASK) == 0)
				ana_get_vref_or_zero();
			else
				ana_get_signal();
		}

#if (HDW_NUM_CHANNELS == 3)
		HDW_DIFF1_SW1_ON = false;
		HDW_DIFF1_SW2_ON = false;
#ifndef HDW_PRIMELOG_PLUS			// only XiLog+ 3ch has DIFF2_SW
		HDW_DIFF2_SW1_ON = false;
		HDW_DIFF2_SW2_ON = false;
#endif		
		
		// Set switches for diff zero if this is required
		if (ANA_config[ana_adc_index].sensor_type == ANA_SENSOR_DIFF_MV)
		{
			if ((ana_adc_index & 1) == 0)				// do channel 1 or 3
				HDW_DIFF1_SW2_ON = true;
			else										// do channel 2 or 4
				HDW_DIFF2_SW2_ON = true;
		}
#endif
		break;

	case ANA_ADC_POWERING:
		if (TIM_TIMER_EXPIRED(ana_timer, 10))
			ana_get_vref_or_zero();
		break;

	case ANA_ADC_BOOSTING:
		if (ANA_boost_time_ms > 16000)					// 20ms resolution
		{
			if (ana_boost_timer_x20ms == 0)
				ana_get_vref_or_zero();
			else if (TIM_20ms_tick)
				ana_boost_timer_x20ms--;
		}
		else if (TIM_TIMER_EXPIRED(ana_timer, ANA_boost_time_ms))
			ana_get_vref_or_zero();
		break;

	case ANA_ADC_GET_VREF:
		// SNS_read_adc or SNS_read_adc_2 now clear (checked above)
		ANA_vref_counts = SNS_adc_value;
		ana_flags |= ANA_GOT_VREF_MASK;
		ana_get_signal();
		break;

	case ANA_ADC_GET_ZERO:
		ANA_zero_counts = SNS_adc_value;
		ana_get_signal();
		break;

#if (HDW_NUM_CHANNELS == 3)		// 9-channel never enters this state
	case ANA_ADC_SWITCHING:
		if (TIM_TIMER_EXPIRED(ana_timer, 10))
		{
			SNS_adc_address = ((ana_adc_index & 1) == 0) ? SNS_ADC_ADDRESS_DIFF1 : SNS_ADC_ADDRESS_DIFF2;
			ana_adc_state = ANA_ADC_GET_SIGNAL;
			SNS_read_adc = true;
		}
		break;
#endif

	default:	// case ANA_ADC_GET_SIGNAL: SNS_read_adc now clear (checked above)
#if (HDW_NUM_CHANNELS == 3)
		// ADC read complete for channels 1 & 3 or 2 & 4
		if ((ana_adc_index & 0x01) == 0)								// chs A1 & A3
		{
			ana_adc_read_required_mask &= _B00001010;					// leave 2 & 4 bits alone
			ana_counts_to_value(0);										// use main ch config
			ANA_channel[2].sample_value = ANA_channel[0].sample_value;	// shadow sample = main
		}
		else								// chs A2 & A4
		{
			ana_adc_read_required_mask &= _B00000101;					// leave 1 & 3 bits alone
			ana_counts_to_value(1);										// use main ch config
			ANA_channel[3].sample_value = ANA_channel[1].sample_value;	// shadow sample = main
		}
#else	// 9-ch
		ana_counts_to_value(ana_adc_index);
		ana_adc_read_required_mask &= ANA_MAX_CHANNEL_MASK & ~(1 << ana_adc_index);
#endif
		if (ana_adc_read_required_mask == 0)
			ana_power_off();
		else								// another read required
			ana_adc_state = ANA_ADC_IDLE;	// leave power on
		break;
	}
}

/******************************************************************************
** Function:	Start or stop logging on a channel
**
** Notes:	
*/
void ana_start_stop_channel(int index)
{
	uint8 ftp_index = FTP_ANA_FTPR_INDEX + index;
	uint8 ftp_derived_index = FTP_DERIVED_ANA_INDEX + index;

	ana_p_channel = &ANA_channel[index];
	ana_p_config = &ANA_config[index];

	// Whether it's on or off, set it not busy so it can start OK:
	ana_adc_read_in_progress_mask &= ~ANA_MASK(index);

	if (((ana_p_config->flags & ANA_MASK_CHANNEL_ENABLED) == 0) ||
		(ana_p_config->sensor_type == ANA_SENSOR_NONE) || (LOG_state <= LOG_STOPPED))	// turn channel off
	{
		ana_power_off();
		ana_p_channel->sample_time = SLP_NO_WAKEUP;
		ana_p_channel->normal_alarm.time = SLP_NO_WAKEUP;
		ana_p_channel->derived_alarm.time = SLP_NO_WAKEUP;
	}
	else	// it's on:
	{
		ana_p_channel->sample_time = RTC_time_sec;

		ana_p_channel->log.count = 0;
		ana_p_channel->log.total = 0.0;
		ana_p_channel->log.time = RTC_time_sec;
		LOG_set_next_time(&ana_p_channel->log.time, ana_p_config->log_interval, true);

		ana_p_channel->sms.count = 0;
		ana_p_channel->sms.total = 0.0;
		ana_p_channel->sms.time = RTC_time_sec;
		LOG_set_next_time(&ana_p_channel->sms.time, ana_p_config->sms_data_interval, true);

		ana_p_channel->min_max.count = 0;
		ana_p_channel->min_max.total = 0.0;
		ana_p_channel->min_max.time = RTC_time_sec;
		LOG_set_next_time(&ana_p_channel->min_max.time, ana_p_config->min_max_sample_interval, true);
		
		ana_p_channel->normal_alarm.count = 0;
		ana_p_channel->normal_alarm.total = 0.0;
		ana_p_channel->normal_alarm.time = SLP_NO_WAKEUP;

		ana_p_channel->derived_alarm.count = 0;
		ana_p_channel->derived_alarm.total = 0.0;
		ana_p_channel->derived_alarm.time = SLP_NO_WAKEUP;

		// activate retrieval data
		FTP_activate_retrieval_info(ftp_index);
		FTP_activate_retrieval_info(ftp_derived_index);
	}
}

/******************************************************************************
** Function:	Start/stop logging on all analogue channels
**
** Notes:	
*/
void ANA_start_stop_logging(void)
{
	for (ana_index = 0; ana_index < ANA_NUM_CHANNELS; ana_index++)
	{
		if (ANA_channel_exists(ana_index))
			ana_start_stop_channel(ana_index);
	}
}

/******************************************************************************
** Function:	Configure analogue channel
**
** Notes:		Returns false if channel calibration not read.
*/
bool ANA_configure_channel(uint8 index)
{
	char *p;
#if (HDW_NUM_CHANNELS == 3)		// keep shadow channel configs in sync
	ANA_config_type * p_main;
	ANA_config_type * p_shadow;
#endif

	ana_p_channel = &ANA_channel[index];

	if (ANA_config[index].sample_interval == 0)					// channel disabled
		ANA_config[index].flags &= ~ANA_MASK_CHANNEL_ENABLED;	// don't start logging

	// Analogue calibration file format given in Calibration and Configuration Data Formats document.
	// Calibration constants described in New Logger Hardware Requirements Specification.
	p = CAL_read_analogue_coefficients(index);
	if (p != NULL)
	{
		// see if this is a channel with internal pressure transducer
		if (CAL_build_info.analogue_channel_types[index] == 'P')
		{
			ANA_config[index].flags &= ~ANA_MASK_POWER_TRANSDUCER;
			ANA_config[index].sensor_type = ANA_SENSOR_DIFF_MV;

			// skip voltage & current gains & offsets.
			// Read quadratic coefficient a into e0, b into gain & c into offset:
			sscanf(p, "%*f %*f %*f %*f %*f %*f %*f %*f %f %f %f",
				&ANA_config[index].e0, &ana_p_channel->amplifier_gain, &ana_p_channel->amplifier_offset);
		}
		else switch (ANA_config[index].sensor_type)
		{
		case ANA_SENSOR_DIFF_MV:		// external pressure sensor
			// skip voltage & current parameters, just read Vcal/Gn into amplifier gain:
			sscanf(p, "%*f %*f %*f %*f %*f %*f %*f %*f %f", &ana_p_channel->amplifier_gain);
			ana_p_channel->amplifier_offset = 0.0;
			break;

		case ANA_SENSOR_CURRENT:
			// skip voltage parameters. Read current gain (M) & offset (C)
			sscanf(p, "%*f %*f %*f %*f %*f %*f %f %f", &ana_p_channel->amplifier_gain, &ana_p_channel->amplifier_offset);
			break;

		case ANA_SENSOR_0_2V:
			sscanf(p, "%f %f", &ana_p_channel->amplifier_gain, &ana_p_channel->amplifier_offset);
			break;

		case ANA_SENSOR_0_5V:
			sscanf(p, "%*f %*f %f %f", &ana_p_channel->amplifier_gain, &ana_p_channel->amplifier_offset);
			break;

		case ANA_SENSOR_0_10V:
			sscanf(p, "%*f %*f %*f %*f %f %f", &ana_p_channel->amplifier_gain, &ana_p_channel->amplifier_offset);
			break;
		}
	}
	else															// couldn't open cal file
		ANA_config[index].flags &= ~ANA_MASK_CHANNEL_ENABLED;		// don't start logging

#if (HDW_NUM_CHANNELS == 3)							// keep shadow channel configs in sync

	p_main = &ANA_config[index & 0x01];				// ch 0 or 1 (A1 or A2)
	p_shadow = &ANA_config[(index & 0x01) + 2];		// ch 2 or 3 (A3 or A4)

	if ((p_main->flags & ANA_MASK_CHANNEL_ENABLED) == 0)	// main channel disabled
		p_shadow->flags &= ~ANA_MASK_CHANNEL_ENABLED;		// so shadow is too

	p_shadow->flags |= p_main->flags & ANA_MASK_POWER_TRANSDUCER;
	p_shadow->sensor_type = p_main->sensor_type;
	p_shadow->user_offset = p_main->user_offset;
	p_shadow->auto_offset = p_main->auto_offset;
	p_shadow->e0 = p_main->e0;
	p_shadow->e1 = p_main->e1;
	p_shadow->p0 = p_main->p0;
	p_shadow->p1 = p_main->p1;
	p_shadow->sensor_index = p_main->sensor_index;

	// if we've just changed a main channel, change its shadow
	if (index < 2)
	{
		ana_start_stop_channel(index + 2);

		// When values are first logged with new config, ensure a header is inserted.
		LOG_header_mask &= ~(1 << (LOG_ANALOGUE_1_INDEX + 2 + index));
		LOG_sms_header_mask &= ~(1 << (LOG_ANALOGUE_1_INDEX + 2 + index));
		ANA_insert_derived_header(index);
	}
#endif

	ana_start_stop_channel(index);

	// When values are first logged with new config, ensure a header is inserted.
	LOG_header_mask &= ~(1 << (LOG_ANALOGUE_1_INDEX + index));
	LOG_sms_header_mask &= ~(1 << (LOG_ANALOGUE_1_INDEX + index));
	ANA_insert_derived_header(index);

	return (p != NULL);
}

/******************************************************************************
** Function:	set flags to insert derived headers
**
** Notes:		used locally and by #ADD command
*/
void ANA_insert_derived_header(uint8 index)
{
	LOG_derived_header_mask &= ~(1 << (LOG_ANALOGUE_1_INDEX + index));
	LOG_derived_sms_header_mask &= ~(1 << (LOG_ANALOGUE_1_INDEX + index));
}

/******************************************************************************
** Function:	Print analogue channel configuration
**
** Notes:	
*/
void ANA_print_config(int index, char *s)
{
	ANA_config_type * p;
	int shadow;

#if (HDW_NUM_CHANNELS == 9)
	shadow = 0;
#else	// 3-ch
	shadow = (index > 1);
#endif

	p = &ANA_config[index];

	s += sprintf(s, "%d,%u,%d,%d,%u,%u,%u,%u,",
		((p->flags & ANA_MASK_CHANNEL_ENABLED) != 0) ? 1 : 0,
		p->sensor_type,
		((p->flags & ANA_MASK_POWER_TRANSDUCER) != 0) ? 1 : 0,
		((p->flags & ANA_MASK_MESSAGING_ENABLED) != 0) ? 1 : 0,
		p->sample_interval, p->log_interval, p->min_max_sample_interval, p->sms_data_interval);

	s += STR_print_float(s, p->user_offset);
	*s++ = ',';
	s += STR_print_float(s, p->auto_offset);
	*s++ = ',';
	s += STR_print_float(s, p->e0);
	*s++ = ',';
	s += STR_print_float(s, p->e1);
	*s++ = ',';
	s += STR_print_float(s, p->p0);
	*s++ = ',';
	s += STR_print_float(s, p->p1);
	sprintf(s, ",%u,%02X,%u,%u,%d",
		p->sensor_index, p->sms_message_type, p->description_index, p->units_index, shadow);
}

/******************************************************************************
** Function:	Synchronise analogue timers to time of day
**
** Notes:	
*/
void ANA_synchronise(void)
{
	int index;

	// for each channel present
	for (index = 0; index < ANA_NUM_CHANNELS; index++)
	{
		if (ANA_channel_exists(index))
			ana_start_stop_channel(index);
	}
}

/******************************************************************************
** Function:	return immediate value of given channel
**
** Notes:		real or derived
*/
float ANA_immediate_value(uint8 channel_id)
{
	uint8 index = (channel_id & 0x07) - 1;																// extract channel number

	if (ANA_channel_exists(index))
	{
		if ((LOG_state > LOG_STOPPED) && ((ANA_config[index].flags & ANA_MASK_CHANNEL_ENABLED) != 0))	// if enabled and logging
		{
			if ((channel_id & 0x08) == 0)																// if normal value required 
				return ANA_channel[index].sample_value;													// get sample value
			else if ((ANA_config[index].flags & ANA_MASK_DERIVED_DATA_ENABLED) != 0)					// else if derived data enabled
				return ANA_channel[index].derived_sample_value;											// get derived sample value
		}
	}																									// else
	return 0;
}

/******************************************************************************
** Function:	Turn Vref off (after battery measurement), only if not in use
**				by analogue channels
** Notes:		
*/
void ANA_vref_off(void)
{
	if ((ana_adc_read_required_mask & ANA_MAX_CHANNEL_MASK) == 0)	// nothing to do
		ana_power_off();
}

/******************************************************************************
** Function:	Determine if any of the channels will require transducer powering
**
** Notes:		Used so RDA and IMV can determine whether or not to wait for
**				an immediate reading, or just use last sample
*/
bool ANA_power_transducer_required(void)
{
	int index;

	for (index = 0; index < ANA_NUM_CHANNELS; index++)
	{
		if (ANA_channel_exists(index))
		{
			if ((ANA_config[index].flags & (ANA_MASK_CHANNEL_ENABLED | ANA_MASK_POWER_TRANSDUCER)) ==
				(ANA_MASK_CHANNEL_ENABLED | ANA_MASK_POWER_TRANSDUCER))
				return true;
		}
	}

	return false;
}

#endif
