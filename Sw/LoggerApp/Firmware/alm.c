/******************************************************************************
** File:	alm.c
**
** Notes:	A picture of what we will have:

-------------------------------------------	Alarm generated above here

		Deadband

===========================================	High Threshold (Alarm & Event)

		Deadband

-------------------------------------------	After an Alarm, no more events will be generated until this
											point is passed going down. High alarm cleared message
											generated here.

											Normal between high and low thresholds


-------------------------------------------	After an Alarm, no more events will be generated until this
											point is passed going up. Low alarm cleared message
		Deadband							generated here.

===========================================	Low Threshold (Alarm & Event)

		Deadband

-------------------------------------------	Alarm generated below here
**
** Changes:
** v2.38 031110 PB  Add bool ALM_com_mode_alarm_enable flag and default setting to true
**
** v2.49 280111 PB  alm_get_string modified to return immediately if it finds empty string
**
** Waste Water
**
** v3.01 101011 PB  subchannel working registers are now in an array
**
** V3.30 221111 PB  code for derived values
**
** V3.08 280212 PB  add fn ALM_log_pwr_alarm() to log power changeover alarms
**					new function ALM_send_ftp_alarm()
** V3.17 091012 PB  bring up to date with Xilog+ V3.06 - use of CFS_open() in alm_log_alarm(), ALM_task()
**
** V3.26 170613 PB  run script files on entering and exiting an alarm. alm_enter_alarm(), alm_exit_alarm(), ALM_process_event()
**
** V3.27 250613 PB	changes to ALM_update_profile() and its use
**
** V3.29 171013 PB  bring up to date with Xilog+ V3.05
**					use new log state LOG_BATT_DEAD instead of LOG_VOLTAGE_TOO_LOW
**
** V3.31 131013 PB  use alm_day_bcd to force recalc of wakeup times at midnight
**
** V3.32 201113 PB  reposition alm_day_bcd = RTC_now.day_bcd after midnight test
**
** V3.33 261113 PB  wait for logging and pdu to complete and file system open in states ALM_MESSAGE_PENDING, ALM_GET_NEXT_LEVELS and ALM_SCRIPT_PENDING
**
** V4.00 220114 PB  if HDW_GPS disable all analogue calls and functions
*/

#include <float.h>

#include "custom.h"
#include "compiler.h"
#include "str.h"
#include "HardwareProfile.h"
#include "MDD File System/FSIO.h"
#include "cfs.h"
#include "Msg.h"
#include "usb.h"
#include "Rtc.h"
#include "Log.h"
#include "Ana.h"
#include "Dig.h"
#include "Dop.h"
#include "Pdu.h"
#include "ftp.h"
#include "cmd.h"
#include "com.h"
#include "slp.h"
#include "Scf.h"

#define extern
#include "alm.h"
#undef extern

// Low-level function in FSIO.c:
int FS_read_line(FSFILE *stream, int line_number, char *ptr, size_t max_bytes);

// Local ALM task states:
#define ALM_IDLE				0
#define ALM_MESSAGE_PENDING		1
#define ALM_GET_NEXT_LEVELS		2
#define ALM_SCRIPT_PENDING		3

// Alarm config action masks:
#define ALM_ACTION_LOG	_B00000100
#define ALM_ACTION_SMS	_B00000010
#define ALM_ACTION_FTP	_B00000001

// Alarm channel masks
#define ALM_MASK_IN_HIGH_ALARM		_B00000001
#define ALM_MASK_ENTER_HIGH_ALARM	_B00000010
#define ALM_MASK_EXIT_HIGH_ALARM	_B00000100
#define ALM_MASK_IN_LOW_ALARM		_B00010000
#define ALM_MASK_ENTER_LOW_ALARM	_B00100000
#define ALM_MASK_EXIT_LOW_ALARM		_B01000000

// alarm threshold types
#define ALM_FIXED_ALARM		0
#define ALM_PROFILE_ALARM	1
#define ALM_ENVELOPE_ALARM	2

#define ALM_MASK_PENDING_MESSAGE	(ALM_MASK_ENTER_HIGH_ALARM | ALM_MASK_EXIT_HIGH_ALARM | \
									 ALM_MASK_ENTER_LOW_ALARM | ALM_MASK_EXIT_LOW_ALARM)

bool alm_read_next_thresholds;

char *alm_char_ptr;

uint8  alm_state;
uint8  alm_info_channel;
uint8  alm_next_timeslot;
uint8  alm_day_bcd;
int    alm_index;
uint16 alm_ftp_msg_index;
uint16 alm_script_flags[5];
uint32 alm_profile_time;

ALM_config_type *alm_p_config;
ALM_channel_type *alm_p_channel;

FAR	char  alm_filename_str[16];
FAR	char  alm_path_str[32];

//*******************************************************************
// private functions
//*******************************************************************

/******************************************************************************
** Function:	read an alarm profile or envelope value from file
**
** Notes:		provide filename in alm_filename_str. Index = 0..23
*/
float alm_extract_threshold(uint8 index)
{
	uint8 j;
	uint32 w;

	if (!CFS_read_file((char *)ALM_PROFILES_PATH, alm_filename_str, STR_buffer, sizeof(STR_buffer)))
		return FLT_MAX;		// no value
	// else:

	// shift value into mask w 1 hex byte at a time. Assumes correct syntax in file
	index *= 6;		// character index
	j = index + 6;
	w = 0;
	do
	{
		// NB byte order is reversed. Nibbles are not.
		w >>= 8;

		// don't amalgamate the next 2 lines, or the order of the auto-inc may be wrong
		HIGH16(w) = (STR_parse_hex_digit(STR_buffer[index++]) << 4);
		HIGH16(w) |= STR_parse_hex_digit(STR_buffer[index++]);
	} while (index < j);

	return STR_float_21_to_32(w);
}

