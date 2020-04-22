/******************************************************************************
** File:	Pwr.c
**
** Notes:	Power management
**
** Changes
**
** 180112 V2.99 PB  add PWR_eco_init() for ECO defaults
**
** 220212 V2.99 PB  edition 3rd - call fn ALM_log_eco_alarm() to log eco power changeover alarms
**
** 190612 V3.01 PB  add debug LED flag to PWR_measurement_flags	and functions to enable, control and read it
**
** 091012 V3.17 PB  Bring up to date with Xilog+ V3.06 - 
**					report removal of external power if any window interval < 15 minutes in activiy log
**					new fn PWR_have_external_power()
**					use of CFS_open() in PWR_task()
**
** V3.26 180613 PB  Remove CFS_FAILED_TO_OPEN state 
**
** 171013 V3.29 PB  bring up to date with Xilog+ V3.05
**					new flag for battery alarm sent
**					new variable and functions for battery failure management and transmission of alarm
**					add diode drop handling
**
** 011113 V3.30 PB  in PWR_tx_internal_batt_alarm() also clear subchannel message enable flags when battery low
**
** 111113 V3.31 PB  in PWR_tx_internal_batt_alarm() also clear doppler and derived analogue message enable flags when battery low 
**
**
** V4.00 220114 PB  if HDW_GPS disable all analogue calls and functions
**
** V4.04 010514 PB  hold TURN_AN_ON low if gps product
**
** V4.09 290514 PB  GPS version - set TURN_AN_ON = false after power measurements to ensure low current
**
** V4.11 260814 PB	line 234 - requires '~' to invert sum of flags for clearing
**
*/

#include "Custom.h"
#include "Compiler.h"
#include "HardwareProfile.h"
#include "MDD File System/FSIO.h"
#include "Tim.h"
#include "Str.h"
#include "Cfs.h"
#include "rtc.h"
#include "Log.h"
#include "Ana.h"
#include "Msg.h"
#include "Com.h"
#include "alm.h"
#include "Slp.h"
#include "Usb.h"
#include "Dig.h"
#include "Dop.h"

#define extern
#include "Pwr.h"
#undef extern

// States:
#define PWR_IDLE					0
#define PWR_PENDING					1
#define PWR_POWERING_1				2
#define PWR_POWERING_2				3
#define PWR_MEASUREMENT_COMPLETE	4

uint8 pwr_state;
int16 pwr_diode_volts_x100;			// diode drop, added to ADC value battery value

/******************************************************************************
** Function:	Read ADC channel
**
** Notes:		ADC already configured for clocked conversion trigger
*/
uint16 pwr_read_adc(int channel)
{
	AD1CHS0 = (uint16)channel;						// Select channel
	AD1CON1bits.SAMP = true;						// Start sample / convert sequence
	while (AD1CON1bits.SAMP || !AD1CON1bits.DONE);	// Wait for sample / conversion complete

	return ADC1BUF0;								// NB result not preserved when ADC turned off
}

/******************************************************************************
** Function:	check whether we have external power	
**
** Notes:		
*/
bool PWR_have_external_power(void)
{
	return (PWR_last_ext_supply > 0.2f);
}

/******************************************************************************
** Function:	
**
** Notes:		
*/
void PWR_enable_debug_led(bool enable)
{
	if (enable)
		PWR_measurement_flags |= PWR_MASK_DEBUG_LED_ON;
	else
		PWR_measurement_flags &= ~PWR_MASK_DEBUG_LED_ON;
}

/******************************************************************************
** Function:	
**
** Notes:		
*/
bool PWR_debug_led_enabled(void)
{
	return ((PWR_measurement_flags & PWR_MASK_DEBUG_LED_ON) == PWR_MASK_DEBUG_LED_ON);
}

/******************************************************************************
** Function:	
**
** Notes:		
*/
void PWR_drive_debug_led(bool led_on)
{
	if (!PWR_debug_led_enabled())
		return;

#ifdef HDW_1FM
	HDW_LED = led_on;
#else
	HDW_CONTROL1_ON = led_on;
#endif
}

