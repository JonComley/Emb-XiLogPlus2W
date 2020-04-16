/******************************************************************************
** File:	Tim.c
**
** Notes:	Timer functions
**
*/

// Generic timer macros: NB START must be a uint16. TMR1 free-running at 4096Hz
#define TIM_START_TIMER(START)				START = TMR1				
#define TIM_TIMER_EXPIRED(START, N_MS)		((TMR1 - (START)) > (uint16)((N_MS) << 2))

// Debug timer: look at value in TMR5
#define TIM_START_DEBUG_TIMER()	TIM_start_debug_timer()
#define TIM_STOP_DEBUG_TIMER()	T5CONbits.TON = false;

extern bool TIM_20ms_tick;

void TIM_init(void);
void TIM_task(void);
void TIM_delay_ms(uint16 n);
void TIM_start_debug_timer(void);