/******************************************************************************
** Function:	get alarm profiles and envelope thresholds for the next timeslot from files
**
** Notes:		if no file(s) exist(s) next level remains at default NO VALUE level FLT_MAX
*/
void alm_get_next_levels(uint8 channel)
{
	uint8   index, quarter;

	// if channel alarm is enabled and type is not fixed thresholds
	if (!ALM_config[channel].enabled || (ALM_config[channel].type == ALM_FIXED_ALARM))
		return;
	// else:

	// calculate quarter and index into file for required timeslot
	quarter = (alm_next_timeslot / 24) + 1;			// quarter range is 1 to 4
	index = alm_next_timeslot % 24;					// index of 15-minute timeslot in file

	if (ALM_config[channel].type == ALM_PROFILE_ALARM)
	{
		sprintf(alm_filename_str, "P%u%s.TXT", quarter, LOG_channel_id[channel + 1]);
		ALM_profile_levels[channel] = alm_extract_threshold(index);
	}
	else	// ALM_config[channel].type == ALM_ENVELOPE_ALARM
	{
		sprintf(alm_filename_str, "EH%u%s.TXT", quarter, LOG_channel_id[channel + 1]);
		ALM_envelope_high[channel] = alm_extract_threshold(index);
	
		sprintf(alm_filename_str, "EL%u%s.TXT", quarter, LOG_channel_id[channel + 1]);
		ALM_envelope_low[channel] = alm_extract_threshold(index);
	}
}

/******************************************************************************
** Function:	Parse next string from alarm config file at alm_char_ptr
**
** Notes:		Strings go in order description, high, low.
**				Returns ptr to start of string & sets alm_char_ptr to start of next
**				Terminates the string it returns. On subsequent passes, string may
**				already be terminated, so don't rely on \r\n at the end
**				If empty string at alm_char_ptr returns immediately
*/
char * alm_get_string(void)
{
	char * p;

	p = alm_char_ptr;
	if (*p == '\0')
		return p;
	// else
	while (alm_char_ptr < &STR_buffer[sizeof(STR_buffer)])	// don't run off end of STR_buffer
	{
		if ((*alm_char_ptr == '\r') || (*alm_char_ptr == '\n') || (*alm_char_ptr == '\0'))
		{
			*alm_char_ptr++ = '\0';							// add string termination (if necessary)
			if (*alm_char_ptr == '\n')
				alm_char_ptr++;								// point to next string

			return p;
		}

		alm_char_ptr++;
	}

	alm_char_ptr = p;
	*alm_char_ptr = '\0';						// off end of buffer - return empty string
	return p;
}

#ifndef HDW_RS485
/******************************************************************************
** Function:	Get units index for digital channel or sub-channel
**
** Notes:		Channel 0 = D1A, 1 = D1B, 2 = D2A, 3 = D2B
*/
int alm_get_digital_units_index(uint8 channel_index)
{
	uint8 dci = channel_index >> 1;		// 0 or 1 for D1 or D2
	uint8 eci = channel_index & 1;		// 0 or 1 for DxA or DxB

	uint8 event_channel = DIG_config[dci].sensor_type & ((eci != 0) ? DIG_EVENT_B_MASK : DIG_EVENT_A_MASK);

	return ((event_channel != 0) && ((DIG_config[dci].ec[eci].flags & DIG_MASK_CHANNEL_ENABLED) != 0)) ?
		DIG_config[dci].ec[eci].output_units_index : DIG_config[dci].units_index;
}
#endif

/******************************************************************************
** Function:	Get units string from channel configuration given index
**
** Notes:		Returns ptr to start of string
*/
char * alm_get_units_string(uint8 channel_index)
{
#ifndef HDW_RS485
	if (channel_index < ALM_ANA_ALARM_CHANNEL0)
		CFS_read_line("\\Config", "Units.txt", alm_get_digital_units_index(channel_index) + 1, alm_filename_str, 8);
  #ifndef HDW_GPS
	else
  #endif
#endif
#ifndef HDW_GPS
		CFS_read_line("\\Config", "Units.txt", ANA_config[channel_index - ALM_ANA_ALARM_CHANNEL0].units_index + 1, alm_filename_str, 8);
#endif
	return alm_filename_str;
}

/******************************************************************************
** Function:	Log alarm string for a channel
**
** Notes:		
*/
void alm_log_alarm(int index, char * log_string, int length)
{
	// All writes to SD disabled if battery flat
	if (LOG_state == LOG_BATT_DEAD)
		return;

	sprintf(alm_path_str, "\\%s\\%s\\20%02X\\%02X",
		"ALMDATA", LOG_channel_id[index + 1], RTC_now.yr_bcd, RTC_now.mth_bcd);

	// Generate logged alarm data filename, e.g. D1A-2705.TXT:
	sprintf(alm_filename_str, "%s-%02X%02X.TXT", LOG_channel_id[index + 1], RTC_now.day_bcd, RTC_now.mth_bcd);

	// write string to file
	CFS_write_file(alm_path_str, alm_filename_str, "a", log_string, length);
}