/******************************************************************************
** Function:	Read batteries + external power
**
** Notes:		Approx 3us per ADC read. Analogue HW needs to have been on for
**				at least 10ms or so before this is called
*/
void PWR_read_values(void)
{
#ifdef HDW_PRIMELOG_PLUS
	int batt_counts;
	int vref_counts;
#endif

    PMD1bits.ADC1MD = 0;							// Enable clocks to ADC
    Nop();
    Nop();
	AD1CON3 = _16BIT(4, (4 - 1));					// Set ADC clock = (4 * Tcy) = 250ns, auto-sample time = 1us
	AD1CON2 = _16BIT(_B00000000, _B00000000);		// select Vr+/- = AVdd, AVss (on XiLog+ AVdd is well-defined 3.3V)
	AD1CON1 = _16BIT(_B10100000, _B11100000);		// Set auto convert & result alignment, & turn A/D on:

#ifdef HDW_PRIMELOG_PLUS
	batt_counts = pwr_read_adc(HDW_ADC_CHANNEL_INT_BAT);
	vref_counts = pwr_read_adc(HDW_ADC_CHANNEL_2V5_REF);
	batt_counts += pwr_read_adc(HDW_ADC_CHANNEL_INT_BAT);
	vref_counts += pwr_read_adc(HDW_ADC_CHANNEL_2V5_REF);
	if (vref_counts == 0)
		vref_counts = 1;
	
	// Vref * resistor ratio * count ratio:
	PWR_int_bat_volts = ((2.5f * 6.42f / 2.0f) * batt_counts) / (float)vref_counts;

	batt_counts = pwr_read_adc(HDW_ADC_CHANNEL_EXT_SUPPLY);
	vref_counts = pwr_read_adc(HDW_ADC_CHANNEL_2V5_REF);
	batt_counts += pwr_read_adc(HDW_ADC_CHANNEL_EXT_SUPPLY);
	vref_counts += pwr_read_adc(HDW_ADC_CHANNEL_2V5_REF);
	
	// Vref * resistor ratio * count ratio:
	PWR_ext_supply_volts = ((2.5f * 12.7f / 2.0f) * batt_counts) / (float)vref_counts;
#else
	PWR_int_bat_volts = (3.3f / 1023.0f) * (6.42f / 2.0f) * pwr_read_adc(HDW_ADC_CHANNEL_INT_BAT);
	PWR_ext_supply_volts = (3.3f / 1023.0f) * (12.7f / 2.0f) * pwr_read_adc(HDW_ADC_CHANNEL_EXT_SUPPLY);
#endif
	// Add diode drop to get more accurate internal battery voltage:
	PWR_int_bat_volts += 0.01f * (float)pwr_diode_volts_x100; 
	AD1CON1 = 0;									// turn ADC off
	
	// Essential workaround to ensure low sleep current:
    PMD1bits.ADC1MD = true;        					// Disable ADC clocks and reset all ADC registers
	HDW_BATTEST_INT_ON = false;						// Power down analogue HW as appropriate
	HDW_BATTEST_EXT_ON = false;
#ifdef HDW_GPS
	HDW_TURN_AN_ON = false;
#else
	ANA_vref_off();
#endif
}

