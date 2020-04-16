/******************************************************************************
** File:	Dop.c	
**
** Notes:	Doppler sensor logging code
**
** v3.04 181211 PB First version for Waste Water
**
** V3.27 250613 PB DEL190: debug #TOD faults)
**				   DEL194: debug #NVM faults 
**				   DEL195: remove individual DOP_configure_xxx() functions
**
** V3.36 140114 PB remove calls to FTP_deactivate_retrieval_info()
**
** V4.11 260814 PB rewrite DOP_task to include send velocity and send height in measurement loop
**				   add DOP_sensor_present flag
**				   set DOP_sensor_present if doppler initialisation is successful
**				   if changes from false to true at this point, insert all log headers, as sensor has been reconnected
**				   clear it if any comms fault with RS485
**		 040914 PB rework calculation of flow rate from doppler sensor data, look up table and area calc	
** 		 231014 PB	RS485 boost control via RG14
*/

#include <string.h>
#include <stdio.h>
#include <float.h>

#include "custom.h"
#include "Compiler.h"
#include "str.h"
#include "math.h"
#include "HardwareProfile.h"
#include "MDD File System/FSIO.h"
#include "Tim.h"
#include "Rtc.h"
#include "Slp.h"
#include "Ana.h"
#include "Log.h"
#include "Ftp.h"
#include "Alm.h"
#include "Pdu.h"
#include "Ser.h"
#include "Cfs.h"
#include "USB.h"

#define extern
#include "dop.h"
#undef extern

//#ifdef HDW_RS485
#if 0

#ifdef WIN32
#pragma message("Warning: Trig functions #defined out")
#define sin(X)	0
#define acos(X)	0
#endif

/* local definitions */

#define DOP_IDLE				0													// definitions of task states
#define DOP_START				1
#define DOP_ONE_SHOT_REPLY		2
#define DOP_MEASURE_INIT		3
#define DOP_READING_MEASUREMENT	4
#define DOP_WRITING_HEIGHT		5
#define DOP_WRITING_VELOCITY	6
#define DOP_DELAY				7

#define DOP_MIN_TEMP		0
#define DOP_MAX_TEMP		1000
#define DOP_TEMP_SCALE		100

/* local variables */

bool	dop_start_pending;
bool	dop_sensor_running;
bool	dop_log_pending;
bool	dop_midnight_task_pending;
int		dop_20ms_timer;														// timeout timer
int		dop_measure_loop_count;
uint8	dop_state;															// independent state machines and outputs

/* local constants */

const uint16 dop_speed_of_sound[11] =										// table of speed of sound in water at 10 degree celsius intervals from 0 to 100
{
	1403, 1447, 1481, 1507, 1526, 1541, 1552, 1555, 1555, 1550, 1543
};

/* private functions */
																					
/******************************************************************************
** Function:	dop_calc_velocity(int16 temperature)
**
** Notes:		temperature in 0.1 degrees Celsius
*/
uint16 dop_calc_velocity(int temperature)
{
	int remainder, lower, upper, point;

	if (temperature > DOP_MAX_TEMP)															// limit input value
		temperature = DOP_MAX_TEMP;
	else if (temperature < DOP_MIN_TEMP)
		temperature = DOP_MIN_TEMP;
	point = (int)((temperature - DOP_MIN_TEMP) / DOP_TEMP_SCALE);							// get lower point and table value
	lower = dop_speed_of_sound[point];
	upper = lower;																			// this lets maths survive a missing upper point and exactly on point
	remainder = temperature - (point * DOP_TEMP_SCALE);
	if ((remainder != 0) && (point < 10))													// if not on exact point and within table
		upper = dop_speed_of_sound[point + 1];												// get upper point
	return (lower + ((upper - lower) * (remainder / DOP_TEMP_SCALE)));						// interpolate linearly to exact value or stay on exact point
}

/******************************************************************************
** Function:	dop_look_up_derived_area(float value)
**
** Notes:		get cross sectional area of water in pipe 
**				using look up table in CONFIG\APIPE.CAL 
**				given input value - depth of water in m
*/
float dop_look_up_derived_area(float input_m)
{
	int   point;
	int	  table_points;
	float scale, remainder;
	float lower_input_m = 0;
	float upper_input_m = 0;
	float lower_value_sqm = 0;
	float upper_value_sqm = 0;

	// test code
	// int j;

	DOP_derived_config_type * p_pointer;

	p_pointer = &DOP_derived_config;

	table_points = (int)(p_pointer->K_value);

	// test code
	/*
	j = sprintf(STR_buffer, "points = %u, min = ", (int)(p_pointer->K_value));
	j += STR_print_float(&STR_buffer[j], p_pointer->min_value);
	j += sprintf(&STR_buffer[j], ", max = ");
	j += STR_print_float(&STR_buffer[j], p_pointer->max_value);
	USB_monitor_string(STR_buffer);
	for (point = 0; point < table_points; point++)
	{
		j = sprintf(STR_buffer, "point %u, depth input = ", point + 1);
		j += STR_print_float(&STR_buffer[j], p_pointer->input_value[point]);
		j += sprintf(&STR_buffer[j], ", area output = ");
		j += STR_print_float(&STR_buffer[j], p_pointer->point_value[point]);
		USB_monitor_string(STR_buffer);
	}
	*/

	if (table_points == 0)																	// test for zero points
		return 0.0;
	if ((p_pointer->max_value == p_pointer->min_value) || (table_points == 1))				// catch min == max or a single entry table
		return p_pointer->point_value[0];
	if (input_m < p_pointer->min_value)														// catch out of range input
		return p_pointer->point_value[0];
	if (input_m > p_pointer->max_value)
		return p_pointer->point_value[table_points - 1];
	else
	{
		point = 0;
		while (point < table_points)														// look for input values that straddle value
		{

			// test code
			// sprintf(STR_buffer, "table point %u", point + 1);
			// USB_monitor_string(STR_buffer);

			lower_input_m = p_pointer->input_value[point];
			upper_input_m = p_pointer->input_value[point + 1];
			lower_value_sqm = p_pointer->point_value[point];
			upper_value_sqm = p_pointer->point_value[point + 1];
			if (input_m == lower_input_m)													// catch on-point values
				return lower_value_sqm; 
			else if (input_m == upper_input_m)
				return upper_value_sqm;
			else if ((input_m > lower_input_m) && (input_m < upper_input_m))				// else if value between input values
			{
				remainder = input_m - lower_input_m;											//  linearly scale between values
				scale = upper_input_m - lower_input_m;
				return (lower_value_sqm + ((upper_value_sqm - lower_value_sqm) * (remainder / scale)));
			}
			point++;																		// else move up table
		};
		return 0.0;
	}
}

