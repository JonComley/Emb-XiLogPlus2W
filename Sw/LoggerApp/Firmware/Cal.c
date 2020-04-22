/******************************************************************************
** File:	Cal.c
**
** Notes:	Calibration	& build info
**
**
** V2.64 220311 PB  New local function cal_set_default_build called if no file system or no build file
**                  Sets modem present by default		
**
** V3.04 050112 PB WasteWater - add number of control outputs to build info
**
** V3.08 280212 PB  ECO product - read line 9 of BUILD.CAL for ECO variant
**					if ECO, then read ECO.TXT for eco config parameters
**					default build and line 9 is NOT eco version - clear flag in config
**
** V3.17 091012 PB  bring up to date with Xilog+ V3.06 - use return value from CFS_open()
**
** V4.00 220114 PB  if HDW_GPS disable all analogue calls and functions
*/

#include <stdio.h>
#include "Custom.h"
#include "Compiler.h"
#include "MDD File System/SD-SPI.h"
#include "MDD File System/FSIO.h"

#include "Cfs.h"
#include "Str.h"
#include "rtc.h"
#include "Ana.h"
#include "Usb.h"
#include "Log.h"
#include "Pwr.h"

#define extern
#include "Cal.h"
#undef extern

const char cal_no_data[] = "<NO DATA>";

// private functions

/******************************************************************************
** Function:	Set default build
**
** Notes:		
*/
void cal_set_default_build(void)
{
	LOG_enqueue_value(LOG_ACTIVITY_INDEX, LOG_CAL_FILE, __LINE__);	// Failed to read build file
	USB_monitor_prompt("Failed to read build file");
	memset(&CAL_build_info, 0, sizeof(CAL_build_info));
	sprintf(STR_buffer, "%s,0", cal_no_data);
	strcpy(CAL_build_info.serial_number, cal_no_data);
	CAL_build_info.digital_wiring_option = 'N';
	CAL_build_info.modem = true;									// default modem present
	PWR_eco_config.flags &= ~PWR_ECO_PRODUCT;						// not eco product
}

/***********************************************************************************************
** Function:	next field
**
** Notes:		return pointer to char after next comma
*/
char * cal_next_field(char * pointer)
{
	char * p = pointer;

	while (*p++ != '\0')
	{
		if (*p == ',')
		{
			p++;
			return p;																			// if find comma return pointer to next char
		}
	}
	p--;																						// if premature end of string point at terminator
	return p;
}

