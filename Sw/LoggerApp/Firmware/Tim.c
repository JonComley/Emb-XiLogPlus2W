/******************************************************************************
** File:	Tim.c
**
** Notes:	Timer functions
**			T1 = 20ms tick
**			T5 = function timing, for debug only
*/

#include "Custom.h"
#include "Compiler.h"
#include "Sns.h"
#include "HardwareProfile.h"

#define extern
#include "Tim.h"
#undef extern

uint16 tim_20ms_timer;

/******************************************************************************
** Function:	Initialise Timer 1 for 20ms tick
**
** Notes:	
*/
void TIM_init(void)
{
	// 32768Hz external clock, 8x prescale (hence 4096Hz), stop when idle
	T1CON = _16BIT(_B10100000, _B00010010);
	
	PR1 = 0xFFFF;	// free running

	TIM_START_TIMER(tim_20ms_timer);
	TIM_20ms_tick = false;
}

/******************************************************************************
** Function:	Start debug timer
**
** Notes:		Use macro TIM_STOP_DEBUG_TIMER() to stop it, & look at TMR5
*/
void TIM_start_debug_timer(void)
{
//	HDW_TEST_LED = true;

	//if (T5CONbits.TON)			// already running
	//	return;
	// else:

	T5CONbits.TON = false;
	TMR5 = 0;
	PR5 = 0xFFFF;

	T4CONbits.T32 = false;		// T4/T5 each 16-bit
	// 32768Hz external clock, 8x prescale, stop when idle
	T5CON = _16BIT(_B10100000, _B00010010);
}

/******************************************************************************
** Function:	Timer task
**
** Notes:		Set / clear TIM_20ms_tick for 1 pass through mainloop
** Interval between ticks guaranteed to be at least 20ms.
*/
void TIM_task(void)
{
	TIM_20ms_tick = false;
	if (TIM_TIMER_EXPIRED(tim_20ms_timer, 20))
	{
		tim_20ms_timer = TMR1;
		TIM_20ms_tick = true;
	}
}

/******************************************************************************
** Function:	Delay ms
**
** Notes:		Synchronous. 
*/
void TIM_delay_ms(uint16 n)
{
	uint16 start_time;

	TIM_START_TIMER(start_time);
	while (!TIM_TIMER_EXPIRED(start_time, n));
}