/******************************************************************************
** Function:	dop_calculate_derived_flow()
**
** Notes:		using measured velocity and depth in pipe
*/
float dop_calculate_derived_flow(void)
{
	float depth_m, theta, radius_m, result_cumps, segment_height_m;
	float area_sqm = 0.0;

	// test code
	//int j;

	depth_m = (float)SER_height_mm / 1000;														// get depth in m from height in mm of water column
	depth_m += DOP_config.sensor_offset_m;														// add sensor offset

	// test code
	/*
	j = sprintf(STR_buffer, "depth = ");
	j += STR_print_float(&STR_buffer[j], depth_m);
	j += sprintf(&STR_buffer[j], " m");
	USB_monitor_string(STR_buffer);
	*/

	if (depth_m == 0)																			// remove zero case
		area_sqm = 0;
	else if (DOP_config.flow_pipe_area_source == 0)												// if calc from look up table
		area_sqm = dop_look_up_derived_area(depth_m);											// look up area of water in pipe
	else
	{																							// else use pipe area formula
		radius_m = DOP_config.flow_pipe_diameter_m / 2;

		// test code
		/*
		j = sprintf(STR_buffer, "radius = ");
		j += STR_print_float(&STR_buffer[j], radius_m);
		j += sprintf(&STR_buffer[j], " m");
		USB_monitor_string(STR_buffer);
		*/

		if (depth_m >= DOP_config.flow_pipe_diameter_m)
			area_sqm = DOP_PI * radius_m * radius_m;
		else if (depth_m == radius_m)
			area_sqm = DOP_PI * radius_m * radius_m / 2;
		else
		{
			if (depth_m < radius_m)
				segment_height_m = depth_m;
			else
				segment_height_m = DOP_config.flow_pipe_diameter_m - depth_m;
			theta = 2 * acos((radius_m - segment_height_m)/ radius_m);
			area_sqm = ((theta - sin(theta)) * radius_m * radius_m) / 2;
			if (depth_m > radius_m)
				area_sqm = (DOP_PI * radius_m * radius_m) - area_sqm;
		}
	}

	// test code
	/*
	j = sprintf(STR_buffer, "area = ");
	j += STR_print_float(&STR_buffer[j], area_sqm);
	j += sprintf(&STR_buffer[j], " sq.m");
	USB_monitor_string(STR_buffer);
	j = sprintf(STR_buffer, "velocity = ");
	j += STR_print_float(&STR_buffer[j], DOP_channel.velocity_value_mps);
	j += sprintf(&STR_buffer[j], " m/s");
	USB_monitor_string(STR_buffer);
	*/

	result_cumps = (DOP_channel.velocity_value_mps == FLT_MAX) ?
		FLT_MAX : area_sqm * DOP_channel.velocity_value_mps;

	// test code
	/*
	j = sprintf(STR_buffer, "flow = ");
	j += STR_print_float(&STR_buffer[j], result_cumps);
	j += sprintf(&STR_buffer[j], " cum/s");
	USB_monitor_string(STR_buffer);
	*/

	return result_cumps;
}

/******************************************************************************
** Function:	Update an doppler register & return average value
**
** Notes:		Exits with p->count at 0 if a log or other action is due
*/
float dop_update_register(DOP_register_type *p, uint8 time_enumeration, float value)
{
	float v;

	if (value == FLT_MAX)								// no value
	{
		if (p->count != 0)								// keep the average unaffected
			p->total += p->total / (float)p->count;
		// else leave total as 0
	}
	else
		p->total += value;
	
	p->count++;
	v = 0.0;																				// by default
	if (p->time == DOP_wakeup_time)															// action is due
	{
		v = p->total / (float)p->count;

		p->total = 0.0;
		p->count = 0;
		LOG_set_next_time(&p->time, time_enumeration, true);
	}

	return v;
}

