/*****************************************************************************
 *
 * Real Time Clock Calender
 *
 *****************************************************************************
 * FileName:        rtcc.c
**
** Changes:
**
** V3.28 030713 PB DEL192 - do not call MDM_change_time() in RTC_add_time() as it is called in RTC_set_time()
 */

#include "Custom.h"
#include "Compiler.h"
#include <p24Fxxxx.h>
#include <string.h>
#ifdef WIN32
#define Nop()
#endif

#define extern
#include "rtc.h"
#undef extern

#include "Log.h"
#include "Mdm.h"

#define RTC_BCD_TO_VALUE(X)	((((X) >> 4) * 10) + ((X) & 0x0F))

// No. of days into year at end of each month
#define RTC_DAYS0	0
#define RTC_DAYS1	(RTC_DAYS0  + 31)
#define RTC_DAYS2	(RTC_DAYS1  + 28)	// Add 1 in a leap year
#define RTC_DAYS3	(RTC_DAYS2  + 31)
#define RTC_DAYS4	(RTC_DAYS3  + 30)
#define RTC_DAYS5	(RTC_DAYS4  + 31)
#define RTC_DAYS6	(RTC_DAYS5  + 30)
#define RTC_DAYS7	(RTC_DAYS6  + 31)
#define RTC_DAYS8	(RTC_DAYS7  + 31)
#define RTC_DAYS9	(RTC_DAYS8  + 30)
#define RTC_DAYS10	(RTC_DAYS9  + 31)
#define RTC_DAYS11	(RTC_DAYS10 + 30)
#define RTC_DAYS12	(RTC_DAYS11 + 31)


uint32 rtc_previous_time;		// includes day of week

RTC_type rtc_work;

const uint16 rtc_days_to_end_month[13] =			// no such month as 0
{
	RTC_DAYS0,
	RTC_DAYS1,  RTC_DAYS2, RTC_DAYS3, RTC_DAYS4, RTC_DAYS5,
	RTC_DAYS6,  RTC_DAYS7, RTC_DAYS8, RTC_DAYS9, RTC_DAYS10,
	RTC_DAYS11, RTC_DAYS12
};

