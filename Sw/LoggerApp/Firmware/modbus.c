/******************************************************************************
** File:	modbus.c	
**
** Notes:	Modbus comms with Krohne dooberry
**
** V6.00 300316 PQ first version
*/

#include <string.h>
#include <stdio.h>
#include <float.h>
//#include <stdint.h>

#include "custom.h"
#include "Compiler.h"
#include "str.h"
#include "HardwareProfile.h"
#include "MDD File System/FSIO.h"
#include "Tim.h"
#include "Rtc.h"
#include "Slp.h"
#include "Log.h"
#include "Ftp.h"
#include "Alm.h"
#include "Pdu.h"
#include "Usb.h"
#include "ser.h"

#define extern
#include "modbus.h"
#undef extern

MOD_CONFIG_t MOD_config;

enum {
	STATE_IDLE = 0,
	STATE_START_READING,
	STATE_READ_STARTUP,
	STATE_READ_LOOP_1,
	STATE_READ_LOOP_2,
	STATE_READ_THRESHOLDS_1,
	STATE_READ_THRESHOLDS_2,
	STATE_READ_THRESHOLDS_3,
	STATE_FINISHED,

	STATE_FAILED
};

uint8	mod_state = STATE_IDLE;


const uint16	channel_addresses[NUM_MOD_CHANNELS] = { MOD_ADDR_FLOW_SPEED_MPS,
													MOD_ADDR_FLOW_RATE_M3S,
													MOD_ADDR_TOTAL_FLOW_M3,
													MOD_ADDR_PRESSURE_MBAR,
													MOD_ADDR_TEMPERATURE_K,
													MOD_ADDR_FORWARD_VOLUME_M3,
													MOD_ADDR_REVERSE_VOLUME_M3,
													UINT16_MAX };	// combination of various bits
const uint8	MOD_channel_units[NUM_MOD_CHANNELS]			= { 45, 3, 8, 7, 15, 8, 8, 0 };
const uint8	MOD_channel_description[NUM_MOD_CHANNELS]	= { 16, 1, 4, 2, 8,  4, 4, 0 };

#define RETRY_DELAY				(1000/20)
#define CHANNEL_DELAY			(20/20)
//#define CHANNEL_DELAY			0

float	MOD_channel_mins[NUM_MOD_CHANNELS];
RTC_hhmmss_type	MOD_channel_mins_time[NUM_MOD_CHANNELS];
float	MOD_channel_maxes[NUM_MOD_CHANNELS];
RTC_hhmmss_type	MOD_channel_maxes_time[NUM_MOD_CHANNELS];

uint32	MOD_wakeup_time;
uint8	mod_last_alarms = 0;
uint8	mod_new_alarms = 0;
int16	MOD_pressure_limit_max_mbar = 0;
int16	MOD_pressure_limit_min_mbar = 0;
uint16	MOD_temp_limit_max_k = 0;
uint16	MOD_temp_limit_min_k = 0;
float	mod_last_pressure_mbar = 0;
float	mod_last_temp_k = 0;
bool	MOD_tx_enable = false;

RTC_type	mod_reading_timestamp;


/******************************************************************************
** Function:	MOD_init
**
** Notes:		Set defaults
*/
void MOD_init(void)
{
	MOD_config.interval = 0;	// default, change later
	//MOD_config.channel_enable_bits = _B00011111;
	MOD_config.channel_enable_bits = 0;
	//LOG_set_next_time(&MOD_wakeup_time, MOD_config.interval, false);
	MOD_wakeup_time = SLP_NO_WAKEUP;
/*
	LOG_header_mask &= ~(1 << LOG_SERIAL_1_INDEX);
	LOG_header_mask &= ~(1 << LOG_SERIAL_2_INDEX);
	LOG_header_mask &= ~(1 << LOG_SERIAL_3_INDEX);
	LOG_header_mask &= ~(1 << LOG_SERIAL_4_INDEX);
	LOG_header_mask &= ~(1 << LOG_SERIAL_5_INDEX);
*/
}

/******************************************************************************
** Function:	MOD_log_measurement
**
** Notes:		save a read value to channel log
*/
void mod_log_measurement(uint8 channel, int32 d)
{
	if ((LOG_header_mask & (1 << (LOG_SERIAL_1_INDEX+channel))) == 0)				// needs header
	{
		LOG_enqueue_value(LOG_SERIAL_1_INDEX + channel,								// enqueue header 
			LOG_BLOCK_HEADER_TIMESTAMP,
			mod_reading_timestamp.reg32[0]);
		LOG_header_mask |= 1 << (LOG_SERIAL_1_INDEX+channel);						// clear header mask bit
	}
	SER_last_read_value = *(float *)&d;
	LOG_enqueue_value(LOG_SERIAL_1_INDEX + channel,			 						// enqueue value
		LOG_DATA_VALUE, 
		d);
	FTP_activate_retrieval_info(channel);										// start channel if not already running
	FTP_update_retrieval_info(channel, &mod_reading_timestamp);
}