/******************************************************************************
** Function:	Transmit internal batt fail alarms
**
** Notes:		
*/
void PWR_tx_internal_batt_alarm(void)
{
	int i;

	i = sprintf(STR_buffer, "dALARM=%02X%02X%02X,%02X:%02X:%02X,%s,",
		RTC_now.day_bcd, RTC_now.mth_bcd, RTC_now.yr_bcd,
		RTC_now.hr_bcd, RTC_now.min_bcd, RTC_now.sec_bcd,
		COM_sitename);
	
																				// get message from ALMBATT.TXT file - allow 256 characters
	if (CFS_read_line((char *)CFS_config_path, (char *)"ALMBATT.TXT", 3, &STR_buffer[i], 256) < 1)
	{
		STR_buffer[i] = '\0';													// if no file concatenate default string
		strcat(STR_buffer, "7V2Low");
	}
	i = strlen(STR_buffer);														// add value measured
	sprintf(&STR_buffer[i], ": %1.3G", (double)PWR_int_bat_when_failed);
	LOG_entry(STR_buffer);

	ALM_send_to_all_sms_numbers();
	if (COM_schedule.ftp_enable)
		ALM_send_ftp_alarm(ALM_FTP_BATTERY);
#ifndef HDW_GPS
																				// Disable all data transmissions due to internal battery alarm:
	for (i = 0; i < ANA_NUM_CHANNELS; i++)
	{
		ANA_config[i].flags &= ~(ANA_MASK_MESSAGING_ENABLED |					// all analogue - direct and derived
								 ANA_MASK_DERIVED_MESSAGING_ENABLED);
	}
#endif
#ifndef HDW_RS485
	for (i = 0; i < DIG_NUM_CHANNELS; i++)
	{
		DIG_config[i].flags &= ~DIG_MASK_MESSAGING_ENABLED;						// clear channel message enable flag
		DIG_config[i].ec[0].flags &= ~DIG_MASK_MESSAGING_ENABLED;				// clear subchannel message enable flags
		DIG_config[i].ec[1].flags &= ~DIG_MASK_MESSAGING_ENABLED;
	}
#else
	DOP_config.flags &= ~(DOP_MASK_VELOCITY_MESSAGING_ENABLED |					// clear doppler messaging enabled flags
						 DOP_MASK_TEMPERATURE_MESSAGING_ENABLED |
						 DOP_MASK_DEPTH_MESSAGING_ENABLED |
						 DOP_MASK_DERIVED_FLOW_MESSAGING_ENABLED);
#endif
	PWR_measurement_flags |= PWR_MASK_BATT_ALARM_SENT;							// indication as to what has happened
}

/******************************************************************************
** Function:	Read diode offset from ALMBATT file
**
** Notes:		
*/
void PWR_read_diode_offset(void)
{
	int i;
	float f;

	i = 0;																	// assume file not there or corrupt
	if (CFS_read_line((char *)CFS_config_path, (char *)"ALMBATT.TXT", 2, STR_buffer, 64) > 0)
		i = sscanf(STR_buffer, "%f", &f);
	pwr_diode_volts_x100 = (i < 1) ? 0 : (int16)(f * 100.0f);				// if no file or corrupt, set default
}

