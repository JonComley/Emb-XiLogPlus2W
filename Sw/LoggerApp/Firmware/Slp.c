/******************************************************************************
** File:	Slp.c
**
** Notes:
**
** V2.65 070411 PB  added conditional compilation ifndef HDW_PRIMELOG_PLUS adding COM and MDM wakeup times to SLP_wakeup_times list
**
** V3.00 260911 PB  Waste Water - add COP_wakeup_time to wakeup sources
**								  add !COP_can_sleep() test to staying awake
**
** V3.04 201211 PB  add DOP_wakeup_time to wakeup sources
**                  add SER_busy() and !DOP_can_sleep() test to staying awake 
**
** V3.08 280212 PB  ECO product - add PWR_wakeup_time to list 
**
** V3.34 041213 PB  change SLP_wakeup_times[] to include shadow channels for 3 channel loggers
**
** V4.00 220114 PB  if HDW_GPS disable all analogue calls and functions
**
** V4.02 140414 PB  need high speed clock if UART is on in RS485 version
**
** V4.04 010514 PB  GPS - add GPS_wakeup_time to wakeup sources
*/

#include "Custom.h"
#include "Compiler.h"
#include "HardwareProfile.h"
#include "MDD File System/SD-SPI.h"
#include "MDD File System/FSIO.h"

#include "Tim.h"
#include "Cfs.h"
#include "Usb.h"
#include "rtc.h"
#include "Sns.h"
#include "Dig.h"
#include "Ana.h"
#include "Msg.h"
#include "Com.h"
#include "Mdm.h"
#include "Log.h"
#include "Pdu.h"
#include "ftp.h"
#include "alm.h"
#include "Scf.h"
#include "Cop.h"
#include "Ser.h"
#include "Dop.h"
#include "Pwr.h"
#include "gps.h"
#include "modbus.h"

#define extern
#include "Slp.h"
#undef extern

// OSCCON high byte values:
#define SLP_CLK_USB			_B00000011		// Primary osc. + PLL
#define SLP_CLK_FAST		_B00000010		// Primary osc.
#define SLP_CLK_DEFAULT		_B00000111		// Fast RC with postscaler

uint32 slp_wakeup_time;

uint16 slp_port_c;		// contains Modem interrupt line - bit 4
uint16 slp_port_f;		// contains USB activity monitor line - bit 8

uint32 * const SLP_wakeup_times[] =
{
#ifndef HDW_RS485
	&DIG_channel[0].sample_time,										// primary channel
	&DIG_channel[0].sub[0].event_time,
	&DIG_channel[0].sub[1].event_time,
	&DIG_channel[0].sub[0].event_log_time,
	&DIG_channel[0].sub[1].event_log_time,
	&DIG_channel[1].sample_time,										// second channel or shadow channel if 3-ch
	&DIG_channel[1].sub[0].event_time,
	&DIG_channel[1].sub[1].event_time,
	&DIG_channel[1].sub[0].event_log_time,
	&DIG_channel[1].sub[1].event_log_time,
#endif
#ifndef HDW_GPS
	&ANA_channel[0].sample_time,										// primary channels
	&ANA_channel[1].sample_time,
	&ANA_channel[2].sample_time,										// channel 3 and 4 or shadow channels if 3-ch
	&ANA_channel[3].sample_time,
#else
	&GPS_wakeup_time,
#endif
#if (HDW_NUM_CHANNELS == 9)
	&ANA_channel[4].sample_time,
	&ANA_channel[5].sample_time,
	&ANA_channel[6].sample_time,
#endif
#ifndef HDW_PRIMELOG_PLUS
	&COM_wakeup_time,
	&MDM_wakeup_time,
#endif
	&LOG_wakeup_time,
	&USB_wakeup_time,
	&ALM_wakeup_time,
#ifdef HDW_RS485
	&MOD_wakeup_time,
#endif
	&COP_wakeup_time,
	&PWR_wakeup_time
};

const uint8 SLP_num_wakeup_sources = sizeof(SLP_wakeup_times) / sizeof(SLP_wakeup_times[0]);

/******************************************************************************
** Function:	USB connected interrupt routine
**
** Notes:	This routine should never get executed. It is only included to prove
**			that the unit is not vectoring to a non-existent interrupt when it
**			wakes from sleep.
*
void __attribute__((__interrupt__, no_auto_psv)) _INT1Interrupt(void)
{
	HDW_TEST_LED = true;
}
*/

/******************************************************************************
** Function:	Set CPU clock speed according to what's required
**
** Notes:	
*/
void SLP_set_required_clock_speed(void)
{
	uint8 c;

	// select PLL as required:
	c = SLP_CLK_DEFAULT;

	if (!HDW_MODEM_PWR_ON_N || !HDW_SD_CARD_ON_N || USB_active)
		c = SLP_CLK_USB;

	if (!HDW_RS485_ON_N || !MOD_can_sleep())
		c = SLP_CLK_USB;

	if (OSCCONbits.COSC == c)					// no need to switch
		return;

	asm_volatile("disi	#7");
	__builtin_write_OSCCONH(c);

	c = LOWBYTE(OSCCON) | _B00000001;			// set OSWEN
	asm_volatile("disi	#7");
	__builtin_write_OSCCONL(c);

	// Code to try & prevent random crashes - looks stupid, but do NOT remove.
	// It seems that if we do anything useful while clock is switching, a crash can occur
	c = 0;
	while (OSCCONbits.NOSC != OSCCONbits.COSC)
	{
		if (c++ == 255)
			break;
	}

	// If we're in default mode, set FRC clock postscaler
	CLKDIVbits.RCDIV = _B00000011;				// 1Mhz CPU operation
}