/******************************************************************************
** Function:	Act on new alarm according to mask
**
** Notes:		Set alm_p_config & alm_p_channel, & read config file
**				before calling
*/
void alm_enter_alarm(int index, bool high, uint8 action_mask)
{
	int i;
	float threshold;

	i = sprintf(STR_buffer, "dALARM=%02X%02X%02X,%02X:%02X:%02X,%s,%s,%s,",
		RTC_now.day_bcd, RTC_now.mth_bcd, RTC_now.yr_bcd,
		RTC_now.hr_bcd, RTC_now.min_bcd, RTC_now.sec_bcd,
		COM_sitename, LOG_channel_id[index + 1], alm_get_string());
	if (!high)
	{
		alm_get_string();									// skip to low string
		threshold = alm_p_channel->low_threshold;
	}
	else
		threshold = alm_p_channel->high_threshold;
	i += sprintf(&STR_buffer[i], "%s,", alm_get_string());
	i += STR_print_float(&STR_buffer[i], alm_p_channel->value);
	STR_buffer[i++] = ',';
	//if (threshold != FLT_MAX)								// threshold no longer reported
	//	i += STR_print_float(&STR_buffer[i], threshold);
	//STR_buffer[i++] = ',';
	i += sprintf(&STR_buffer[i], "%s", alm_get_units_string(index));

	if (action_mask & ALM_ACTION_SMS)
	{
		ALM_send_to_all_sms_numbers();
	}
	if (action_mask & ALM_ACTION_FTP)
	{
		ALM_send_ftp_alarm(ALM_FTP_ENTER);
	}
	if (action_mask & ALM_ACTION_LOG)
	{
		// create the alarm log string
		i = sprintf(STR_buffer, "%02X:%02X:%02X,",
			RTC_now.hr_bcd, RTC_now.min_bcd, RTC_now.sec_bcd);
		if (high)
		{
			strcat(STR_buffer, "EH,");
			threshold = alm_p_channel->high_threshold;
		}
		else
		{
			strcat(STR_buffer, "EL,");
			threshold = alm_p_channel->low_threshold;
		}
		i += 3;
		i += STR_print_float(&STR_buffer[i], alm_p_channel->value);
		STR_buffer[i++] = ',';
		if (threshold != FLT_MAX)
			i += STR_print_float(&STR_buffer[i], threshold);
#ifndef HDW_RS485
		if (index < ALM_ANA_ALARM_CHANNEL0)
			i += sprintf(&STR_buffer[i], ",%u\r\n", alm_get_digital_units_index(index));
  #ifndef HDW_GPS
		else
  #endif
#endif
#ifndef HDW_GPS
			i += sprintf(&STR_buffer[i], ",%u\r\n", ANA_config[index - ALM_ANA_ALARM_CHANNEL0].units_index);
#endif
		// log it
		alm_log_alarm(index, STR_buffer, i);
	}
																				// set the correct flag to run the script file
	if (high)
		alm_script_flags[0] |= CMD_word_mask[index];
	else
		alm_script_flags[1] |= CMD_word_mask[index];
}

/******************************************************************************
** Function:	Come out of alarm with actions according to mask
**
** Notes:		setb alm_p_config & alm_p_channel before calling
*/
void alm_exit_alarm(int index, bool high, uint8 action_mask)
{
	int i;
	float threshold;

	i = sprintf(STR_buffer, "dALMCLR=%02X%02X%02X,%02X:%02X:%02X,%s,%s,%s,",
		RTC_now.day_bcd, RTC_now.mth_bcd, RTC_now.yr_bcd,
		RTC_now.hr_bcd, RTC_now.min_bcd, RTC_now.sec_bcd,
		COM_sitename, LOG_channel_id[index + 1], alm_get_string());
	if (!high)
	{
		alm_get_string();									// skip to low string
		threshold = alm_p_channel->low_threshold;
	}
	else
		threshold = alm_p_channel->high_threshold;
	i += sprintf(&STR_buffer[i], "%s,", alm_get_string());
	i += STR_print_float(&STR_buffer[i], alm_p_channel->value);
	STR_buffer[i++] = ',';
	//if (threshold != FLT_MAX)								// threshold no longer reported
	//	i += STR_print_float(&STR_buffer[i], threshold);
	//STR_buffer[i++] = ',';
	i += sprintf(&STR_buffer[i], "%s", alm_get_units_string(index));

	if (action_mask & ALM_ACTION_SMS)
	{
		STR_buffer[160] = '\0';								// truncate SMS length to 160 bytes
		ALM_send_to_all_sms_numbers();
	}
	if (action_mask & ALM_ACTION_FTP)
	{
		ALM_send_ftp_alarm(ALM_FTP_CLEAR);
	}
	if (action_mask & ALM_ACTION_LOG)
	{
		// create the alarm log string
		i = sprintf(STR_buffer, "%02X:%02X:%02X,",
			RTC_now.hr_bcd, RTC_now.min_bcd, RTC_now.sec_bcd);
		if (high)
		{
			strcat(STR_buffer, "CH,");
			threshold = alm_p_channel->high_threshold;
		}
		else
		{
			strcat(STR_buffer, "CL,");
			threshold = alm_p_channel->low_threshold;
		}
		i += 3;
		i += STR_print_float(&STR_buffer[i], alm_p_channel->value);
		STR_buffer[i++] = ',';
		if (threshold != FLT_MAX)
			i += STR_print_float(&STR_buffer[i], threshold);
#ifndef HDW_RS485
		if (index < ALM_ANA_ALARM_CHANNEL0)
			i += sprintf(&STR_buffer[i], ",%u\r\n", alm_get_digital_units_index(index));
  #ifndef HDW_GPS
		else
  #endif
#endif
#ifndef HDW_GPS
			i += sprintf(&STR_buffer[i], ",%u\r\n", ANA_config[index - ALM_ANA_ALARM_CHANNEL0].units_index);
#endif
		// log it
		alm_log_alarm(index, STR_buffer, i);
	}
																				// set the correct flag to run the script file
	if (high)
		alm_script_flags[2] |= CMD_word_mask[index];
	else
		alm_script_flags[3] |= CMD_word_mask[index];
}

