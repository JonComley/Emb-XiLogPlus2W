/******************************************************************************
** File:	Slp.h
**
** Notes:
**
** V2.65 070411 PB  added conditional compilation ifdef HDW_PRIMELOG_PLUS for size of SLP_NUM_WAKEUP_SOURCES
**
** V3.00 260911 PB  Waste Water - added 1 to number of wakeup sources
**
** V3.08 280212 PB  ECO product - add pwr wakeup time 
**
** V4.00 220114 PB  if HDW_GPS disable all analogue calls and functions
**
** V4.04 010514 PB	add a wakeup source for GPS
*/

#include "HardwareProfile.h"	// essential for 3ch/9ch selection

// Set alarm time to max long int to disable wakeup:
#define SLP_NO_WAKEUP	0xFFFFFFFFL


#ifndef extern
extern uint32 * const SLP_wakeup_times[];
extern const uint8 SLP_num_wakeup_sources;
#endif

void SLP_task(void);
void SLP_set_required_clock_speed(void);
uint32 SLP_get_system_clock(void);