const uint8 rtc_month_days[13] =					// no such month as 0
{
	0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

const uint8 rtc_value_to_bcd[100] =
{
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19,
	0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29,
	0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
	0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
	0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
	0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
	0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
	0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
	0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99
};

uint8 RTC_bcd_from_value(uint8 value)
{
	if (value >= 100) value = 99;
	return rtc_value_to_bcd[value];
}

/******************************************************************************
** Function:	See if start/stop time is today
**
** Notes:
*/
bool RTC_start_stop_today(RTC_start_stop_type *p)
{
	return ((p->yr_bcd == RTC_now.yr_bcd) &&
			(p->mth_bcd == RTC_now.mth_bcd) &&
			(p->day_bcd == RTC_now.day_bcd));
}

/******************************************************************************
** Function:
**
** Notes:
*/
void RTC_set_start_stop_now(RTC_start_stop_type *p)
{
	p->day_bcd = RTC_now.day_bcd;
	p->mth_bcd = RTC_now.mth_bcd;
	p->yr_bcd = RTC_now.yr_bcd;
	p->hr_bcd = RTC_now.hr_bcd;
	p->min_bcd = RTC_now.min_bcd;
}

/******************************************************************************
** Function:	Compare start/stop date & time with now
**
** Notes:		Returns true if time is now or in the past
*/
bool RTC_start_stop_event(RTC_start_stop_type *p)
{
	uint16 i;

	if (p->yr_bcd == RTC_now.yr_bcd)				// this year...
	{
		HIGHBYTE(i) = p->mth_bcd;
		LOWBYTE(i) = p->day_bcd;
		if (i != RTC_now.reg[2])					// not today
			return (i < RTC_now.reg[2]);

		// Else today, so check hours & min:
		if (p->hr_bcd == RTC_now.hr_bcd)			// this hour
			return (p->min_bcd <= RTC_now.min_bcd);

		return (p->hr_bcd <= RTC_now.hr_bcd);
	}
	// else:

	return (p->yr_bcd < RTC_now.yr_bcd);
}

/******************************************************************************
** Function:	Check if a time in hh:mm is in the past
**
** Notes:		Return true if past or present, false if future
*/
bool RTC_hhmm_past(RTC_hhmm_type *p)
{
	if (p->hr_bcd < RTC_now.hr_bcd)
		return true;

	if (p->hr_bcd > RTC_now.hr_bcd)
		return false;

	// else the hour is nigh:
	return (p->min_bcd <= RTC_now.min_bcd);
}

/*****************************************************************************
 * Function: RTC_unlock
 *
 * Preconditions: None.
 *
 * Overview: The function allows a writing into the clock registers.
 *
 * Input: None.
 *
 * Output: None.
 *
 *****************************************************************************/
void rtc_unlock(void)
{
	asm_volatile("disi	#5");
	asm_volatile("mov	#0x55, w0");
	asm_volatile("mov	w0, _NVMKEY");
	asm_volatile("mov	#0xAA, w0");
	asm_volatile("mov	w0, _NVMKEY");
    asm_volatile("bset	_RCFGCAL, #13");
	Nop();
	Nop();
}

/******************************************************************************
** Function:	Get time & date into work structure
**
** Notes:
*/
void rtc_read(void)
{
	_RTCPTR = 3;					// auto-decrements on read of RTCVAL
	rtc_work.reg[3] = RTCVAL;
	rtc_work.reg[2] = RTCVAL;
	rtc_work.reg[1] = RTCVAL;
	rtc_work.reg[0] = RTCVAL;
}

/******************************************************************************
** Function:	Convert BCD date to number of days since 01/01/00
**
** Notes:		returns 0xffff if fault in day or month
**				01/01/00 is day 0
*/
uint16 rtc_get_days_to_date(uint8 dd_bcd, uint8 mm_bcd, uint8 yy_bcd)
{
	uint16 days;
	int ymod4, m, d;

	ymod4 = RTC_BCD_TO_VALUE(yy_bcd);
	days = ((365 * 3) + 366) * (ymod4 >> 2);	// days to start of 4-year block
	ymod4 &= 0x03;								// remaining years into 4-year block
	days += 365 * ymod4;
	if (ymod4 != 0)								// leap year at beginning of each 4-year block
		days++;
	// Now add in number of days to start of this month:
	m = RTC_BCD_TO_VALUE(mm_bcd);
	days += rtc_days_to_end_month[m - 1];
	if ((ymod4 == 0) && (m > 2))				// after Feb in a leap year
		days++;
	d = RTC_BCD_TO_VALUE(dd_bcd);
	// test for faulty date
	if ((m == 0) || (d == 0) || (d > rtc_month_days[m]))	// possible date error
	{
		if ((ymod4 != 0) || (m != 2) || (d != 29))			// not Feb 29 in a leap year
			return 0xffff;
		// else it's OK
	}
	days += d;
	days--;		// adjustment for day 0 = 01/01/00, not day 1 PB 050110

	return days;
}

/******************************************************************************
** Function:	Convert BCD time to minutes
**
** Notes:
*/
uint16 RTC_bcd_to_min(uint8 hh, uint8 mm)
{
	return ((uint16)RTC_BCD_TO_VALUE(hh) * 60) + RTC_BCD_TO_VALUE(mm);
}

/******************************************************************************
** Function:	Convert BCD time to seconds
**
** Notes:
*/
uint32 RTC_bcd_time_to_sec(uint8 hh, uint8 mm, uint8 ss)
{
	uint16 i;

	i = RTC_bcd_to_min(hh, mm) * 30;
	return ((uint32)i << 1) + RTC_BCD_TO_VALUE(ss);
}

/******************************************************************************
** Function:	Convert time type to seconds since 00:00:00,1/1/00
**
** Notes:
*/
uint32 RTC_time_date_to_sec(RTC_type *p)
{
	uint32 result = 0;
	uint16 days = rtc_get_days_to_date(p->day_bcd, p->mth_bcd, p->yr_bcd);

	result = (uint32)days * RTC_SEC_PER_DAY;
	result += RTC_bcd_time_to_sec(p->hr_bcd, p->min_bcd, p->sec_bcd);
	return result;
}

/******************************************************************************
** Function:	Get time & date now
**
** Notes:		Check rollover has not occurred while getting current time
*/
void RTC_get_time_now(void)
{
	uint16 i;

	rtc_read();										// first read
	for (i = 4; i != 0; i--)
	{
		RTC_half_sec = RCFGCALbits.HALFSEC;
		RTC_now.reg32[0] = rtc_work.reg32[0];
		RTC_now.reg32[1] = rtc_work.reg32[1];

		rtc_read();									// 2nd, 3rd, 4th, 5th read

		if (RTC_now.reg[0] == rtc_work.reg[0])
			break;
	}

	// If the time is different from the previous BCD value, update the integer time value
	if (rtc_previous_time != RTC_now.reg32[0])
	{
		rtc_previous_time = RTC_now.reg32[0];
		i = RTC_bcd_to_min(RTC_now.hr_bcd, RTC_now.min_bcd) * 30;
		RTC_time_sec = ((uint32)i << 1) + RTC_BCD_TO_VALUE(RTC_now.sec_bcd);
	}
}

/******************************************************************************
** Function:	Get previous day, given pointer to a RTC_time type
**
** Notes:		return false if out of range
*/
bool RTC_get_previous_day(RTC_type *p)
{
	if ((p->day_bcd == 0x00) || (p->day_bcd > 0x31))
	{
		return false;
	}
	if ((p->mth_bcd == 0x00) || (p->mth_bcd > 0x12))
	{
		return false;
	}

	// if not first of month
	if (p->day_bcd > 0x01)
	{
		p->day_bcd = rtc_value_to_bcd[RTC_BCD_TO_VALUE(p->day_bcd) - 1];
	}
	else
	{
		// need to set last day of previous month
		// if not January
		if (p->mth_bcd > 0x01)
		{
			p->mth_bcd = rtc_value_to_bcd[RTC_BCD_TO_VALUE(p->mth_bcd) - 1];
		}
		else
		{
			// need to set to December of previous year if there is one
			if (p->yr_bcd == 0x00)
			{
				return false;
			}
			p->yr_bcd = rtc_value_to_bcd[RTC_BCD_TO_VALUE(p->yr_bcd) - 1];
			p->mth_bcd = 0x12;
		}
		// set day to last of month taking into account leap years
		p->day_bcd = rtc_value_to_bcd[rtc_month_days[RTC_BCD_TO_VALUE(p->mth_bcd)]];
		// if feb and a leap year
		if ((p->mth_bcd == 0x02) && ((RTC_BCD_TO_VALUE(p->yr_bcd) & 0x03) == 0x00))
		{
			p->day_bcd = 0x29;
		}
	}
	return true;
}

/******************************************************************************
** Function:	Get next day, given pointer to a RTC_time type
**
** Notes:		return false if out of range
*/
bool RTC_get_next_day(RTC_type *p)
{
	bool last_day_of_month = false;

	if ((p->day_bcd == 0x00) || (p->day_bcd > 0x31))
	{
		return false;
	}
	if ((p->mth_bcd == 0x00) || (p->mth_bcd > 0x12))
	{
		return false;
	}

	// if not last day of month
	// if not February
	if (p->mth_bcd != 0x02)
	{
		if (p->day_bcd >= rtc_value_to_bcd[rtc_month_days[RTC_BCD_TO_VALUE(p->mth_bcd)]])
		{
			last_day_of_month = true;
		}
	}
	// else is February - check for leap year
	else if ((RTC_BCD_TO_VALUE(p->yr_bcd) & 0x03) == 0x00)
	{
		if (p->day_bcd == 0x29)
		{
			last_day_of_month = true;
		}
	}
	// else is February but not leap year
	else if (p->day_bcd == 0x28)
	{
		last_day_of_month = true;
	}
	if (!last_day_of_month)
	{
		// add 1 day
		p->day_bcd = rtc_value_to_bcd[RTC_BCD_TO_VALUE(p->day_bcd) + 1];
	}
	else
	{
		// need to set first day of next month
		// if not December
		if (p->mth_bcd < 0x12)
		{
			// add 1 month
			p->mth_bcd = rtc_value_to_bcd[RTC_BCD_TO_VALUE(p->mth_bcd) + 1];
		}
		else
		{
			// need to set to January of next year if there is one
			if (p->yr_bcd == 0x99)
			{
				return false;
			}
			p->yr_bcd = rtc_value_to_bcd[RTC_BCD_TO_VALUE(p->yr_bcd) + 1];
			p->mth_bcd = 0x01;
		}
		p->day_bcd = 0x01;
	}
	return true;
}

/******************************************************************************
** Function:	add time
**
** Notes:		adds or subtracts given time in bcd depending on given sign to current time and date
**				returns false if time did not change
*/
bool RTC_add_time(bool plus_sign, uint8 hh_bcd, uint8 mm_bcd, uint8 ss_bcd)
{
	uint32 delta_time_sec, old_time_sec, new_time_bcd;
	RTC_type new_rtc;

	if ((hh_bcd == 0) && (mm_bcd == 0) && (ss_bcd == 0))			// no change
		return false;

	old_time_sec = RTC_time_sec;
	memcpy(&new_rtc, &RTC_now, sizeof(RTC_type));
	
	delta_time_sec = RTC_bcd_time_to_sec(hh_bcd, mm_bcd, ss_bcd);	// get delta time in seconds
	if (plus_sign)													// add time
	{
		delta_time_sec += RTC_time_sec;
		if (delta_time_sec >= RTC_SEC_PER_DAY)						// rollover
		{
			delta_time_sec -= RTC_SEC_PER_DAY;
			RTC_get_next_day(&new_rtc);
			RTC_set_date(new_rtc.day_bcd, new_rtc.mth_bcd, new_rtc.yr_bcd);
		}
	}
	else															// subtract time
	{
		if (delta_time_sec <= RTC_time_sec)
			delta_time_sec = RTC_time_sec - delta_time_sec;
		else
		{
			delta_time_sec = (RTC_time_sec + RTC_SEC_PER_DAY) - delta_time_sec;	// underflow
			RTC_get_previous_day(&new_rtc);
			RTC_set_date(new_rtc.day_bcd, new_rtc.mth_bcd, new_rtc.yr_bcd);
		}
	}

	// set new time
	new_time_bcd = RTC_sec_to_bcd(delta_time_sec);
	new_rtc.sec_bcd = (uint8)(new_time_bcd & 0x000000ff);
	new_time_bcd >>= 8;
	new_rtc.min_bcd = (uint8)(new_time_bcd & 0x000000ff);
	new_time_bcd >>= 8;
	new_rtc.hr_bcd = (uint8)(new_time_bcd & 0x000000ff);

	RTC_set_time(new_rtc.hr_bcd, new_rtc.min_bcd, new_rtc.sec_bcd);

	return true;
}

/******************************************************************************
** Function:	Set time correction
**
** Notes:
*/
void RTC_set_correction(uint8 value)
{
	_ALRMEN = false;						// avoid false alarms - see PIC24 manual sec 29.4

    rtc_unlock();
    RCFGCALbits.RTCEN = false;				// stop the clock
	// load value into lower byte of RFGCAL
	RCFGCALbits.CAL = value;
    RCFGCALbits.RTCEN = true;				// restart the clock
	RCFGCALbits.RTCWREN = false;			// re-lock the RTCC
}

/******************************************************************************
** Function:	Set time
**
** Notes:		Check numeric range of parameters prior to call
*/
void RTC_set_time(uint8 hh_bcd, uint8 mm_bcd, uint8 ss_bcd)
{
	uint32 old_time_sec = RTC_time_sec;		// get time before change
	RTC_type old_rtc;

	memcpy(&old_rtc, &RTC_now, sizeof(RTC_type));

	_ALRMEN = false;						// avoid false alarms - see PIC24 manual sec 29.4

    rtc_unlock();
    RCFGCALbits.RTCEN = false;				// stop the clock
	rtc_read();								// read date into work register

	rtc_work.hr_bcd = hh_bcd;
	rtc_work.min_bcd = mm_bcd;
	rtc_work.sec_bcd = ss_bcd;

	_RTCPTR = 1;							// auto-decrements on write to RTCVAL
	RTCVAL = rtc_work.reg[1];				// WKDYHR
	RTCVAL = rtc_work.reg[0];				// MINSEC

    RCFGCALbits.RTCEN = true;				// restart the clock
	RCFGCALbits.RTCWREN = false;			// re-lock the RTCC

	RTC_get_time_now();						// update RTC_time_sec
	// if date has not changed
	MDM_change_time(old_time_sec, RTC_time_sec);	// update modem use

	LOG_flush();
}

/******************************************************************************
** Function:	Set date
**
** Notes:		Prior to call, check mm <= 12
*/
bool RTC_set_date(uint8 dd_bcd, uint8 mm_bcd, uint8 yy_bcd)
{
	uint16 days;

	_ALRMEN = false;						// avoid false alarms - see PIC24 manual sec 29.4

	// Validate date, and compute day of week:
	// Get days from 01/01/2000 to this date:
	days = rtc_get_days_to_date(dd_bcd, mm_bcd, yy_bcd);
	if (days == 0xffff) return false;		// date error

	// Do not return after this point without re-starting the clock.
    rtc_unlock();
    RCFGCALbits.RTCEN = false;				// stop the clock
	rtc_read();								// read time into work register

	// Add number of days into current month and offset for Sat 01/01/00
	rtc_work.wkd = (days + 6) % 7;
	rtc_work.day_bcd = dd_bcd;
	rtc_work.mth_bcd = mm_bcd;
	rtc_work.yr_bcd = yy_bcd;

	_RTCPTR = 3;							// auto-decrements on write to RTCVAL
	RTCVAL = rtc_work.reg[3];				// YEAR
	RTCVAL = rtc_work.reg[2];				// MTHDY
	RTCVAL = rtc_work.reg[1];				// WKDYHR

    RCFGCALbits.RTCEN = true;				// restart the clock
	RCFGCALbits.RTCWREN = false;			// re-lock the RTCC
	return true;
}

/******************************************************************************
** Function:	Convert time in sec to BCD
**
** Notes:	Input value 0 - 86399. Returns 00HHMMSS
*/
uint32 RTC_sec_to_bcd(uint32 time_sec)
{
	uint16 i;
	uint32 t_bcd;

	// 65536 sec = 18:12:16
	if (HIGH16(time_sec) == 0)					// before 18:12:16
	{
		i = LOW16(time_sec) / 3600;				// hours 0..18
		t_bcd = (uint32)rtc_value_to_bcd[i] << 16;
	}
	else										// 18:12:16 or later
	{
		LOW16(time_sec) += 736;
		i = LOW16(time_sec) / 3600;				// hours minus 18 (0..5)
		t_bcd = (uint32)rtc_value_to_bcd[18 + i] << 16;
	}

	LOW16(time_sec) -= i * 3600;				// min&sec in sec
	i = LOW16(time_sec) / 60;					// min
	t_bcd |= (uint32)rtc_value_to_bcd[i] << 8;
	t_bcd |= rtc_value_to_bcd[LOW16(time_sec) - (i * 60)];

	return t_bcd;
}

/******************************************************************************
** Function:	Set the alarm
**
** Notes:
*/
void RTC_set_alarm(uint32 t)
{
	t = RTC_sec_to_bcd(t);
	rtc_work.hr_bcd = BITS16TO23(t);
	rtc_work.min_bcd = BITS8TO15(t);
	rtc_work.sec_bcd = BITS0TO7(t);

	_ALRMEN = false;							// avoid false alarms
    rtc_unlock();
	_ALRMPTR = 1;								// auto-decrements on write to ALRMVAL
	ALRMVAL = rtc_work.reg[1];					// WKDYHR
	ALRMVAL = rtc_work.reg[0];					// MINSEC
	RCFGCALbits.RTCWREN = false;				// re-lock the RTCC

	ALCFGRPT = _16BIT(_B10011000, _B00000000);	// alarm once per day, no repeat
}

/******************************************************************************
** Purpose:	Get no. of days in month
**
** Returns: days
**
** Notes:
*/
static uint8 rtc_days_in_month(uint8 Month, bool Leap)
{
   // Deal with February first.
   if (Month == 2)
      return(Leap ? 29 : 28);

   // else
   return(rtc_month_days[Month]);
}

/******************************************************************************
** Purpose:	convert year into Julian days
**
** Returns: days from 01/01/2000 to 01/01/20yy
**
** Notes:
*/
static uint16 rtc_year_to_days(uint8 yy)
{
	uint16 d;

	d = 1461 * (uint16)(yy >> 2);
	yy &= 0x03;
	d += 365 * (uint16)yy;
	if (yy != 0)
		d++;

	return(d);
}

/******************************************************************************
** Purpose:	convert Julian days to dd/mm/yy
**
** Returns:
**
** Notes:
*/
void RTC_days_to_date_bcd(uint16 JulianDays, uint8 * p_dd, uint8 * p_mm, uint8 * p_yy)
{
	uint8 d;
	uint16 ds;
	bool LeapYear;

	*p_yy = (uint8)(((uint32)JulianDays << 2) / 1461L);	// year done

	JulianDays -= rtc_year_to_days(*p_yy);		// get no of days into the year
	ds = JulianDays + 1;

	LeapYear = ((*p_yy & 0x03) == 0);
	for (*p_mm = 1; *p_mm < 12; (*p_mm)++)
	{
		d = rtc_days_in_month(*p_mm, LeapYear);
		if (ds <= d)
			break;

		ds -= d;
	}

	*p_yy = RTC_bcd_from_value(*p_yy);
	*p_mm = RTC_bcd_from_value(*p_mm);
	*p_dd = RTC_bcd_from_value((uint8)ds);
}

/******************************************************************************
** Function:	Compare two time stamps
**
** Notes:		returns:
**				0  if equal
**				-1 if x before y
**				+1 if x after y
**
**				does not compare day of week
*/
int RTC_compare(RTC_type * x, RTC_type * y)
{
	// compare dates
	if (x->reg32[1] < y->reg32[1]) return -1;
	if (x->reg32[1] > y->reg32[1]) return 1;
	// else dates are equal
	// compare times ignoring day of week
	if ((x->reg32[0] & 0x00ffffff) < (y->reg32[0] & 0x00ffffff)) return -1;
	if ((x->reg32[0] & 0x00ffffff) > (y->reg32[0] & 0x00ffffff)) return 1;
	// else dates and times are equal
	return 0;
}

/*****************************************************************************
 * Function: RTC_init
 *
 */
 void RTC_init(void)
{
	// check if rtc is still running when we get here after reset
    if (!RCFGCALbits.RTCEN)		// stopped: reset the clock
	{
		// Enable the timer input for RTCC operation:
		asm("mov	#OSCCON,W1");
		asm("mov.b	#0x00, W0");
		asm("mov.b	#0x46, W2");
		asm("mov.b	#0x57, W3");
		asm("mov.b	W2, [W1]");
		asm("mov.b	W3, [W1]");
		asm("mov.b	W0, [W1]");

		RCFGCAL	= 0x0000;
		RTC_set_date(0x01, 0x01, 0x00);
		RTC_set_time(0x00, 0x00, 0x00);
	}

	RTC_get_time_now();
}

/*****************************************************************************
 * EOF
 *****************************************************************************/