/******************************************************************************
** Function:	dop_log_measurements()
**
** Notes:		enqueue logs for all enabled measurements now, plus headers if required
*/
void dop_log_measurements(bool valid)
{
	RTC_type time_stamp;

	time_stamp.reg32[1] = RTC_now.reg32[1]; 												// calculate timestamp of this data item
	time_stamp.reg32[0] = LOG_get_timestamp(DOP_wakeup_time, DOP_config.log_interval);
	if ((DOP_config.flags &  DOP_MASK_VELOCITY_LOG_ENABLED) != 0x00)						// if velocity enabled
	{
		// get velocity in m/s
		DOP_channel.velocity_value_mps = valid ? 0.001f * SER_water_speed_mmps : FLT_MAX;
		DOP_channel.velocity_value = valid ? DOP_channel.velocity_value_mps * DOP_config.velocity_cal : FLT_MAX;
		if ((LOG_header_mask & (1 << LOG_SERIAL_1_INDEX)) == 0)								// needs header
		{
			LOG_enqueue_value(LOG_SERIAL_1_INDEX,											// enqueue header 
							  LOG_BLOCK_HEADER_TIMESTAMP,
							  (int32)LOG_get_timestamp(DOP_wakeup_time, 
							  DOP_config.log_interval));
			LOG_header_mask |= 1 << LOG_SERIAL_1_INDEX;										// clear header mask bit
		}
		LOG_enqueue_value(LOG_SERIAL_1_INDEX,					 							// enqueue value
						  LOG_DATA_VALUE, 
						  *(int32 *)&DOP_channel.velocity_value);
		FTP_update_retrieval_info(FTP_SER_VELOCITY_INDEX, &time_stamp);						// save ftp retrieval info for velocity
	}
	if ((DOP_config.flags & DOP_MASK_TEMPERATURE_LOG_ENABLED) != 0x00)						// if temperature enabled
	{
		DOP_channel.temperature_value = valid ? ((float)SER_temperature) / 10 : FLT_MAX;
		if ((LOG_header_mask & (1 << LOG_SERIAL_2_INDEX)) == 0)								// needs header
		{
			LOG_enqueue_value(LOG_SERIAL_2_INDEX,											// enqueue header 
							  LOG_BLOCK_HEADER_TIMESTAMP,
							  (int32)LOG_get_timestamp(DOP_wakeup_time, 
							  DOP_config.log_interval));
			LOG_header_mask |= 1 << LOG_SERIAL_2_INDEX;										// clear header mask bit
		}
		LOG_enqueue_value(LOG_SERIAL_2_INDEX, 												// enqueue value
						  LOG_DATA_VALUE, 
						  *(int32 *)&DOP_channel.temperature_value);
		FTP_update_retrieval_info(FTP_SER_TEMPERATURE_INDEX, &time_stamp);					// save ftp retrieval info for temperature
	}
	if ((DOP_config.flags & DOP_MASK_DEPTH_LOG_ENABLED) != 0x00)							// if depth enabled
	{
		// get depth in metres from measurement in mm, & apply cal
		DOP_channel.depth_value = valid ? (float)SER_level_mm / 1000 * DOP_config.depth_cal : FLT_MAX;
		if ((LOG_header_mask & (1 << LOG_SERIAL_3_INDEX)) == 0)								// needs header
		{
			LOG_enqueue_value(LOG_SERIAL_3_INDEX,											// enqueue header 
							  LOG_BLOCK_HEADER_TIMESTAMP,
							  (int32)LOG_get_timestamp(DOP_wakeup_time, 
							  DOP_config.log_interval));
			LOG_header_mask |= 1 << LOG_SERIAL_3_INDEX;										// clear header mask bit
		}
		LOG_enqueue_value(LOG_SERIAL_3_INDEX, 												// enqueue value
						  LOG_DATA_VALUE, 
						  *(int32 *)&DOP_channel.depth_value);
		FTP_update_retrieval_info(FTP_SER_DEPTH_INDEX, &time_stamp);						// save ftp retrieval info for depth
	}
	if ((DOP_config.flags & DOP_MASK_DERIVED_FLOW_LOG_ENABLED) != 0x00)						// if flow enabled
	{
		// get velocity in m/s
		DOP_channel.velocity_value_mps = valid ? 0.001f * SER_water_speed_mmps : FLT_MAX;
		DOP_channel.derived_flow_value = valid ? dop_calculate_derived_flow() * DOP_config.flow_cal : FLT_MAX;
		if ((LOG_derived_header_mask & (1 << LOG_SERIAL_1_INDEX)) == 0)						// needs header 
		{
			LOG_enqueue_value(LOG_SERIAL_1_INDEX | LOG_DERIVED_MASK,						// enqueue header 
							  LOG_BLOCK_HEADER_TIMESTAMP,
							  (int32)LOG_get_timestamp(DOP_wakeup_time, 
							  DOP_config.log_interval));
			LOG_derived_header_mask |= 1 << LOG_SERIAL_1_INDEX;								// clear header mask bit
		}
		LOG_enqueue_value(LOG_SERIAL_1_INDEX | LOG_DERIVED_MASK,							// enqueue value
						  LOG_DATA_VALUE, 
						  *(int32 *)&DOP_channel.derived_flow_value);
		FTP_update_retrieval_info(FTP_SER_DERIVED_FLOW_INDEX, &time_stamp);					// save ftp retrieval info for derived flow
	}
}

/******************************************************************************
** Function:	Update an SMS channel
**
** Notes:		
*/
void dop_update_sms_channel(DOP_register_type *p, uint8 enable_mask, float value, bool valid, uint8 channel_index, bool derived)
{
	float f;
	uint8 c;

	if ((DOP_config.flags & enable_mask) == 0)
		return;

	f = dop_update_register(p, DOP_config.sms_data_interval, value);
	if (p->count != 0)
		return;

	if (!valid)					// log no value
		f = FLT_MAX;

	c = channel_index | LOG_SMS_MASK;														// get channel number for log routines
	if (derived)
	{
		c |= LOG_DERIVED_MASK;
		if ((LOG_derived_sms_header_mask & (1 << channel_index)) == 0)						// needs sms header
		{
			LOG_enqueue_value(c, LOG_BLOCK_HEADER_TIMESTAMP,
							  (int32)LOG_get_timestamp(DOP_wakeup_time, DOP_config.sms_data_interval));
			LOG_derived_sms_header_mask |= (1 << channel_index);							// clear sms header mask bit
		}
	}
	else if ((LOG_sms_header_mask & (1 << channel_index)) == 0)								// needs sms header
	{
		LOG_enqueue_value(c, LOG_BLOCK_HEADER_TIMESTAMP,
						  (int32)LOG_get_timestamp(DOP_wakeup_time, DOP_config.sms_data_interval));
		LOG_sms_header_mask |= (1 << channel_index);										// clear sms header mask bit
	}

	LOG_enqueue_value(c, LOG_DATA_VALUE, *(int32 *)&f);

	c = channel_index - 1;																	// get channel number for PDU routine
	if (derived)
		c = 11;

	if (PDU_time_for_batch(c))																// check for batch mode synchronisation
		PDU_schedule(c);
}

/******************************************************************************
** Function:	Process samples for SMS values
**
** Notes:	
*/
void dop_update_sms_values(bool valid)
{
	if (DOP_config.sms_data_interval == 0)
		return;

	// Log all enabled channels for SMS, regardless of whether messaging or not
	dop_update_sms_channel(&DOP_channel.velocity_sms, DOP_MASK_VELOCITY_LOG_ENABLED,	// + DOP_MASK_VELOCITY_MESSAGING_ENABLED
		DOP_channel.velocity_value,	valid, LOG_SERIAL_1_INDEX, false);

	dop_update_sms_channel(&DOP_channel.temperature_sms, DOP_MASK_TEMPERATURE_LOG_ENABLED,
		DOP_channel.temperature_value, valid, LOG_SERIAL_2_INDEX, false);

	dop_update_sms_channel(&DOP_channel.depth_sms, DOP_MASK_DEPTH_LOG_ENABLED,
		DOP_channel.depth_value, valid, LOG_SERIAL_3_INDEX, false);

	dop_update_sms_channel(&DOP_channel.derived_flow_sms, DOP_MASK_DERIVED_FLOW_LOG_ENABLED,
		DOP_channel.derived_flow_value, valid, LOG_SERIAL_1_INDEX, true);
}