/******************************************************************************
** Function:	mod_set_interval
**
** Notes:		set the Krohne's reception interval to the time until the next
**				set sample time
*/
void mod_set_interval(void)
{
	uint16 v = 3600;
	SER_write(MOD_FUNC_PRESET_SINGLE_REG, MOD_ADDR_RECEPTION_INTERVAL_S, v);
}

/******************************************************************************
** Function:	MOD_can_sleep
**
** Notes:		Returns true when MODBUS is idle
*/
bool MOD_can_sleep(void)
{
	return (mod_state == STATE_IDLE);
}

/******************************************************************************
** Function:	mod_decode_double
**
** Notes:		convert 8 modbus bytes to a double
*/
double mod_decode_double(uint8 *start_byte)
{
	int i;
	double d;
	uint8 *p = (uint8 *)&d;
	for (i = 7; i > 0; i--)
		p[i] = *start_byte++;
	return d;
}

/******************************************************************************
** Function:	Log totaliser + min/max, then re-initialise for next day
**
** Notes:		
*/
void mod_midnight_task(void)
{
	uint8 channel;
	for (channel = 0; channel < NUM_MOD_CHANNELS; channel++)
	{
		if (MOD_config.channel_enable_bits & (1<<channel))
		{
			LOG_enqueue_value(LOG_SERIAL_1_INDEX + channel, LOG_BLOCK_FOOTER, 0);				// enqueue footer
			//LOG_enqueue_value((LOG_SERIAL_1_INDEX + channel) | LOG_SMS_MASK, LOG_BLOCK_FOOTER, 0);
		}
	}
}

/******************************************************************************
** Function:	mod_set_hhmmss
**
** Notes:		make timestamps for min/max
*/
void mod_set_hhmmss(RTC_hhmmss_type *p_dest, RTC_type *p_src)
{
	p_dest->hr_bcd = p_src->hr_bcd;
	p_dest->min_bcd = p_src->min_bcd;
	p_dest->sec_bcd = p_src->sec_bcd;
	p_dest->padding = 0;
}