/******************************************************************************
** Function:	Handle pending alarm message
**
** Notes:	
*/
void alm_handle_alarm_message(int index)
{
	alm_p_config = &ALM_config[index];
	alm_p_channel = &ALM_channel[index];

	// Use STR_buffer for filename, read contents into 2nd half of buffer
	sprintf(STR_buffer, "ALM%s.TXT", LOG_channel_id[index + 1]);
	alm_char_ptr = &STR_buffer[256];
	if (!CFS_read_file((char *)CFS_config_path, STR_buffer, alm_char_ptr, 256))
		*alm_char_ptr = '\0';

	// Do alarm clear messages first, so excursions can be initialised when we enter alarms
	// (not so important now, as excursions have been removed).
	// Reset alm_char_ptr after each one so exit low & into high or vice-versa won't
	// cause reporting strings to be omitted.
	if (alm_p_channel->mask & ALM_MASK_EXIT_HIGH_ALARM)
	{
		alm_exit_alarm(index, true, alm_p_config->high_clear_mask);
		alm_p_channel->mask &= ~ALM_MASK_EXIT_HIGH_ALARM;
		alm_char_ptr = &STR_buffer[256];
	}

	if (alm_p_channel->mask & ALM_MASK_EXIT_LOW_ALARM)
	{
		alm_exit_alarm(index, false, alm_p_config->low_clear_mask);
		alm_p_channel->mask &= ~ALM_MASK_EXIT_LOW_ALARM;
		alm_char_ptr = &STR_buffer[256];
	}

	if (alm_p_channel->mask & ALM_MASK_ENTER_HIGH_ALARM)
	{
		alm_enter_alarm(index, true, alm_p_config->high_enable_mask);
		alm_p_channel->mask &= ~ALM_MASK_ENTER_HIGH_ALARM;
		//alm_p_channel->excursion = alm_p_channel->value;	// initialise excursion
		alm_char_ptr = &STR_buffer[256];
	}

	if (alm_p_channel->mask & ALM_MASK_ENTER_LOW_ALARM)
	{
		alm_enter_alarm(index, false, alm_p_config->low_enable_mask);
		alm_p_channel->mask &= ~ALM_MASK_ENTER_LOW_ALARM;
		//alm_p_channel->excursion = alm_p_channel->value;	// initialise excursion
		alm_char_ptr = &STR_buffer[256];
	}
}

/******************************************************************************
** Function:	run first script file found
**
** Notes:		creates script file name from flag indices
**				clears its flag
*/
void alm_run_script(void)
{
	int   i;
	uint8 register_index, flag_index;
	bool  found = false;

	register_index = 0;															// find first flag set
	do
	{
		flag_index = 0;
		do
		{
			found = ((alm_script_flags[register_index] & CMD_word_mask[flag_index]) != 0x0000);
			if (!found)
				flag_index++;
		}
		while ((flag_index < 12) && !found);
		if (!found)
			register_index++;
	} 
	while ((register_index < 5) && !found);

	if (found)
		alm_script_flags[register_index] &= ~CMD_word_mask[flag_index];				// clear flag
	else
		return;

	if (register_index < 4)
	{
		i = sprintf(STR_buffer, "AM%s", LOG_channel_id[flag_index + 1]);
		if (register_index == 0)
			strcat(STR_buffer, "EH");
		else if (register_index == 1)
			strcat(STR_buffer, "EL");
		else if (register_index == 2)
			strcat(STR_buffer, "CH");
		else if (register_index == 3)
			strcat(STR_buffer, "CL");
	}
	else
	{
		i = sprintf(STR_buffer, "ATOD%d", flag_index + 1);
	}
	strcat(STR_buffer, ".HCS");

	SCF_execute((char *)CFS_scripts_path, STR_buffer, false);
}

/******************************************************************************
** Function:	Set current alarm thresholds according to alarm type
**
** Notes:		
*/
void alm_set_current_thresholds(int index)
{
	float f1, f2;

	alm_p_config = &ALM_config[index];
	if (!alm_p_config->enabled || (LOG_state != LOG_LOGGING))
		return;
	// else:

	alm_p_channel = &ALM_channel[index];
	switch (alm_p_config->type)
	{
	case ALM_PROFILE_ALARM:
		// calculate high and low thresholds from profile level
		f1 = ALM_profile_levels[index];					// new profile value for this 15 minute period
		if (f1 == FLT_MAX)								// no threshold
		{
			alm_p_channel->high_threshold = FLT_MAX;	// no threshold
			alm_p_channel->low_threshold = FLT_MAX;
			break;										// leave thresholds at FLT_MAX
		}
		// else:

		f2 = alm_p_config->profile_width;				// delta, or fraction of profile value
		if (alm_p_config->width_is_multiplier)			// fraction of profile level 
			f2 *= f1;									// convert to delta
		alm_p_channel->high_threshold = f1 + f2;
		alm_p_channel->low_threshold = f1 - f2;
		break;

	case ALM_ENVELOPE_ALARM:
		alm_p_channel->high_threshold = ALM_envelope_high[index];
		alm_p_channel->low_threshold = ALM_envelope_low[index];
		break;

	default:	// fixed thresholds
		alm_p_channel->high_threshold = alm_p_config->default_high_threshold;
		alm_p_channel->low_threshold = alm_p_config->default_low_threshold;
		break;
	}
}

