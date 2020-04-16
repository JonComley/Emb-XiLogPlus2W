/******************************************************************************
** File:	Com.h
**
** Notes:	
**
** V2.45 130111 PB  Add parameters for network test	
**
** V2.51 040211 PB  Change GSMxxto NWxxx
**       090211 PB  New function COM_test_in_progress
**
** V2.56 240211 PB  New function COM_schedule_check_dflags		
**
** V2.65 070411 PB  removed COM_site_id[] - now comes from details.txt file in config dir
**
** V.274 250511 PB  DEL143 - new function prototype COM_initiate_gprs_nitz_tsync()
**
** V2.90 010811 PB  DEL142 - COM_initiate_tx() given a bool input parameter to select SMS or FTP 
**
** V3.00 220911 PB  Waste Water - make com_first_window_time and com_next_window_time global
**
** V3.04 050112 PB  Waste Water - add extra standby window and period
**
** V3.17 091012 PB  Bring up to date with Xilog+ V3.06 - new fn COM_long_interval()
*/

// Put this value into COM_reset_logger and logger will reset
#define COM_RESET_LOGGER_KEY	0xA9

#define com_ftp_transfer_file_pending	COM_flags_1.b0
#define com_report_pending				COM_flags_1.b1
#define com_pending_ftp_poll			COM_flags_1.b2
#define com_pending_ftp_tx				COM_flags_1.b3
#define com_pending_sms_rx				COM_flags_1.b4
#define com_pending_sms_tx				COM_flags_1.b5
#define COM_mdm_stdby_required			COM_flags_1.b6
#define com_sms_rx_second_shot			COM_flags_1.b7
#define com_gprs_fail					COM_flags_1.b8
#define com_pending_nw_test				COM_flags_1.b9
#define com_pending_abort				COM_flags_1.b10
#define com_pending_check_dflags		COM_flags_1.b11
#define com_pending_tsync				COM_flags_1.b12
#define com_ftp_slow_server				COM_flags_1.b13

// Modem window for Tx, Rx or standby:
typedef struct
{
	RTC_hhmm_type start;
	RTC_hhmm_type stop;
	uint8 day_mask;
	uint8 interval;
} COM_window_type;

// Configuration of modem power schedule
typedef struct
{
	bool ftp_enable;
	bool batch_enable;
	bool tx_oldest_first;
	uint8 _1fm_standby_mins; 
	COM_window_type tx_window;
	COM_window_type rx_window;
	COM_window_type modem_standby;			// interval used for FTP poll
} COM_schedule_type;

extern BITFIELD COM_flags_1;
extern COM_schedule_type COM_schedule;

extern uint8 COM_commissioning_mode;
extern uint8 COM_reset_logger;
extern uint8 COM_sign_on_status;

extern int COM_csq;
extern uint32 COM_wakeup_time;
extern bool COM_roaming_enabled;
extern uint32 COM_gsm_network_id;

extern char COM_output_buffer[162];
extern char COM_sitename[32];

extern FAR char COM_ftp_filename[32];

// telephone numbers
extern FAR char COM_sms_sender_number[MSG_PHONE_NUMBER_LENGTH];
extern char COM_host1[MSG_PHONE_NUMBER_LENGTH];
extern char COM_host2[MSG_PHONE_NUMBER_LENGTH];
extern char COM_host3[MSG_PHONE_NUMBER_LENGTH];
extern char COM_alarm1[MSG_PHONE_NUMBER_LENGTH];
extern char COM_alarm2[MSG_PHONE_NUMBER_LENGTH];

// signal and network test parameters
extern uint16 COM_sigtst_delay;
extern uint8  COM_sigtst_interval;
extern uint8  COM_sigtst_samples;
extern uint8  COM_sigtst_count;
extern char   COM_nwtest_progress;
extern uint16 COM_nwtst_delay;

void COM_reset(void);
bool COM_long_interval(void);
bool COM_within_window(COM_window_type *p);
uint32 COM_next_window_time(COM_window_type *p);
uint32 COM_first_window_time(COM_window_type *p);
bool COM_test_in_progress(void);
void COM_ftp_transfer_file(char * path, char * filename);
void COM_recalc_wakeups(void);
void COM_schedule_control(void);
void COM_set_wakeup_time(void);
void COM_initiate_tx(bool sms);
void COM_initiate_nw_test(void);
void COM_initiate_gprs_nitz_tsync(void);
void COM_schedule_check_dflags(void);
void COM_cancel_com_mode(void);
bool COM_can_sleep(void);
void COM_task(void);
void COM_init(void);