/******************************************************************************
** Function:	power management task
**
** Notes:		
*/
void PWR_task(void)
{
	float internal_bat_alarm_threshold;
	float internal_bat_logging_threshold;
	float ext_threshold;
	int i;
	char * char_ptr;

	if (PWR_wakeup_time <= RTC_time_sec)										// if wakeup time has passed
	{
		PWR_wakeup_time = SLP_NO_WAKEUP;
		if ((PWR_eco_config.flags & PWR_ECO_PRODUCT) != 0x00)					// if eco product
		{
			PWR_set_pending_batt_test(false);										// set pending test
			PWR_wakeup_time = RTC_time_sec;										// set next wakeup time
			if ((PWR_eco_config.flags & PWR_ECO_BELOW_THRESHOLD) != 0x00)		// if is below eco threshold
				LOG_set_next_time(&PWR_wakeup_time, 							// set wakeup to next internal interval
								  PWR_eco_config.internal_sample_interval, false);
			else
				LOG_set_next_time(&PWR_wakeup_time, 							// set wakeup to next cla-val interval
								  PWR_eco_config.cla_val_sample_interval, false);
		}
	}

	if (pwr_state != PWR_MEASUREMENT_COMPLETE)
	{
		if (!TIM_20ms_tick)
			return;

		switch (pwr_state)														// execute measurement if required
		{
		case PWR_POWERING_1:													// start powering
			HDW_BATTEST_INT_ON = true;
			HDW_BATTEST_EXT_ON = true;
			HDW_TURN_AN_ON = true;
			pwr_state = PWR_POWERING_2;
			return;																// new values not ready

		case PWR_POWERING_2:													// 20ms elapsed since power applied
			PWR_read_values();
			if ((PWR_last_ext_supply > 0.2f) && (PWR_ext_supply_volts <= 0.2f))	// check on removal of external power
			{
				if (!COM_long_interval())										// if any window interval < 15 minutes
					LOG_enqueue_value(LOG_ACTIVITY_INDEX, 						// cannot use modem rapidly on internal battery
									  LOG_COM_FILE, 
									  __LINE__);
			}
			PWR_last_ext_supply = PWR_ext_supply_volts;
			PWR_measurement_flags &= ~(PWR_MASK_MODEM_ON | PWR_MASK_SD_ON);		// clear modem & SD flags
#ifndef HDW_PRIMELOG_PLUS
			if (!HDW_MODEM_PWR_ON_N)
				PWR_measurement_flags |= PWR_MASK_MODEM_ON;
#endif
			if (!HDW_SD_CARD_ON_N)
				PWR_measurement_flags |= PWR_MASK_SD_ON;

			pwr_state = PWR_MEASUREMENT_COMPLETE;
			break;																// process new values below

		default:																// PWR_IDLE or PWR_PENDING (not MEASUREMENT_COMPLETE)
			return;																// new values not ready
		}
	}
																				// Now process new values
	if (!CFS_open())															// waiting for file system
		return;

	pwr_state = PWR_IDLE;														// Handle alarms now, so state can go back to idle:

	if ((PWR_measurement_flags & PWR_MASK_LOG_USAGE) != 0)						// See if we need to enter values read for battery & ext power in usage log:
	{
		PWR_measurement_flags &= ~PWR_MASK_LOG_USAGE;

		sprintf(STR_buffer, "Batt=%4.2f,%4.2f,%02X",
			(double)PWR_int_bat_volts, (double)PWR_ext_supply_volts, PWR_measurement_flags);
		LOG_entry(STR_buffer);
	}

	i = 0;																		// assume file not there or corrupt
	if (CFS_open() && (CFS_read_line((char *)CFS_config_path, (char *)"ALMBATT.TXT", 1, STR_buffer, 64) > 0))
		i = sscanf(STR_buffer, "%f,%f,%f", &internal_bat_logging_threshold, 
				   &internal_bat_alarm_threshold, &ext_threshold);
	
	if (i < 3)																	// if no file or corrupt, set defaults
	{
#ifdef HDW_PRIMELOG_PLUS
		internal_bat_logging_threshold = 2.7f;
		internal_bat_alarm_threshold = 3.05f;
		ext_threshold = 6.5f;													// should be above internal_bat_alarm_threshold
#else
		if ((PWR_eco_config.flags & PWR_ECO_PRODUCT) != 0x00)					// if eco product
		{
			internal_bat_logging_threshold = 2.7f;								// set lower internal thresholds
			internal_bat_alarm_threshold = 3.9f;
		}
		else
		{
			internal_bat_logging_threshold = 4.5f;
			internal_bat_alarm_threshold = 5.5f;
		}
		ext_threshold = 6.5f;													// should be above internal_bat_alarm_threshold
#endif
	}
	
	// internal battery alarm regardless of whether on external power or not.
	if ((PWR_int_bat_volts > internal_bat_logging_threshold) && (PWR_int_bat_volts < internal_bat_alarm_threshold) &&
		((PWR_measurement_flags & PWR_MASK_BATT_ALARM_SENT) == 0x00))
	{
		PWR_int_bat_when_failed = PWR_int_bat_volts;
		PWR_tx_internal_batt_alarm();
	}

	// if running on internal battery & level too low, stop all logging
	// (to avoid corrupting file system)
	// if running on ext battery or USB & int battery level low, send alarm, 
	// but don't stop logging
	if (PWR_ext_supply_volts > 0.2f)							// running on ext battery or USB
	{
		// no alarm if USB power
		if (((USB_state == USB_EXT_BATTERY) || (USB_state == USB_LOW_VOLTAGE_EXT_BATTERY)) &&
			(PWR_ext_supply_volts < ext_threshold))
		{
			i = sprintf(STR_buffer, "dALARM=%02X%02X%02X,%02X:%02X:%02X,%s,",
				RTC_now.day_bcd, RTC_now.mth_bcd, RTC_now.yr_bcd,
				RTC_now.hr_bcd, RTC_now.min_bcd, RTC_now.sec_bcd,
				COM_sitename);
			char_ptr = &STR_buffer[i];
			
			// get message from ALMBATT.TXT file - allow 256 characters
			if (!CFS_open() || (CFS_read_line((char *)CFS_config_path, (char *)"ALMBATT.TXT", 4, char_ptr, 256) < 1))
			{
				// if no file concatinate default string
				*char_ptr = '\0';
				strcat(STR_buffer, "ExtLow");
			}
			i = strlen(STR_buffer);											// add value measured
			char_ptr = &STR_buffer[i];
			sprintf(char_ptr, ": %1.3G", (double)PWR_ext_supply_volts);
			LOG_entry(STR_buffer);
			ALM_send_to_all_sms_numbers();
			if (COM_schedule.ftp_enable)
				ALM_send_ftp_alarm(ALM_FTP_BATTERY);
		}
	}
	else if ((PWR_int_bat_volts > 0.2f) && (PWR_int_bat_volts < internal_bat_logging_threshold))
	{
		// stop logging, to avoid corrupting SD card
		// NB we are not running off ext battery or USB here.
		if (LOG_state != LOG_BATT_DEAD)											// need to stop now
		{
			LOG_entry("Internal battery flat. Logging disabled until reset.");	// Write event before we permanently disable SD writes
			LOG_state = LOG_BATT_DEAD;											// power down transducers:
#ifndef HDW_RS485
			DIG_start_stop_logging();
#endif
#ifndef HDW_GPS
			ANA_start_stop_logging();
#endif
		}
	}

	// read diode drop voltage for next time around:
	PWR_read_diode_offset();

	// Generate Eco alarms if required
	if ((PWR_eco_config.flags & PWR_ECO_PRODUCT) == 0x00)					// if not eco product
		return;
	// else:

	if (PWR_int_bat_volts <= 0.2f)											// no internal battery
		return;
	// else:

	if (PWR_int_bat_volts <= PWR_eco_config.low_alarm_threshold)			// internal is below threshold
	{
		if ((PWR_eco_config.flags & PWR_ECO_BELOW_THRESHOLD) == 0x00)		// if was not below threshold last time
		{
			PWR_eco_config.flags |= PWR_ECO_BELOW_THRESHOLD;				// set below threshold flag
			if (PWR_eco_config.eco_power_loss_mask != 0x00)					// if required to generate below threshold alarm
			{
				i = sprintf(STR_buffer, "dALARM=%02X%02X%02X,%02X:%02X:%02X,%s,",
					RTC_now.day_bcd, RTC_now.mth_bcd, RTC_now.yr_bcd,
					RTC_now.hr_bcd, RTC_now.min_bcd, RTC_now.sec_bcd,
					COM_sitename);
				char_ptr = &STR_buffer[i];
																		// get message from ECO.TXT file line 3 - allow 256 characters
				if (!CFS_open() || (CFS_read_line((char *)CFS_config_path, (char *)CFS_eco_name, 3, char_ptr, 256) < 1))
				{
					*char_ptr = '\0';
					strcat(STR_buffer, "ECO lost");						// if no file concatinate default string
				}
				i = strlen(STR_buffer);									// add value measured
				char_ptr = &STR_buffer[i];
				sprintf(char_ptr, ": %1.3G", (double)PWR_int_bat_volts);
				STR_buffer[160] = '\0';									// truncate message length to 160 bytes
				if ((PWR_eco_config.eco_power_loss_mask & PWR_LOG_ALARM) != 0x00)
					ALM_log_pwr_alarm();
				if ((PWR_eco_config.eco_power_loss_mask & PWR_SMS_ALARM) != 0x00)
					ALM_send_to_all_sms_numbers();
				if ((PWR_eco_config.eco_power_loss_mask & PWR_FTP_ALARM) != 0x00)
					ALM_send_ftp_alarm(ALM_FTP_BATTERY);
			}	
			PWR_wakeup_time = RTC_time_sec;								// set next wakeup time
			LOG_set_next_time(&PWR_wakeup_time, 						// set wakeup to next internal interval
							  PWR_eco_config.internal_sample_interval, false);
		}
	}
	else	// internal above threshold - so powered by CLA-VAL
	{
		if ((PWR_eco_config.flags & PWR_ECO_BELOW_THRESHOLD) != 0x00)	// if was below threshold last time
		{
			PWR_eco_config.flags &= ~PWR_ECO_BELOW_THRESHOLD;			// clear below threshold flag
			if (PWR_eco_config.eco_power_restored_mask != 0x00)			// if required to generate above threshold alarm
			{
				i = sprintf(STR_buffer, "dALARM=%02X%02X%02X,%02X:%02X:%02X,%s,",
					RTC_now.day_bcd, RTC_now.mth_bcd, RTC_now.yr_bcd,
					RTC_now.hr_bcd, RTC_now.min_bcd, RTC_now.sec_bcd,
					COM_sitename);
				char_ptr = &STR_buffer[i];
																		// get message from ECO.TXT file line 4 - allow 256 characters
				if (!CFS_open() || (CFS_read_line((char *)CFS_config_path, (char *)CFS_eco_name, 4, char_ptr, 256) < 1))
				{
					*char_ptr = '\0';
					strcat(STR_buffer, "ECO restored");					// if no file concatinate default string
				}
				i = strlen(STR_buffer);									// add value measured
				char_ptr = &STR_buffer[i];
				sprintf(char_ptr, ": %1.3G", (double)PWR_int_bat_volts);
				STR_buffer[160] = '\0';									// truncate message length to 160 bytes
				if ((PWR_eco_config.eco_power_restored_mask & PWR_LOG_ALARM) != 0x00)
					ALM_log_pwr_alarm();
				if ((PWR_eco_config.eco_power_restored_mask & PWR_SMS_ALARM) != 0x00)
					ALM_send_to_all_sms_numbers();
				if ((PWR_eco_config.eco_power_restored_mask & PWR_FTP_ALARM) != 0x00)
					ALM_send_ftp_alarm(ALM_FTP_ECO);
			}	
			PWR_wakeup_time = RTC_time_sec;								// set next wakeup time
			LOG_set_next_time(&PWR_wakeup_time, 						// set wakeup to next cla-val interval
							  PWR_eco_config.cla_val_sample_interval, false);
		}
	}
}

