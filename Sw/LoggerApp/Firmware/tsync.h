/******************************************************************************
** File:	tsync.h - time synchronisation header file	
**
** Notes:	
*/

#define TSYNC_NO_PROTOCOL	0
#define TSYNC_GSM			1
#define TSYNC_GPRS_NITZ		2
//#define TSYNC_GPRS_RFC867	3
#define TSYNC_MAX_PROTOCOL	3

extern bool     TSYNC_on;
extern bool     TSYNC_use_mins_secs;
extern bool		TSYNC_offset_negative;

extern uint8	TSYNC_offset_hh;	// BCD
extern uint8	TSYNC_offset_mm;	// BCD

extern uint8    TSYNC_threshold;

void   TSYNC_set_interval(uint16 interval);
uint16 TSYNC_get_interval(void);
void   TSYNC_set_remaining(uint16 remaining);
uint16 TSYNC_get_remaining(void);
void   TSYNC_action(void);
void   TSYNC_csmp_parameters(char * string_buffer);
uint8  TSYNC_pdu_parameter(void);
void   TSYNC_parse_id(char * in_buffer);
void   TSYNC_parse_status(char * in_buffer);
void   TSYNC_parse_nitz_reply(char * in_buffer);
void   TSYNC_format_status(char * str_ptr);
void   TSYNC_task(void);
void   TSYNC_init(void);
void   TSYNC_change_clock(void);
bool   TSYNC_set_offset(bool plus, uint8 hh_bcd, uint8 mm_bcd);