/******************************************************************************
** Function:	set ALM_wakeup_time to lowest of all alm times
**
** Notes:		remembers profile fetch time if enabled
*/
void alm_set_wakeup_time(void)
{
	int    index;
	uint32 time_sec;
	ALM_tod_config_type * p;

	ALM_wakeup_time = SLP_NO_WAKEUP;

	for (index = 0; index < ALM_NUM_ALARM_CHANNELS; index++)					// if any profiles or envelopes enabled, repeat in 15 mins
	{
																				// if channel alarm is enabled and type is not fixed thresholds
		if (ALM_config[index].enabled && (ALM_config[index].type != ALM_FIXED_ALARM))
		{
			// set wakeup time to the end of the present 15 minute period
			ALM_wakeup_time = RTC_time_sec;
			LOG_set_next_time(&ALM_wakeup_time, 14, false);
			break;
		}
	}
	alm_profile_time = ALM_wakeup_time;											// remember profile time if any, or set to no wake up if none

	for (index = 0; index < ALM_TOD_NUMBER; index++)
	{
		p = &ALM_tod_config[index];
		if (p->enabled)															// if time of day trigger required
		{
			time_sec = RTC_bcd_time_to_sec(p->trigger_time.hr_bcd, p->trigger_time.min_bcd, p->trigger_time.sec_bcd);
			if ((time_sec > RTC_time_sec) && (time_sec < ALM_wakeup_time))		// don't schedule a wakeup if it's in the past
				ALM_wakeup_time = time_sec;										// set wakeup time to lowest
		}
	}
}

//*******************************************************************
// public functions
//*******************************************************************

/******************************************************************************
** Function:	Log voltage source alarm string
**
** Notes:		
*/
void ALM_log_pwr_alarm(void)
{
	sprintf(alm_path_str, "\\%s\\%s\\20%02X\\%02X",
		"ALMDATA", "PWR", RTC_now.yr_bcd, RTC_now.mth_bcd);

	// Generate logged alarm data filename, e.g. ECO-2705.TXT:
	sprintf(alm_filename_str, "PWR-%02X%02X.TXT", RTC_now.day_bcd, RTC_now.mth_bcd);

	// write STR_buffer to file
	CFS_write_file(alm_path_str, alm_filename_str, "a", STR_buffer, strlen(STR_buffer));
}

/********************************************************************
 * Function:        void ALM_configure_channel(uint8 channel_index)
 *
 * PreCondition:    None
 *
 * Input:           channel index to identify channel to configure
 *
 * Output:          None
 *
 * Side Effects:    None
 *
 * Overview:        configures a channel after receiving new configuration
 *
 * Note:            None
 *******************************************************************/
void ALM_configure_channel(uint8 channel_index)
{
	ALM_wakeup_time = 0;															// force immediate setup of wakeup time in ALM_task if required
	memset(&ALM_channel[channel_index], 0, sizeof(ALM_channel[channel_index]));		// Clear working registers:
	if (ALM_config[channel_index].type != ALM_FIXED_ALARM)							// Initialise current alarm thresholds:
	{
		ALM_channel[channel_index].high_threshold = FLT_MAX;						// no threshold until next 15-min boundary
		ALM_channel[channel_index].low_threshold = FLT_MAX;
	}
	else																			// fixed threshold alarms
	{
		ALM_channel[channel_index].high_threshold = ALM_config[channel_index].default_high_threshold;
		ALM_channel[channel_index].low_threshold = ALM_config[channel_index].default_low_threshold;
	}

																					// force alarm samples to re-synchronise
#ifndef HDW_RS485																	// digital and derived digital channels
	if (channel_index < ALM_ANA_ALARM_CHANNEL0)
		DIG_channel[channel_index / 2].sub[channel_index % 2].normal_alarm_time = SLP_NO_WAKEUP;

	else if ((channel_index >= ALM_ALARM_DERIVED_CHANNEL0) && (channel_index < ALM_ANA_ALARM_DERIVED_CH0))
		DIG_channel[(channel_index - ALM_ALARM_DERIVED_CHANNEL0) / 2].sub[(channel_index - ALM_ALARM_DERIVED_CHANNEL0) % 2].derived_alarm_time = SLP_NO_WAKEUP;
#else																				// serial doppler sensor channels
	if (channel_index == ALM_ALARM_CHANNEL0)
		DOP_channel.velocity_alarm.time = SLP_NO_WAKEUP;
	else if (channel_index == ALM_ALARM_CHANNEL1)
		DOP_channel.temperature_alarm.time = SLP_NO_WAKEUP;
	else if (channel_index == ALM_ALARM_CHANNEL2)
		DOP_channel.depth_alarm.time = SLP_NO_WAKEUP;
	else if (channel_index == ALM_ALARM_DERIVED_CHANNEL0)
		DOP_channel.derived_flow_alarm.time = SLP_NO_WAKEUP;
#endif
#ifndef HDW_GPS
																					// analogue channels
	else if ((channel_index >= ALM_ANA_ALARM_CHANNEL0) && (channel_index < ALM_ALARM_DERIVED_CHANNEL0))
		ANA_channel[channel_index - ALM_ANA_ALARM_CHANNEL0].normal_alarm.time = SLP_NO_WAKEUP;
	else if ((channel_index >= ALM_ANA_ALARM_DERIVED_CH0) && (channel_index < ALM_NUM_ALARM_CHANNELS))
		ANA_channel[channel_index - ALM_ANA_ALARM_DERIVED_CH0].derived_alarm.time = SLP_NO_WAKEUP;
#endif
}