/***********************************************************************************************
** Function:	set binary
**
** Notes:		return 3 bit flag from field - copes with short or long field
*/
uint8 cal_set_binary(char * pointer)
{
	uint8 index = 0;
	uint8 result = 0x00;
	char * p = pointer;

	while (index < 3)
	{
		result <<= 1;
		if (*p == '1')
		{
			result |= 0x01;
		}
		else if (*p != '0')
		{
			return result;
		}
		p++;
		index++;
	}
	return result;
}
/******************************************************************************
** Function:	Read logger model, serial number & channel definitions
**
** Notes:		Requires file system access. Puts logger type & pressure rating in STR_buffer
*/
void CAL_read_build_info(void)
{
	int i;
	unsigned int x;
	float pressure_rating;
	char * p;

	if (!CFS_open())
	{
		LOG_enqueue_value(LOG_ACTIVITY_INDEX, LOG_CAL_FILE, __LINE__);	// Couldn't read build file
		cal_set_default_build();
		return;
	}

	// Read modem flag
	if (CFS_read_line((char *)CFS_cal_path, (char *)CFS_build_name, 4, STR_buffer, 80) < 1)
	{
		cal_set_default_build();
		return;
	}
	// else:

	CAL_build_info.modem = (STR_buffer[0] != '0');

	// Read serial number:
	CFS_read_line((char *)CFS_cal_path, (char *)CFS_build_name, 5,
				  CAL_build_info.serial_number, sizeof(CAL_build_info.serial_number));

	// Read number of digital channels:
	CFS_read_line((char *)CFS_cal_path, (char *)CFS_build_name, 6, STR_buffer, 128);
	sscanf(STR_buffer, "%d", &CAL_build_info.num_digital_channels);

	// Read analogue channel types
	memset(CAL_build_info.analogue_channel_types, '\0', sizeof(CAL_build_info.analogue_channel_types));
	CFS_read_line((char *)CFS_cal_path, (char *)CFS_build_name, 7,
				  CAL_build_info.analogue_channel_types, sizeof(CAL_build_info.analogue_channel_types));

	// Read digital wiring option:
	CFS_read_line((char *)CFS_cal_path, (char *)CFS_build_name, 8, STR_buffer, 128);
	CAL_build_info.digital_wiring_option = STR_buffer[0];

	CFS_read_line((char *)CFS_cal_path, (char *)CFS_build_name, 9, STR_buffer, 10);				// Read eco product option:
	if ((STR_buffer[0] == 'E') || (STR_buffer[0] == 'e'))
	{
		PWR_eco_config.flags |= PWR_ECO_PRODUCT;												// is eco product
																								// attempt to read defaults from eco config file line 2
		if (CFS_read_line((char *)CFS_config_path, (char *)CFS_eco_name, 2, STR_buffer, 80) < 1)
			PWR_eco_init();																		// if fault then set defaults
		else
		{																						// parse STR_buffer into eco defaults - assumes a correct string
			p = STR_buffer;
			sscanf(p, "%f", &(PWR_eco_config.low_alarm_threshold));
			p = cal_next_field(p);
			sscanf(p, "%u", &x);
			PWR_eco_config.cla_val_sample_interval = (uint8)(x & 0x00ff);
			p = cal_next_field(p);
			sscanf(p, "%u", &x);
			PWR_eco_config.internal_sample_interval = (uint8)(x & 0x00ff);
			p = cal_next_field(p);
			PWR_eco_config.eco_power_loss_mask = cal_set_binary(p);
			p = cal_next_field(p);
			PWR_eco_config.eco_power_restored_mask = cal_set_binary(p);
		}
		PWR_wakeup_time = RTC_time_sec;															// start sampling eco power
		if ((PWR_eco_config.flags & PWR_ECO_BELOW_THRESHOLD) != 0x00)							// if below eco threshold flag set
			LOG_set_next_time(&PWR_wakeup_time, PWR_eco_config.internal_sample_interval, false);
		else
			LOG_set_next_time(&PWR_wakeup_time, PWR_eco_config.cla_val_sample_interval, false);
	}
	else
		PWR_eco_config.flags &= ~PWR_ECO_PRODUCT;												// not eco product if no line or not E

	// Read number of control outputs:
	CFS_read_line((char *)CFS_cal_path, (char *)CFS_build_name, 10, STR_buffer, 128);
	sscanf(STR_buffer, "%d", &CAL_build_info.num_control_outputs);

	// Read pressure rating:
	CFS_read_line((char *)CFS_cal_path, (char *)CFS_build_name, 3, STR_buffer, 128);
	sscanf(STR_buffer, "%f", &pressure_rating);

	// Leave logger type & pressure rating in STR_buffer:
	i = CFS_read_line((char *)CFS_cal_path, (char *)CFS_build_name, 2, STR_buffer, 128);
	if (i < 0)
		i = 0;
	STR_buffer[i++] = ',';
	STR_print_float(&STR_buffer[i], pressure_rating);

#ifndef HDW_GPS
	// Enter the bar rating into parameter e1 of all internal pressure channels:
	for (i = 0; i < sizeof(CAL_build_info.analogue_channel_types); i++)
	{
		if (CAL_build_info.analogue_channel_types[i] == 'P')
		{
			ANA_config[i].e1 = pressure_rating;
			ANA_config[i].sensor_type = ANA_SENSOR_DIFF_MV;
		}
	}
#endif
}

/******************************************************************************
** Function:	Read coefficients for an analogue channel
**
** Notes:		Returns pointer to start of values in STR_buffer, or NULL if fails
*/
char * CAL_read_analogue_coefficients(int index)
{
	int i;

#if (HDW_NUM_CHANNELS == 3)
	// if shadow channel (3-ch HW only), use CAL file for the main channel
	// index 2 = A3 = shadow of A1 => index 0
	// index 3 = A4 = shadow of A2 => index 1
	index &= 1;
#endif

	sprintf(STR_buffer, "ANA%02d.CAL", index + 1);
	if (!CFS_read_file((char *)CFS_cal_path, STR_buffer, STR_buffer, sizeof(STR_buffer)))
		return NULL;
	// else:

	// read past 1-line comment header
	for (i = 0; i < 128; i++)
	{
		if ((STR_buffer[i] == '\n') || (STR_buffer[i] == '\0'))
			break;
	}

	return &STR_buffer[i];
}