/******************************************************************************
** Function:	Get system clock speed
**
** Notes:		Assumes clock switching complete
*/
uint32 SLP_get_system_clock(void)
{
	switch (OSCCONbits.COSC)
	{
	case SLP_CLK_USB:
		return 32000000;

	case SLP_CLK_FAST:
		return 4000000;
	}

	// default:
	return 1000000;
}

/******************************************************************************
** Function:	Sleep when able
**
** Notes:		
*/
void SLP_task(void)
{
	uint16 i;

	// In order to sleep, must be no USB, no modem activity,
	// no CFS activity, no sensor PIC comms in progress,
	// no PDU activity
	if ((SNS_command_flags.mask != 0) || 
		(CFS_timer_x20ms != 0) || 
		!COM_can_sleep() || 
		USB_active || 
		PDU_busy() || 
		FTP_busy() || 
		!COP_can_sleep() || 
		SER_busy() ||
		!MOD_can_sleep() ||
		//DOP_busy() ||
		!ALM_can_sleep()
		)
		return;
	if (SCF_progress() != 100)
		return;									// stay awake
	// else:

	LOG_set_wakeup_time();
	COM_set_wakeup_time();

	slp_wakeup_time = 0xFFFFFFFFL;
	for (i = 0; i < SLP_num_wakeup_sources; i++)
	{
		if (*SLP_wakeup_times[i] < slp_wakeup_time)			// new minimum
		{
			if (*SLP_wakeup_times[i] > RTC_time_sec)		// wake up in the future
				slp_wakeup_time = *SLP_wakeup_times[i];
			else											// stay awake for now
			{
				slp_wakeup_time = RTC_time_sec;
				return;
			}
		}
	}

	// check if no wakeup scheduled: if so wakeup at midnight instead.
	if (slp_wakeup_time >= RTC_SEC_PER_DAY)
	{
		RTC_set_alarm(0L);						// wakeup at midnight
		slp_wakeup_time = RTC_SEC_PER_DAY;		// check OK to sleep before midnight below
	}
	else
		RTC_set_alarm(slp_wakeup_time);

	// Wakeup time must be at least half a sec in the future, else stay awake.
	RTC_get_time_now();
	if (RTC_half_sec)		// time now = second half of a sec
	{
		if (slp_wakeup_time <= RTC_time_sec + 1)	// e.g. time_now = 37.5, wakeup = 38, don't sleep
			return;
		// else OK to sleep
	}
	else if (slp_wakeup_time <= RTC_time_sec)		// e.g. time_now = 37.0, wakeup = 37, don't sleep
		return;
	// else OK to sleep

	CFS_power_down();

	// Set CPU priority level to 2, so interrupts can wake without vectoring
	_IPL = 2;

	// always wake up on change on HDW_USB_MON - RP15/CN74/RF8
	slp_port_f = PORTF;

	// modem ring interrupt enable and disable is controlled by modem on and off functions
	// and follows modem on and off states
	slp_port_c = PORTC;

	// enable CN interrupts
	IEC1bits.CNIE = 1;

#ifndef HDW_RS485
	// enable event logging interrupts
	if (DIG_INT1_active) _INT1IE = true;			// enable INT1
	if (DIG_INT2_active) _INT2IE = true;			// enable INT2
  #if (HDW_NUM_CHANNELS == 9)
	if (DIG_INT3_active) _INT3IE = true;			// enable INT3
	if (DIG_INT4_active) _INT4IE = true;			// enable INT4
  #endif
#endif

	// Wake up on RTC
	_RTCIP = 1;
	_RTCIE = true;

	PWR_drive_debug_led(false);
#ifndef ECLIPSE
	Sleep();
	Nop();
#endif
	PWR_drive_debug_led(true);

	// if CN interrupt, could be either USB connection or modem activity
	if (IFS1bits.CNIF == 1)
	{
		// if USB
		i = PORTF;
		if (slp_port_f != i)
		{
			slp_port_f = i;
			// clear CN interrupt flag
			IFS1bits.CNIF = 0;
			// USB_task will pick it up & act
		}
		else
		{
			// must be modem
			slp_port_c = PORTC;
			// leave CN interrupt flag set for COM_task to pick up
		}
		// disable CN interrupts
		IEC1bits.CNIE = 0;
	}

	// disable event logging interrupts
	_INT1IE = false;						// disable INT1
	_INT2IE = false;						// disable INT2
#if (HDW_NUM_CHANNELS == 9)
	_INT3IE = false;						// disable INT3
	_INT4IE = false;						// disable INT4
#endif

	if (_RTCIF)								// Scheduled event
	{
		_RTCIF = false;
		_RTCIE = false;
	}

	TIM_init();						// reset timers

	// Check we finished clock switch before we slept
	if (OSCCONbits.COSC != SLP_CLK_DEFAULT)
		SLP_set_required_clock_speed();
}