/******************************************************************************
** Function:	Send an alarm message to ftp 
**
** Notes:		alarm text in STR_buffer
*/
void ALM_send_ftp_alarm(uint8 type)
{
	if (type == ALM_FTP_CLEAR)
		sprintf(alm_filename_str, "ALMCLR_%05d.TXT", alm_ftp_msg_index);
	else if (type == ALM_FTP_ENTER)
		sprintf(alm_filename_str, "ALARM_%05d.TXT", alm_ftp_msg_index);
	else if (type == ALM_FTP_BATTERY)
		sprintf(alm_filename_str, "ALMBAT_%05d.TXT", alm_ftp_msg_index);
	else if (type == ALM_FTP_ECO)
		sprintf(alm_filename_str, "ALMECO_%05d.TXT", alm_ftp_msg_index);
	else
		return;
	MSG_send(MSG_TYPE_FTP_MSG, STR_buffer,  alm_filename_str);
	MSG_flush_outbox_buffer(true);												// immediate tx
	FTP_schedule();																// generate up to date outgoing FTP reports
	alm_ftp_msg_index++;
}

/******************************************************************************
** Function:	Send an alarm message to all configured sms telephone numbers 
**
** Notes:		
*/
void ALM_send_to_all_sms_numbers(void)
{
	// send alarm message to ALL configured numbers
	MSG_send(MSG_TYPE_SMS_TEXT, STR_buffer, COM_host1);
	MSG_flush_outbox_buffer(true);							// immediate tx
	MSG_send(MSG_TYPE_SMS_TEXT, STR_buffer, COM_host2);
	MSG_flush_outbox_buffer(true);							// immediate tx
	MSG_send(MSG_TYPE_SMS_TEXT, STR_buffer, COM_host3);
	MSG_flush_outbox_buffer(true);							// immediate tx
	MSG_send(MSG_TYPE_SMS_TEXT, STR_buffer, COM_alarm1);
	MSG_flush_outbox_buffer(true);							// immediate tx
	MSG_send(MSG_TYPE_SMS_TEXT, STR_buffer, COM_alarm2);
	MSG_flush_outbox_buffer(true);							// immediate tx
}

/******************************************************************************
** Function:	Process value for alarm
**
** Notes:		Called by analogue or digital channel, at the configured alarm
**				sample rate.
*/
void ALM_process_value(int index, float value)
{
	alm_p_config = &ALM_config[index];
	if (!alm_p_config->enabled || (LOG_state != LOG_LOGGING))
		return;

	alm_p_channel = &ALM_channel[index];
	alm_p_channel->value = value;

	// First look for alarm clear events, so excursion can be used before it is
	// re-initialised on going into alarm.
	if ((alm_p_channel->mask & ALM_MASK_IN_HIGH_ALARM) != 0)	// in high alarm
	{
		if (alm_p_channel->high_threshold == FLT_MAX)			// no threshold - exit alarm, but no message
			alm_p_channel->mask &= ~ALM_MASK_IN_HIGH_ALARM;		// re-arm alarm for when we have a threshold
		else if (value < alm_p_channel->high_threshold - alm_p_config->deadband)	// exit and report ALMCLR
		{
			alm_p_channel->mask &= ~ALM_MASK_IN_HIGH_ALARM;							// re-arm alarm
			if ((alm_p_config->high_clear_mask & (ALM_ACTION_SMS | ALM_ACTION_FTP | ALM_ACTION_LOG)) != 0)
				alm_p_channel->mask |= ALM_MASK_EXIT_HIGH_ALARM;	// send message when file system available
		}
	}

	if ((alm_p_channel->mask & ALM_MASK_IN_LOW_ALARM) != 0)		// in low alarm
	{
		if (alm_p_channel->low_threshold == FLT_MAX)			// no threshold - exit alarm but no message
			alm_p_channel->mask &= ~ALM_MASK_IN_LOW_ALARM;		// re-arm alarm for when we have a threshold
		else if (value > alm_p_channel->low_threshold + alm_p_config->deadband)		// exit and report ALMCLR
		{
			alm_p_channel->mask &= ~ALM_MASK_IN_LOW_ALARM;							// re-arm alarm
			if ((alm_p_config->low_clear_mask & (ALM_ACTION_SMS | ALM_ACTION_FTP | ALM_ACTION_LOG)) != 0)
				alm_p_channel->mask |= ALM_MASK_EXIT_LOW_ALARM;		// send message when file system available
		}
	}

	// Check for going into high alarm
	if ((alm_p_channel->high_threshold != FLT_MAX) &&							// high limit valid
		(value >= (alm_p_channel->high_threshold + alm_p_config->deadband)))	// high limit exceeded
	{
		if ((alm_p_channel->mask & ALM_MASK_IN_HIGH_ALARM) == 0)				// not yet in alarm
		{
			if (alm_p_channel->high_debounce_count == 0)						// load up debounce timer
				alm_p_channel->high_debounce_count = (uint8)
					(LOG_interval_sec[alm_p_config->debounce_delay] / LOG_interval_sec[alm_p_config->sample_interval] + 1);
			
			if (alm_p_channel->high_debounce_count > 1)							// decrement debounce timer
				alm_p_channel->high_debounce_count--;
			else																// debounce timer expired (1 or 0)
			{
				alm_p_channel->high_debounce_count = 0;
				alm_p_channel->mask |= ALM_MASK_IN_HIGH_ALARM;
				if ((alm_p_config->high_enable_mask & (ALM_ACTION_SMS | ALM_ACTION_FTP | ALM_ACTION_LOG)) != 0)
					alm_p_channel->mask |= ALM_MASK_ENTER_HIGH_ALARM;		// send message when file system available
			}
		}
		//else if (value > alm_p_channel->excursion)	// excursions removed for V2.46
		//	alm_p_channel->excursion = value;
	}
	else											// below high limit plus deadband
		alm_p_channel->high_debounce_count = 0;		// restart debounce timer when limit exceeded

	// Check for low alarms
	if ((alm_p_channel->low_threshold != FLT_MAX) &&					// low limit valid
		(value <= (alm_p_channel->low_threshold - alm_p_config->deadband)))	// low limit exceeded
	{
		if ((alm_p_channel->mask & ALM_MASK_IN_LOW_ALARM) == 0)				// not yet in alarm
		{
			if (alm_p_channel->low_debounce_count == 0)						// load up debounce timer
				alm_p_channel->low_debounce_count = (uint8)
					(LOG_interval_sec[alm_p_config->debounce_delay] / LOG_interval_sec[alm_p_config->sample_interval] + 1);

			if (alm_p_channel->low_debounce_count > 1)						// decrement debounce timer
				alm_p_channel->low_debounce_count--;
			else															// debounce timer expired
			{
				alm_p_channel->low_debounce_count = 0;
				alm_p_channel->mask |= ALM_MASK_IN_LOW_ALARM;
				if ((alm_p_config->low_enable_mask & (ALM_ACTION_SMS | ALM_ACTION_FTP | ALM_ACTION_LOG)) != 0)
					alm_p_channel->mask |= ALM_MASK_ENTER_LOW_ALARM;		// send message when file system available
			}
		}
		//else if (value < alm_p_channel->excursion)	// excurions removed for V2.46
		//	alm_p_channel->excursion = value;
	}
	else											// above low limit minus deadband
		alm_p_channel->low_debounce_count = 0;		// restart debounce timer when limit exceeded
}