/******************************************************************************
** Function:	initialise eco power config
**
** Notes:		
*/
void PWR_eco_init(void)
{
	PWR_eco_config.flags = _B00000011;											// start with internal battery flag set
	PWR_eco_config.low_alarm_threshold = 5.5F;									// these are the defaults used if no ECO.TXT fiel found in SYSYEM\CAL
	PWR_eco_config.cla_val_sample_interval = 14;
	PWR_eco_config.internal_sample_interval = 24;
	PWR_eco_config.eco_power_loss_mask = _B00000111;
	PWR_eco_config.eco_power_restored_mask = _B00000111;

	PWR_wakeup_time = SLP_NO_WAKEUP;
}

/******************************************************************************
** Function:	Set batt test pending, & optionally log voltages once read
**
** Notes:		
*/
void PWR_set_pending_batt_test(bool log_usage)
{
	if (log_usage)
		PWR_measurement_flags |= PWR_MASK_LOG_USAGE;

#ifdef HDW_PRIMELOG_PLUS
	pwr_state = PWR_POWERING_1;			// test immediately
#else
	if (pwr_state == PWR_PENDING)		// haven't tested since last set pending (no modem power-up?)
		pwr_state = PWR_POWERING_1;		// so test immediately
	else if (pwr_state == PWR_IDLE)
		pwr_state = PWR_PENDING;

	// else it's in progress, or done and awaiting processing of values
#endif
}

/******************************************************************************
** Function:	Measure batteries
**
** Notes:		Call just before modem powered up for preference.
**				Returns false when done, or if not required, otherwise true
*/
bool PWR_measure_in_progress(void)
{
	switch (pwr_state)
	{
	case PWR_PENDING:						// start measurement
		pwr_state = PWR_POWERING_1;
		return true;						// busy

	case PWR_POWERING_1:
	case PWR_POWERING_2:
		return true;						// busy
	}

	// else PWR_IDLE  or PWR_MEASUREMENT_COMPLETE: not busy
	return false;
}