/******************************************************************************
** Function:	Process samples for min/max values
**
** Notes:	
*/
void dop_update_min_max_values(void)
{
	float value;
																									// velocity
	value = dop_update_register(&(DOP_channel.velocity_min_max), DOP_config.min_max_sample_interval, DOP_channel.velocity_value);
	if (DOP_channel.velocity_min_max.count == 0)													// if time to sample velocity min max
	{
		if (value > DOP_channel.velocity_max_value)
		{
			DOP_channel.velocity_max_value = value;
			DOP_channel.velocity_max_time.hr_bcd = RTC_now.hr_bcd;
			DOP_channel.velocity_max_time.min_bcd = RTC_now.min_bcd;
			DOP_channel.velocity_max_time.sec_bcd = RTC_now.sec_bcd;
		}

		if (value < DOP_channel.velocity_min_value)
		{
			DOP_channel.velocity_min_value = value;
			DOP_channel.velocity_min_time.hr_bcd = RTC_now.hr_bcd;
			DOP_channel.velocity_min_time.min_bcd = RTC_now.min_bcd;
			DOP_channel.velocity_min_time.sec_bcd = RTC_now.sec_bcd;
		}
	}
																									// temperature
	value = dop_update_register(&(DOP_channel.temperature_min_max), DOP_config.min_max_sample_interval, DOP_channel.temperature_value);
	if (DOP_channel.temperature_min_max.count == 0)													// if time to sample temperature min max
	{
		if (value > DOP_channel.temperature_max_value)
		{
			DOP_channel.temperature_max_value = value;
			DOP_channel.temperature_max_time.hr_bcd = RTC_now.hr_bcd;
			DOP_channel.temperature_max_time.min_bcd = RTC_now.min_bcd;
			DOP_channel.temperature_max_time.sec_bcd = RTC_now.sec_bcd;
		}

		if (value < DOP_channel.temperature_min_value)
		{
			DOP_channel.temperature_min_value = value;
			DOP_channel.temperature_min_time.hr_bcd = RTC_now.hr_bcd;
			DOP_channel.temperature_min_time.min_bcd = RTC_now.min_bcd;
			DOP_channel.temperature_min_time.sec_bcd = RTC_now.sec_bcd;
		}
	}
																									// depth
	value = dop_update_register(&(DOP_channel.depth_min_max), DOP_config.min_max_sample_interval, DOP_channel.depth_value);
	if (DOP_channel.depth_min_max.count == 0)														// if time to sample depth min max
	{
		if (value > DOP_channel.depth_max_value)
		{
			DOP_channel.depth_max_value = value;
			DOP_channel.depth_max_time.hr_bcd = RTC_now.hr_bcd;
			DOP_channel.depth_max_time.min_bcd = RTC_now.min_bcd;
			DOP_channel.depth_max_time.sec_bcd = RTC_now.sec_bcd;
		}

		if (value < DOP_channel.depth_min_value)
		{
			DOP_channel.depth_min_value = value;
			DOP_channel.depth_min_time.hr_bcd = RTC_now.hr_bcd;
			DOP_channel.depth_min_time.min_bcd = RTC_now.min_bcd;
			DOP_channel.depth_min_time.sec_bcd = RTC_now.sec_bcd;
		}
	}
																									// derived flow
	value = dop_update_register(&(DOP_channel.derived_flow_min_max), DOP_config.min_max_sample_interval, DOP_channel.derived_flow_value);
	if (DOP_channel.derived_flow_min_max.count == 0)												// if time to sample velocity min max
	{
		if (value > DOP_channel.derived_flow_max_value)
		{
			DOP_channel.derived_flow_max_value = value;
			DOP_channel.derived_flow_max_time.hr_bcd = RTC_now.hr_bcd;
			DOP_channel.derived_flow_max_time.min_bcd = RTC_now.min_bcd;
			DOP_channel.derived_flow_max_time.sec_bcd = RTC_now.sec_bcd;
		}

		if (value < DOP_channel.derived_flow_min_value)
		{
			DOP_channel.derived_flow_min_value = value;
			DOP_channel.derived_flow_min_time.hr_bcd = RTC_now.hr_bcd;
			DOP_channel.derived_flow_min_time.min_bcd = RTC_now.min_bcd;
			DOP_channel.derived_flow_min_time.sec_bcd = RTC_now.sec_bcd;
		}
	}
}