/******************************************************************************
** Function:	Process value for alarm from MODBUS
**
** Notes:		
*/
void ALM_modbus_process_value(int index, float value, float high_th, float low_th, bool high_alarm, bool low_alarm)
{
	alm_p_config = &ALM_config[index];
	if (!alm_p_config->enabled || (LOG_state != LOG_LOGGING))
		return;

	alm_p_channel = &ALM_channel[index];
	alm_p_channel->value = value;
	alm_p_channel->high_threshold = high_th;
	alm_p_channel->low_threshold = low_th;

	// First look for alarm clear events, so excursion can be used before it is
	// re-initialised on going into alarm.
	if ((alm_p_channel->mask & ALM_MASK_IN_HIGH_ALARM) != 0)	// in high alarm
	{
		if (!high_alarm)
		{
			// exit and report ALMCLR
			alm_p_channel->mask &= ~ALM_MASK_IN_HIGH_ALARM;							// re-arm alarm
			if ((alm_p_config->high_clear_mask & (ALM_ACTION_SMS | ALM_ACTION_FTP | ALM_ACTION_LOG)) != 0)
				alm_p_channel->mask |= ALM_MASK_EXIT_HIGH_ALARM;	// send message when file system available
		}
	}

	if ((alm_p_channel->mask & ALM_MASK_IN_LOW_ALARM) != 0)		// in low alarm
	{
		if (!low_alarm)
		{
			// exit and report ALMCLR
			alm_p_channel->mask &= ~ALM_MASK_IN_LOW_ALARM;							// re-arm alarm
			if ((alm_p_config->low_clear_mask & (ALM_ACTION_SMS | ALM_ACTION_FTP | ALM_ACTION_LOG)) != 0)
				alm_p_channel->mask |= ALM_MASK_EXIT_LOW_ALARM;		// send message when file system available
		}
	}

	// Check for going into high alarm
	if (high_alarm)
	{
		if ((alm_p_channel->mask & ALM_MASK_IN_HIGH_ALARM) == 0)				// not yet in alarm
		{
			alm_p_channel->mask |= ALM_MASK_IN_HIGH_ALARM;
			if ((alm_p_config->high_enable_mask & (ALM_ACTION_SMS | ALM_ACTION_FTP | ALM_ACTION_LOG)) != 0)
				alm_p_channel->mask |= ALM_MASK_ENTER_HIGH_ALARM;		// send message when file system available
		}
	}

	// Check for low alarms
	if (low_alarm)
	{
		if ((alm_p_channel->mask & ALM_MASK_IN_LOW_ALARM) == 0)				// not yet in alarm
		{
			alm_p_channel->mask |= ALM_MASK_IN_LOW_ALARM;
			if ((alm_p_config->low_enable_mask & (ALM_ACTION_SMS | ALM_ACTION_FTP | ALM_ACTION_LOG)) != 0)
				alm_p_channel->mask |= ALM_MASK_ENTER_LOW_ALARM;		// send message when file system available
		}
	}
}

/******************************************************************************
** Function:	process an event alarm
**
** Notes:
*/
void ALM_process_event(int index, bool value_is_high)
{
	alm_p_config = &ALM_config[index];
	if (!alm_p_config->enabled)
		return;

	if (LOG_state != LOG_LOGGING)
		return;

	alm_p_channel = &ALM_channel[index];										// set up alarm channel for alarm task to deal with
	alm_p_channel->high_threshold = FLT_MAX;									// set unused thresholds
	alm_p_channel->low_threshold = FLT_MAX;
	if (value_is_high)
	{
		alm_p_channel->value = 1;
		alm_p_channel->mask |= ALM_MASK_ENTER_HIGH_ALARM;						// enable flags are checked in enter alarm code
	}
	else
	{
		alm_p_channel->value = 0;
		alm_p_channel->mask |= ALM_MASK_ENTER_LOW_ALARM;						// enable flags are checked in enter alarm code
	}
}

