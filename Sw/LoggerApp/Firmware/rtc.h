/*****************************************************************************
 *
 * Real Time Clock Calender
 *
 *****************************************************************************
 * FileName:        rtc.h
 */

#define RTC_SEC_PER_DAY		86400L

// Union to access rtcc registers
typedef union
{
	struct
	{
		unsigned char sec_bcd;
		unsigned char min_bcd;
		unsigned char hr_bcd;
		unsigned char wkd;			// 0 = Sunday, 6 = Saturday
		unsigned char day_bcd;
		unsigned char mth_bcd;
		unsigned char yr_bcd;
	};
	uint16 reg[4];
	uint32 reg32[2];
} RTC_type;

// Compressed version for start/stop times and dates:
typedef struct
{
	unsigned char min_bcd;
	unsigned char hr_bcd;
	unsigned char day_bcd;
	unsigned char mth_bcd;
	unsigned char yr_bcd;
} RTC_start_stop_type;

// BCD HH:MM:SS
typedef struct
{
	unsigned char sec_bcd;
	unsigned char min_bcd;
	unsigned char hr_bcd;
	unsigned char padding;
} RTC_hhmmss_type;

// BCD HH:MM
typedef union
{
	struct
	{
		unsigned char min_bcd;
		unsigned char hr_bcd;
	};
	uint16 word;
} RTC_hhmm_type;

extern uint32 RTC_time_sec;
extern uint8 RTC_half_sec;

extern RTC_type RTC_now;

uint8 RTC_bcd_from_value(uint8 value);
void RTC_get_time_now(void);
bool RTC_hhmm_past(RTC_hhmm_type *p);
bool RTC_get_previous_day(RTC_type *p);
bool RTC_get_next_day(RTC_type *p);
bool RTC_start_stop_today(RTC_start_stop_type *p);
bool RTC_start_stop_event(RTC_start_stop_type *p);
void RTC_set_start_stop_now(RTC_start_stop_type *p);
uint16 RTC_bcd_to_min(uint8 hh, uint8 mm);
uint32 RTC_bcd_time_to_sec(uint8 hh, uint8 mm, uint8 ss);
uint32 RTC_time_date_to_sec(RTC_type *p);
uint32 RTC_sec_to_bcd(uint32 time_sec);
void RTC_set_correction(uint8 value);
void RTC_set_time(uint8 hh_bcd, uint8 mm_bcd, uint8 ss_bcd);
bool RTC_set_date(uint8 dd_bcd, uint8 mm_bcd, uint8 yy_bcd);
bool RTC_add_time(bool sign, uint8 hh_bcd, uint8 mm_bcd, uint8 ss_bcd);
void RTC_init(void);
void RTC_set_alarm(uint32 time_sec);
int RTC_compare(RTC_type * x, RTC_type * y);
void RTC_days_to_date_bcd(uint16 JulianDays, uint8 * p_dd, uint8 * p_mm, uint8 * p_yy);


/*****************************************************************************
 * EOF
 *****************************************************************************/
