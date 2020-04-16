/******************************************************************************
** File:	Gps.h
**
** Notes:	GPS NMEA receive
*/

/* CHANGES
** V4.00 280214 PB 	Created
**
** V4.04 010514 PB  add config for time of day for gps fix
**                  add gps_wakeup_timer and set timer function for time of day for gps fix
*/

#ifdef HDW_GPS

// Number of forward flow pulses to trigger GPS in truck mode:
#define GPS_TRIGGER_COUNT	4

// gps configuration type
typedef struct
{
	RTC_hhmm_type 	trigger_time;										// set by #GPS
	bool			truck_mode;
} GPS_config_type;

extern bool GPS_is_on;
extern GPS_config_type GPS_config;
extern uint32 GPS_wakeup_time;
extern FAR char GPS_time[16];
extern FAR char GPS_latitude[16];
extern FAR char GPS_NS[2];
extern FAR char GPS_longitude[16];
extern FAR char GPS_EW[2];
extern FAR char GPS_fix[2];

void GPS_recalc_wakeup(void);
bool GPS_can_sleep(void);
void GPS_on(void);
void GPS_off(void);
void GPS_task(void);
void GPS_init(void);
void GPS_clear_fix(void);

#endif