/******************************************************************************
** Function:	Process samples for alarms
**
** Notes:	
*/
void dop_update_alarms(void)
{
	float value;
																									// velocity
	if (ALM_config[LOG_SERIAL_1_INDEX - 1].enabled)
	{
		if (DOP_channel.velocity_alarm.time == SLP_NO_WAKEUP)										// if first time - alarm time set to sleep but alarm enabled
		{
			DOP_channel.velocity_alarm.time = DOP_wakeup_time;
			LOG_set_next_time(&(DOP_channel.velocity_alarm.time), 
							  ALM_config[LOG_SERIAL_1_INDEX - 1].sample_interval, 
							  true);
			DOP_channel.velocity_alarm.count = 0;
			DOP_channel.velocity_alarm.total = 0.0;
		}
		else
		{
			value = dop_update_register(&(DOP_channel.velocity_alarm), ALM_config[LOG_SERIAL_1_INDEX - 1].sample_interval, DOP_channel.velocity_value);
			if (DOP_channel.velocity_alarm.count == 0)												// if time to process alarm
				ALM_process_value(LOG_SERIAL_1_INDEX - 1, value);
		}
	}
	else
		DOP_channel.velocity_alarm.time = SLP_NO_WAKEUP;											// alarm not enabled; send alarm to sleep

																									// temperature
	if (ALM_config[LOG_SERIAL_2_INDEX - 1].enabled)
	{
		if (DOP_channel.temperature_alarm.time == SLP_NO_WAKEUP)									// if first time - alarm time set to sleep but alarm enabled
		{
			DOP_channel.temperature_alarm.time = DOP_wakeup_time;
			LOG_set_next_time(&(DOP_channel.temperature_alarm.time), 
							  ALM_config[LOG_SERIAL_2_INDEX - 1].sample_interval, 
							  true);
			DOP_channel.temperature_alarm.count = 0;
			DOP_channel.temperature_alarm.total = 0.0;
		}
		else
		{
			value = dop_update_register(&(DOP_channel.temperature_alarm), ALM_config[LOG_SERIAL_2_INDEX - 1].sample_interval, DOP_channel.temperature_value);
			if (DOP_channel.temperature_alarm.count == 0)											// if time to process alarm
				ALM_process_value(LOG_SERIAL_2_INDEX - 1, value);
		}
	}
	else
		DOP_channel.temperature_alarm.time = SLP_NO_WAKEUP;											// alarm not enabled; send alarm to sleep

																									// depth
	if (ALM_config[LOG_SERIAL_3_INDEX - 1].enabled)
	{
		if (DOP_channel.depth_alarm.time == SLP_NO_WAKEUP)											// if first time - alarm time set to sleep but alarm enabled
		{
			DOP_channel.depth_alarm.time = DOP_wakeup_time;
			LOG_set_next_time(&(DOP_channel.depth_alarm.time), 
							  ALM_config[LOG_SERIAL_3_INDEX - 1].sample_interval, 
							  true);
			DOP_channel.depth_alarm.count = 0;
			DOP_channel.depth_alarm.total = 0.0;
		}
		else
		{
			value = dop_update_register(&(DOP_channel.depth_alarm), ALM_config[LOG_SERIAL_3_INDEX - 1].sample_interval, DOP_channel.depth_value);
			if (DOP_channel.depth_alarm.count == 0)													// if time to process alarm
				ALM_process_value(LOG_SERIAL_3_INDEX - 1, value);
		}
	}
	else
		DOP_channel.depth_alarm.time = SLP_NO_WAKEUP;												// alarm not enabled; send alarm to sleep

																									// derived flow
	if (ALM_config[ALM_ALARM_DERIVED_CHANNEL0].enabled)
	{
		if (DOP_channel.derived_flow_alarm.time == SLP_NO_WAKEUP)									// if first time - alarm time set to sleep but alarm enabled
		{
			DOP_channel.derived_flow_alarm.time = DOP_wakeup_time;
			LOG_set_next_time(&(DOP_channel.derived_flow_alarm.time), 
							  ALM_config[ALM_ALARM_DERIVED_CHANNEL0].sample_interval, 
							  true);
			DOP_channel.derived_flow_alarm.count = 0;
			DOP_channel.derived_flow_alarm.total = 0.0;
		}
		else
		{
			value = dop_update_register(&(DOP_channel.derived_flow_alarm), ALM_config[ALM_ALARM_DERIVED_CHANNEL0].sample_interval, DOP_channel.derived_flow_value);
			if (DOP_channel.derived_flow_alarm.count == 0)											// if time to process alarm
				ALM_process_value(ALM_ALARM_DERIVED_CHANNEL0, value);
		}
	}
	else
		DOP_channel.derived_flow_alarm.time = SLP_NO_WAKEUP;										// alarm not enabled; send alarm to sleep
}

/******************************************************************************
** Function:	Midnight task
**
** Notes:	
*/
void dop_channel_midnight_task(void)
{
	if ((DOP_config.flags & DOP_MASK_VELOCITY_LOG_ENABLED) != 0x00)							// if velocity enabled
		LOG_enqueue_value(LOG_SERIAL_1_INDEX, LOG_BLOCK_FOOTER, 0);
	if ((DOP_config.flags & DOP_MASK_TEMPERATURE_LOG_ENABLED) != 0x00)						// if temperature enabled
		LOG_enqueue_value(LOG_SERIAL_2_INDEX, LOG_BLOCK_FOOTER, 0);
	if ((DOP_config.flags & DOP_MASK_DEPTH_LOG_ENABLED) != 0x00)							// if depth enabled
		LOG_enqueue_value(LOG_SERIAL_3_INDEX, LOG_BLOCK_FOOTER, 0);
	if ((DOP_config.flags & DOP_MASK_DERIVED_FLOW_LOG_ENABLED) != 0x00)						// if flow enabled
		LOG_enqueue_value(LOG_SERIAL_1_INDEX | LOG_DERIVED_MASK, LOG_BLOCK_FOOTER, 0);
	LOG_flush();																			// flush now so files are updated immediately
}