/******************************************************************************
** Function:	MOD_task
**
** Notes:		called from the main loop
*/
void MOD_task(void)
{
	static uint16	timer20ms = 0;
	static uint8	last_state = STATE_IDLE;
	static bool		new_day = false;
	//static uint8	retries = 0;

	uint8	channel = 0;
	char temp[32];

	if (TIM_20ms_tick)					// run timer																		
	{
		if (timer20ms > 0)
			timer20ms--;
	}

	switch(mod_state)
	{
	default: break;
	case STATE_IDLE:
		if (new_day && (RTC_time_sec < 600))
			new_day = false;
		if (new_day)
			break;
		if (MOD_wakeup_time > RTC_time_sec)
			break;
		// else fall through
		if (mod_state != last_state)
		{
			//USB_monitor_string("IDLE");
			last_state = mod_state;
		}
		//LOG_set_next_time(&MOD_wakeup_time, MOD_config.interval, true);
		mod_reading_timestamp.reg32[0] = RTC_now.reg32[0];
		mod_reading_timestamp.reg32[1] = RTC_now.reg32[1];

	case STATE_START_READING:
		if (mod_state != last_state)
		{
			//USB_monitor_string("START_READING");
			last_state = mod_state;
		}
		//channel = 0;
		SER_RS485_on();
		timer20ms = 5;		// start up time for RS485
		//retries = 2;
		mod_state = STATE_READ_STARTUP;
		break;

	case STATE_READ_STARTUP:
		if (mod_state != last_state)
		{
			//USB_monitor_string("READ_STARTUP");
			last_state = mod_state;
		}
		if (timer20ms == 0)
			mod_state = STATE_READ_LOOP_1;
		break;

	case STATE_READ_LOOP_1:
		if (mod_state != last_state)
		{
			//USB_monitor_string("READ_LOOP_1");
			last_state = mod_state;
		}
		if (timer20ms > 0)
			break;
		if (!SER_read(4, 3000, 4*16))
		{
			// retries removed
			// RS485 busy, wait some time and retry
			//if (retries == 0)
			//{
				mod_state = STATE_FAILED;
				break;
			//}
			//retries--;
			mod_state = STATE_READ_STARTUP;
			timer20ms = RETRY_DELAY;
		}
		mod_state = STATE_READ_LOOP_2;
		break;

	case STATE_READ_LOOP_2:
		if (mod_state != last_state)
		{
			//USB_monitor_string("READ_LOOP_2");
			last_state = mod_state;
		}
		// timeout failure
		if (SER_timeout)
		{
			// retries removed
			//if (retries == 0)
			//{
				mod_state = STATE_FAILED;
				break;
			//}
			//retries--;
			timer20ms = RETRY_DELAY;
			mod_state = STATE_READ_LOOP_1;
		}

		// value read
		if (SER_transaction_finished)
		{
			if (new_day)
			{
				LOG_header_mask &= ~(1 << (LOG_SERIAL_1_INDEX));
				LOG_header_mask &= ~(1 << (LOG_SERIAL_2_INDEX));
				LOG_header_mask &= ~(1 << (LOG_SERIAL_3_INDEX));
				LOG_header_mask &= ~(1 << (LOG_SERIAL_4_INDEX));
				LOG_header_mask &= ~(1 << (LOG_SERIAL_5_INDEX));
				LOG_header_mask &= ~(1 << (LOG_SERIAL_6_INDEX));
				LOG_header_mask &= ~(1 << (LOG_SERIAL_7_INDEX));
				LOG_header_mask &= ~(1 << (LOG_SERIAL_8_INDEX));
			}
			new_day = false;
		
			// log values
			for (channel = 0; channel < NUM_MOD_CHANNELS; channel++)
			{
				if (channel_addresses[channel] != UINT16_MAX)
				{
					double d = mod_decode_double(&mod_rx_buffer_AT[3 + (channel_addresses[channel] * 2)]);
					float f = (float)d;
					sprintf(temp, "%u:%lf", channel, d);
					USB_monitor_string(temp);
					if (MOD_config.channel_enable_bits & (1<<channel))
						mod_log_measurement(channel, *(int32 *)&f);
					if (channel == 3)
						mod_last_pressure_mbar = (float)d;
					if (channel == 4)
						mod_last_temp_k = (float)d;
					
					if (MOD_channel_mins[channel] > d)
					{
						MOD_channel_mins[channel] = (float)d;
						mod_set_hhmmss(&MOD_channel_mins_time[channel], &RTC_now);
					}
					if (MOD_channel_maxes[channel] < d)
					{
						MOD_channel_maxes[channel] = (float)d;
						mod_set_hhmmss(&MOD_channel_maxes_time[channel], &RTC_now);
					}
				}
				else
				{
					// special case bitfield channel
					uint32_t bits = 0;
					bits = mod_rx_buffer_AT[3 + (MOD_ADDR_PRESSURE_ALARMS*2)] & 0x1F;						// +5
					bits |= ((uint32)mod_rx_buffer_AT[3 + (MOD_ADDR_TEMP_ALARMS*2)] & 0x1F) << 5;			// +5
					bits |= ((uint32)mod_rx_buffer_AT[3 + (MOD_ADDR_ERROR_WARNINGS*2)] & 0x1F) << 10;		// +5
					bits |= ((uint32)mod_rx_buffer_AT[3 + (MOD_ADDR_BATTERY_TYPE*2)] & 0x3) << 15;			// +2
					bits |= ((uint32)mod_rx_buffer_AT[3 + (MOD_ADDR_BATTERY_REMAIN_AH*2)] & 0x3FFF) << 17;	// +14
					bits |= ((uint32)mod_rx_buffer_AT[3 + (MOD_ADDR_FLOW_DIR*2)] & 0x1) << 31;				// +1
					sprintf(temp, "%u:%08lX", channel, bits);
					USB_monitor_string(temp);
					if (MOD_config.channel_enable_bits & (1<<channel))
						mod_log_measurement(channel, bits);
				}
			}
			mod_state = STATE_FINISHED;
		}
		break;

	case STATE_FAILED:
		if (mod_state != last_state)
		{
			//USB_monitor_string("FAILED");
			last_state = mod_state;
		}
		mod_state = STATE_FINISHED;
		//break;
		// fall through

	case STATE_FINISHED:
		if (mod_state != last_state)
		{
			//USB_monitor_string("FINISHED");
			last_state = mod_state;
		}
		SER_RS485_off();
		mod_state = STATE_IDLE;

		//sprintf(temp, "rtc:%lu", RTC_time_sec);
		//USB_monitor_string(temp);
		//sprintf(temp, "old:%lu", MOD_wakeup_time);
		//USB_monitor_string(temp);

		MOD_wakeup_time = RTC_time_sec;
		LOG_set_next_time(&MOD_wakeup_time, MOD_config.interval, true);
		if (MOD_wakeup_time == 0)
		{
			mod_midnight_task();
			new_day = true;
		}

		//sprintf(temp, "new:%lu", MOD_wakeup_time);
		//USB_monitor_string(temp);

		break;
	}
}