/******************************************************************************
** Function:	trigger profile and envelope update
**
** Notes:
*/
void ALM_update_profile(void)
{
	alm_read_next_thresholds = true;											// force immediate fetch of next profile and envelope values
	alm_set_wakeup_time();														// recalc next wakeup time for profile AND scripts
}

/******************************************************************************
** Function:	can alarm task sleep?
**
** Notes:
*/
bool ALM_can_sleep(void)
{
	return (!alm_read_next_thresholds && (alm_state == ALM_IDLE));
}

/******************************************************************************
** Function:	ALM task
**
** Notes:
*/
void ALM_task(void)
{
	uint8  index;
	uint32 time_sec;
	ALM_tod_config_type * p;

	if (alm_day_bcd != RTC_now.day_bcd)											// check for midnight
	{
		ALM_wakeup_time = 0;													// force recalc of wakeup times
		alm_day_bcd = RTC_now.day_bcd;
	}

	if (ALM_wakeup_time <= RTC_time_sec)										// If wakeup time has passed, ensure it is updated.
	{
		if (alm_profile_time <= RTC_time_sec)									// if profile time passed or exact 
			alm_read_next_thresholds = true;									// force fetch of next profile and envelope values

		for (index = 0; index < ALM_TOD_NUMBER; index++)						// for each time of day trigger time
		{
			p = &ALM_tod_config[index];
			if (p->enabled)														// if time of day trigger required
			{
				time_sec = RTC_bcd_time_to_sec(p->trigger_time.hr_bcd, p->trigger_time.min_bcd, p->trigger_time.sec_bcd);
				if (time_sec == RTC_time_sec)									// if exact time to do it
					alm_script_flags[4] |= CMD_word_mask[index];				// set script flag
			}
		}
		alm_set_wakeup_time();													// recalculate alarm wake up time
	}

	switch (alm_state)
	{
	case ALM_IDLE:
		// find the first alarm pending message
		for (index = 0; index < ALM_NUM_ALARM_CHANNELS; index++)
		{
			// if alarm enabled
			if (ALM_config[index].enabled)
			{
				// if a message pending flag set for indexed channel
				alm_p_channel = &ALM_channel[index];
				if ((alm_p_channel->mask & ALM_MASK_PENDING_MESSAGE) != 0)	// pending actions
				{
					alm_state = ALM_MESSAGE_PENDING;
					alm_index = index;						// remember channel
					return;
				}
			}
		}
		// Reaches this point if no pending messages

		// check if current alarm thresholds can to be updated
		if (alm_next_timeslot == (uint8)(RTC_time_sec / 900))
		{
			// set profile and envelope levels 'this' to 'next' for all channels
			for (index = 0; index < ALM_NUM_ALARM_CHANNELS; index++)
				alm_set_current_thresholds(index);

			// only try to read from file if at least 1 profile or envelope enabled
			if (ALM_wakeup_time != SLP_NO_WAKEUP)
				alm_read_next_thresholds = true;
		}

		// check if next thresholds need to be read from file
		if (alm_read_next_thresholds)
		{
			alm_read_next_thresholds = false;
			alm_info_channel = 0;
			alm_next_timeslot = (uint8)(RTC_time_sec / 900) + 1;	// next 15-minute timeslot (1..96)
			if (alm_next_timeslot > 95)
				alm_next_timeslot = 0;								// 0..95

			alm_state = ALM_GET_NEXT_LEVELS;
		}

		// check script pending flags
		for (index = 0; index < 5; index++)
		{
			if (alm_script_flags[index] != 0)
				alm_state = ALM_SCRIPT_PENDING;
		}		
		break;

	case ALM_MESSAGE_PENDING:
		if (!LOG_busy() && !PDU_busy() && CFS_open())
		{
			alm_handle_alarm_message(alm_index);			
			alm_state = ALM_IDLE;
		}
		break;

	case ALM_GET_NEXT_LEVELS:
		if (!LOG_busy() && !PDU_busy() && CFS_open())
		{
			// for each channel
			if (alm_info_channel < ALM_NUM_ALARM_CHANNELS)
				alm_get_next_levels(alm_info_channel++);
			else
			{
				alm_state = ALM_IDLE;
				alm_info_channel = 0;
			}
		}
		break;

	case ALM_SCRIPT_PENDING:
		if (!LOG_busy() && !PDU_busy() && CFS_open())
		{
			if (SCF_progress() == 100)							// if no script running already
				alm_run_script();								// run first script file found and clear its flag			
			alm_state = ALM_IDLE;								// return to idle and recheck script files
		}
		break;

	default:
		alm_state = ALM_IDLE;
		break;
	}
}

/******************************************************************************
** Function:	ALM initialisation
**
** Notes:
*/
void ALM_init(void)
{
	uint8 i;

	ALM_wakeup_time = 0;				// ensures threshold update from file after reset
	ALM_com_mode_alarm_enable = true;	// default = commission mode alarm is enabled
	alm_ftp_msg_index = 0;
	for (i = 0; i < ALM_NUM_ALARM_CHANNELS; i++)
	{
		ALM_config[i].enabled = false;
		ALM_config[i].type = 0;

		ALM_profile_levels[i] = FLT_MAX;
		ALM_envelope_high[i] = FLT_MAX;
		ALM_envelope_low[i] = FLT_MAX;
	}
	alm_day_bcd = RTC_now.day_bcd;
}