/******************************************************************************
** Function:	get one or two values from the line in the apipe table file
**
** Notes:		returns false if no such line
*/
bool dop_get_table_values(int line, float * input, float * output)
{
	char * p_line = STR_buffer;

	if (CFS_read_line((char *)CFS_config_path, (char *)CFS_apipe_name, line, STR_buffer, 140) > 0)
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
** Function:	doppler sensor present on successful one shot or initialisation
**
** Notes:		set log header flags if was not present before this
*/
void dop_sensor_present(void)
{
	if (!DOP_sensor_present)
	{
		LOG_header_mask &= ~(0x000E);
		LOG_sms_header_mask &= ~(0x000E);
		LOG_derived_header_mask &= ~(0x0002);
		LOG_derived_sms_header_mask &= ~(0x0002);
	}
	DOP_sensor_present = true;
}

/******************************************************************************
** Function:	stop doppler sensor at end of any activity
**
** Notes:		
*/
void dop_stop(void)
{
	if (dop_midnight_task_pending)												// have just done our midnight logs (or failed)
	{
		dop_midnight_task_pending = false;
		dop_channel_midnight_task();
	}
	HDW_RS485_BOOST_ON = false;													// ensure the Nivus Sensor is off
	dop_state = DOP_IDLE;														// back to idle
}

/* public functions */

/******************************************************************************
** Function:	retrieve file APIPE.CAL to derived config
**
** Notes:
*/
bool DOP_retrieve_derived_conversion_data(void)
{
	int   point, table_points;
	float temp_value;
	DOP_derived_config_type * p_pointer;

	p_pointer = &DOP_derived_config;

	if (CFS_file_exists((char *)CFS_config_path, (char *)CFS_apipe_name))						// check apipe file exists
	{																							// table is min, max, n points, lines 0 to n-1: input,output 
		if (!dop_get_table_values(1, &(p_pointer->min_value), &temp_value))
			return false;
		if (!dop_get_table_values(2, &(p_pointer->max_value), &temp_value))
			return false;
		if (CFS_read_line((char *)CFS_config_path, (char *)CFS_apipe_name, 3, STR_buffer, 140) > 0)
			sscanf(STR_buffer, "%d", &table_points);
		else
			return false;
		if (table_points > DOP_DEPTH_TO_AREA_POINTS)											// test for too many points
			return false;
		p_pointer->K_value = (float)table_points;
		point = 0;
		while (point < table_points)															// get table entries
		{
			if (!dop_get_table_values(4 + point, &(p_pointer->input_value[point]), &(p_pointer->point_value[point])))
				return false;
			point++;																			// move up table
		};
		return true;
	}
	else
		return false;
}

/******************************************************************************
** Function:	DOP_configure_sensor
**
** Notes:
*/
void DOP_configure_sensor(void)
{
	if ((DOP_config.sensor_flags & DOP_MASK_SENSOR_ENABLED) != 0x00)
	{
		DOP_wakeup_time = RTC_time_sec;
		LOG_set_next_time(&DOP_wakeup_time, DOP_config.log_interval, true);			// set first logging time

		DOP_channel.velocity_sms.count = 0;
		DOP_channel.velocity_sms.total = 0.0;
		DOP_channel.velocity_sms.time = RTC_time_sec;
		LOG_set_next_time(&DOP_channel.velocity_sms.time, DOP_config.sms_data_interval, true);

		DOP_channel.temperature_sms.count = 0;
		DOP_channel.temperature_sms.total = 0.0;
		DOP_channel.temperature_sms.time = RTC_time_sec;
		LOG_set_next_time(&DOP_channel.temperature_sms.time, DOP_config.sms_data_interval, true);

		DOP_channel.depth_sms.count = 0;
		DOP_channel.depth_sms.total = 0.0;
		DOP_channel.depth_sms.time = RTC_time_sec;
		LOG_set_next_time(&DOP_channel.depth_sms.time, DOP_config.sms_data_interval, true);

		DOP_channel.derived_flow_sms.count = 0;
		DOP_channel.derived_flow_sms.total = 0.0;
		DOP_channel.derived_flow_sms.time = RTC_time_sec;
		LOG_set_next_time(&DOP_channel.derived_flow_sms.time, DOP_config.sms_data_interval, true);

		DOP_channel.velocity_min_max.count = 0;
		DOP_channel.velocity_min_max.total = 0.0;
		DOP_channel.velocity_min_max.time = RTC_time_sec;
		LOG_set_next_time(&DOP_channel.velocity_min_max.time, DOP_config.min_max_sample_interval, true);

		DOP_channel.temperature_min_max.count = 0;
		DOP_channel.temperature_min_max.total = 0.0;
		DOP_channel.temperature_min_max.time = RTC_time_sec;
		LOG_set_next_time(&DOP_channel.temperature_min_max.time, DOP_config.min_max_sample_interval, true);

		DOP_channel.depth_min_max.count = 0;
		DOP_channel.depth_min_max.total = 0.0;
		DOP_channel.depth_min_max.time = RTC_time_sec;
		LOG_set_next_time(&DOP_channel.depth_min_max.time, DOP_config.min_max_sample_interval, true);

		DOP_channel.derived_flow_min_max.count = 0;
		DOP_channel.derived_flow_min_max.total = 0.0;
		DOP_channel.derived_flow_min_max.time = RTC_time_sec;
		LOG_set_next_time(&DOP_channel.derived_flow_min_max.time, DOP_config.min_max_sample_interval, true);
		
		DOP_channel.velocity_alarm.count = 0;
		DOP_channel.velocity_alarm.total = 0.0;
		DOP_channel.velocity_alarm.time = SLP_NO_WAKEUP;

		DOP_channel.temperature_alarm.count = 0;
		DOP_channel.temperature_alarm.total = 0.0;
		DOP_channel.temperature_alarm.time = SLP_NO_WAKEUP;

		DOP_channel.depth_alarm.count = 0;
		DOP_channel.depth_alarm.total = 0.0;
		DOP_channel.depth_alarm.time = SLP_NO_WAKEUP;

		DOP_channel.derived_flow_alarm.count = 0;
		DOP_channel.derived_flow_alarm.total = 0.0;
		DOP_channel.derived_flow_alarm.time = SLP_NO_WAKEUP;

		// When values are first logged with new config, ensure a header is inserted.
		LOG_header_mask &= ~(0x000E);
		LOG_sms_header_mask &= ~(0x000E);
		LOG_derived_header_mask &= ~(0x0002);
		LOG_derived_sms_header_mask &= ~(0x0002);

																					// activate retrieval data
		FTP_activate_retrieval_info(FTP_SER_VELOCITY_INDEX);
		FTP_activate_retrieval_info(FTP_SER_TEMPERATURE_INDEX);
		FTP_activate_retrieval_info(FTP_SER_DEPTH_INDEX);
		FTP_activate_retrieval_info(FTP_SER_DERIVED_FLOW_INDEX);
		dop_sensor_running = true;
	}
	else if ((DOP_config.sensor_flags & DOP_MASK_SENSOR_ENABLED) == 0x00)
	{
		DOP_wakeup_time = SLP_NO_WAKEUP;											// stop wakup - no more logging
		DOP_channel.velocity_alarm.time = SLP_NO_WAKEUP;
		DOP_channel.temperature_alarm.time = SLP_NO_WAKEUP;
		DOP_channel.depth_alarm.time = SLP_NO_WAKEUP;
		DOP_channel.derived_flow_alarm.time = SLP_NO_WAKEUP;

		dop_sensor_running = false;
	}
}

/******************************************************************************
** Function:	create file footer in string supplied, for a doppler channel
**
** Notes:		channel is 0 or 1, derived bool supplied
**				Add signal strength, batt volts & \r\n, after calling this.
*/
int DOP_create_block_footer(char * string, uint8 channel, bool derived)
{
	int len = 0;

	if (DOP_config.min_max_sample_interval == 0)											// if min/max interval is zero
		len = sprintf(string, "\r\n*,,,");
	else if (!derived)
	{
		switch (channel)
		{
		case 0:
			len = LOG_print_footer_min_max(string, &DOP_channel.velocity_min_time, DOP_channel.velocity_min_value,
						&DOP_channel.velocity_max_time, DOP_channel.velocity_max_value);
			DOP_channel.velocity_min_value = FLT_MAX;
			DOP_channel.velocity_max_value = -FLT_MAX;
			*(uint32 *)&DOP_channel.velocity_min_time = 0;
			*(uint32 *)&DOP_channel.velocity_max_time = 0;
			break;

		case 1:
			len = LOG_print_footer_min_max(string, &DOP_channel.temperature_min_time, DOP_channel.temperature_min_value,
						&DOP_channel.temperature_max_time, DOP_channel.temperature_max_value);
			DOP_channel.temperature_min_value = FLT_MAX;
			DOP_channel.temperature_max_value = -FLT_MAX;
			*(uint32 *)&DOP_channel.temperature_min_time = 0;
			*(uint32 *)&DOP_channel.temperature_max_time = 0;
			break;

		default:	// channel 2
			len = LOG_print_footer_min_max(string, &DOP_channel.depth_min_time, DOP_channel.depth_min_value,
						&DOP_channel.depth_max_time, DOP_channel.depth_max_value);
			DOP_channel.depth_min_value = FLT_MAX;
			DOP_channel.depth_max_value = -FLT_MAX;
			*(uint32 *)&DOP_channel.depth_min_time = 0;
			*(uint32 *)&DOP_channel.depth_max_time = 0;
			break;
		}
	}
	else if (channel == 0)
	{
		len = LOG_print_footer_min_max(string, &DOP_channel.derived_flow_min_time, DOP_channel.derived_flow_min_value,
					&DOP_channel.derived_flow_max_time, DOP_channel.derived_flow_max_value);
		DOP_channel.derived_flow_min_value = FLT_MAX;
		DOP_channel.derived_flow_max_value = -FLT_MAX;
		*(uint32 *)&DOP_channel.derived_flow_min_time = 0;
		*(uint32 *)&DOP_channel.derived_flow_max_time = 0;
	}

	return len;
}

/******************************************************************************
** Function:	return immediate value of given channel
**
** Notes:		normal or derived, channel ID can be 0x81, 0x82 or 0x89
*/
float DOP_immediate_value(uint8 channel_id)
{
	float value = 0.0;

	if (LOG_state > LOG_STOPPED)												// if logging
	{
		if (channel_id == 0x81)													// velocity
		{
			if ((DOP_config.flags & DOP_MASK_VELOCITY_LOG_ENABLED) != 0)		// if enabled
				value = DOP_channel.velocity_value;								// get value
		}
		else if (channel_id == 0x82)											// temperature
		{
			if ((DOP_config.flags & DOP_MASK_TEMPERATURE_LOG_ENABLED) != 0)		// if enabled
				value = DOP_channel.temperature_value;							// get value
		}
		else if (channel_id == 0x83)											// depth
		{
			if ((DOP_config.flags & DOP_MASK_DEPTH_LOG_ENABLED) != 0)			// if enabled
				value = DOP_channel.depth_value;								// get value
		}
		else if (channel_id == 0x89)											// flow
		{
			if ((DOP_config.flags & DOP_MASK_DERIVED_FLOW_LOG_ENABLED) != 0)	// if enabled
				value = DOP_channel.derived_flow_value;							// get value
		}
	}

	return value;
}

/******************************************************************************
** Function:	DOP_start_sensor
**
** Notes:		called by command line commands - do not require logging
*/
void DOP_start_sensor(void)
{
	dop_start_pending = true;
}

/******************************************************************************
** Function:	DOP_busy
**
** Notes:		return true if so
*/
bool DOP_busy(void)
{
	return ((dop_state != DOP_IDLE) || dop_start_pending);
}

/******************************************************************************
** Function:	Comms failure
**
** Notes:		Suppress logging failure by passing in 0
*/
void dop_comms_fail(int line)
{
	if (dop_log_pending)												// only if logging sample
	{
		dop_log_measurements(false);									// log no values
		dop_update_sms_values(false);									// log no values
		DOP_wakeup_time = RTC_time_sec;									// set next logging time
		LOG_set_next_time(&DOP_wakeup_time, DOP_config.log_interval, false);
	}

	DOP_sensor_present = false;
	DOP_nivus_code = 'F';
	if (line > 0)
		LOG_enqueue_value(LOG_ACTIVITY_INDEX, LOG_DOP_FILE, line);		// DOP comms failed
	dop_stop();				
}

/******************************************************************************
** Function:	DOP_task
**
** Notes:		called all the time processor is awake
**				processor may have been awakened by timed wakeup relevant to DOP
*/
void DOP_task(void)
{
	if (TIM_20ms_tick)																		// run timer																		
	{
		if (dop_20ms_timer > 0)
			dop_20ms_timer--;
	}

	switch (dop_state)																		// state machine
	{
	case DOP_IDLE:																			// can sleep in this state
		dop_log_pending = false;
		if (((DOP_wakeup_time <= RTC_time_sec) && dop_sensor_running) || dop_start_pending)	// if pending or scheduled measurement
		{
			if (!dop_start_pending)															// if a scheduled measurement
			{
				if (DOP_wakeup_time == 0)													// if midnight log time
					dop_midnight_task_pending = true;										// set flag so midnight task runs after averaging loop
				DOP_nivus_code = 'M';														// set Measurement code
				dop_log_pending = true;														// request measurement to be logged
			}
			dop_start_pending = false;														// clear pending flag
			dop_20ms_timer = 50;															// set 1 second delay
			if ((USB_state == USB_EXT_BATTERY) || (USB_state > USB_PC_DETECTED))			// OK to power the transducer
			{
				HDW_RS485_BOOST_ON = true;													// turn on power out boost - powers the Nivus Sensor
				dop_state = DOP_START;	 													// start sequence
			}
			else
				dop_comms_fail(0);															// no external power or USB
		}
		break;	

	case DOP_START:
		if (dop_20ms_timer == 0)														// at end of start delay
		{
			dop_state = DOP_ONE_SHOT_REPLY;												// by default
			switch (DOP_nivus_code)														// send required command to Nivus
			{
			case 'B':
				SER_send_baud_rate();
				break;

			case 'H':
				SER_send_height();
				break;

			case 'I':
				SER_send_get_id();
				break;

			case 'M':									// starts a meaasurement sequence of 9 seconds
				SER_send_write_init();
				dop_state = DOP_MEASURE_INIT;
				break;

			case 'R':
				SER_send_read_init();
				break;

			case 'T':
				SER_send_reset();
				break;

			case 'V':
				SER_send_sound_velocity();
				break;

			case 'W':
				SER_send_write_init();
				break;

			default:
				dop_stop();			
				break;
			}
		}
		break;

	case DOP_ONE_SHOT_REPLY:
		if (!SER_busy())																	// wait for end of communication reply
		{
			if (SER_success)
			{
				dop_sensor_present();
				dop_stop();																	// back to idle
			}
			else
				dop_comms_fail(__LINE__);
		}
		break;

	case DOP_MEASURE_INIT:
		if (!SER_busy())																// wait for init reply
		{
			if (SER_success)
			{
				dop_sensor_present();
				SER_send_read_measurement();											// read measurements to get water temperature
				dop_measure_loop_count = 0;
				dop_state = DOP_READING_MEASUREMENT;
			}
			else
				dop_comms_fail(__LINE__);
		}
		break;

	case DOP_READING_MEASUREMENT:
		if (!SER_busy())																// got measurement reply
		{
			if (SER_success)
			{
				if (USB_echo)
				{
					sprintf(STR_buffer, "Counter %d, speed = %d", dop_measure_loop_count, SER_water_speed_mmps); 
					USB_monitor_string(STR_buffer);
				}

				// get signed value with max amplitude
				//if (abs(SER_water_speed_mmps) > abs(dop_velocity_mmps))
				//	dop_velocity_mmps = SER_water_speed_mmps;

				if (++dop_measure_loop_count >= SER_n_samples)							// end of loop
				{
					if (dop_log_pending)												// only if logging sample
					{
						dop_log_measurements(true);										// got a measurement - go and calculate all required
						dop_update_sms_values(true);									// check sms
						dop_update_min_max_values();		 							// check min and max
						dop_update_alarms();											// check alarms
						DOP_wakeup_time = RTC_time_sec;									// set next logging time
						LOG_set_next_time(&DOP_wakeup_time, DOP_config.log_interval, false);
					}
					dop_stop();				
				}
				else
				{
					// get height of water column - if depth from sensor, get it in mm from last measurement in mm
					// else get it in mm from defined depth sensor channel
					SER_height_mm = ((DOP_config.sensor_flags & DOP_MASK_DEPTH_SOURCE) == 0x00) ?
						SER_level_mm : (uint16)(ANA_channel[DOP_config.depth_data_channel].sample_value * 1000);

					if (USB_echo)
					{
						sprintf(STR_buffer, "Height = %d", SER_height_mm); 
						USB_monitor_string(STR_buffer);
					}

					SER_send_height();
					dop_state = DOP_WRITING_HEIGHT;
				}
			}
			else
				dop_comms_fail(__LINE__);
		}
		break;

	case DOP_WRITING_HEIGHT:
		if (!SER_busy())																// got height reply
		{
			if (SER_success)
			{
				SER_velocity = dop_calc_velocity(SER_temperature);						// write speed of sound in water at temperature

				if (USB_echo)
				{
					sprintf(STR_buffer, "Velocity = %d", SER_velocity); 
					USB_monitor_string(STR_buffer);
				}

				SER_send_sound_velocity();
				dop_state = DOP_WRITING_VELOCITY;
			}
			else
				dop_comms_fail(__LINE__);
		}
		break;

	case DOP_WRITING_VELOCITY:
		if (!SER_busy())																// got velocity reply
		{
			if (SER_success)
			{
				dop_20ms_timer = 48;													// Nearly 1s delay
				dop_state = DOP_DELAY;
			}
			else
				dop_comms_fail(__LINE__);
		}
		break;

	case DOP_DELAY:
		if (dop_20ms_timer == 0)														// at end of delay
		{
			SER_send_read_measurement();
			dop_state = DOP_READING_MEASUREMENT;
		}
		break;

	default:
		dop_stop();																		// back to idle
		break;
	}
}

/******************************************************************************
** Function:	DOP_init
**
** Notes:		set defaults
*/
void DOP_init(void)
{
	dop_stop();																				// ensure the Nivus Sensor is off
	dop_sensor_running = false;
	DOP_config.flags = 0x00;																// disable all flags
	DOP_config.sensor_flags = 0x00;
	DOP_config.velocity_cal = 1.0;															// default cal multipliers
	DOP_config.depth_cal = 1.0;
	DOP_config.flow_cal = 1.0;
	DOP_config.log_interval = 9;															// default intervals
	DOP_config.min_max_sample_interval = 9;
	DOP_config.sms_data_interval = 9;
	DOP_wakeup_time = SLP_NO_WAKEUP;														// clear all times
	DOP_channel.velocity_sms.time = SLP_NO_WAKEUP;
	DOP_channel.temperature_sms.time = SLP_NO_WAKEUP;
	DOP_channel.depth_sms.time = SLP_NO_WAKEUP;
	DOP_channel.derived_flow_sms.time = SLP_NO_WAKEUP;
	DOP_channel.velocity_min_max.time = SLP_NO_WAKEUP;
	DOP_channel.temperature_min_max.time = SLP_NO_WAKEUP;
	DOP_channel.depth_min_max.time = SLP_NO_WAKEUP;
	DOP_channel.derived_flow_min_max.time = SLP_NO_WAKEUP;
	DOP_channel.velocity_alarm.time = SLP_NO_WAKEUP;
	DOP_channel.temperature_alarm.time = SLP_NO_WAKEUP;
	DOP_channel.depth_alarm.time = SLP_NO_WAKEUP;
	DOP_channel.derived_flow_alarm.time = SLP_NO_WAKEUP;
}

#endif





