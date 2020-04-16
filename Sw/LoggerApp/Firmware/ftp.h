/******************************************************************************
** File:	ftp.h
**			P Boyce
**			9/7/09
**
** Notes:	FTP message coding
**
** V3.36 140114 PB  remove FTP_deactivate_retrieval_info()
*/

// Configuration of ftp file retrieval - stuff to be remembered between message creations

																				// ftp retrieve indices
#define FTP_BASE_FTPR_INDEX			0
#define FTP_DERIVED_BASE_INDEX		11
#define FTP_NUM_FTPR_CHANNELS		22
#ifndef HDW_RS485
#define FTP_DIG_FTPR_INDEX			0
#define FTP_DERIVED_DIG_INDEX		11
#else
#define FTP_SER_VELOCITY_INDEX  	0
#define FTP_SER_TEMPERATURE_INDEX  	1
#define FTP_SER_DEPTH_INDEX  		2
#define FTP_SER_DERIVED_FLOW_INDEX 	11
#endif
#define FTP_ANA_FTPR_INDEX			9
#define FTP_NUM_CHANNELS			11
#define FTP_DERIVED_ANA_INDEX		15

typedef struct
{
	uint8	 flags;																// bit 0 - channel is currently active 
																				// bit 1 - channel has data to be transmitted
	RTC_type seek_time_stamp;													// timestamp of next item to be transmitted
	long  	 seek_pos;															// byte count into file of next item to be transmitted
} FTP_file_retrieve_type;

extern FAR FTP_file_retrieve_type FTP_channel_data_retrieve[FTP_NUM_FTPR_CHANNELS];
extern FAR char 				  FTP_path_str[32];
extern FAR char 				  FTP_filename_str[32];

extern const char FTP_logon_filename[]
#ifdef extern
= "ftplogon.txt"
#endif
;

int    FTP_encrypt_password(char * password_p);
bool   FTP_get_logon_string(char * logon_string);
void   FTP_set_filename_and_path(uint8 channel, RTC_type * date_p);
long   FTP_find_file(uint8 channel, RTC_type * date_p);
uint16 FTP_flag_normal_files_present(RTC_type * date);
uint16 FTP_flag_derived_files_present(RTC_type * date);
uint8  FTP_frd_send(char *path, char *filename);
void   FTP_update_retrieval_info(uint8 channel, RTC_type * time_stamp);
void   FTP_activate_retrieval_info(uint8 channel);
void   FTP_reset_active_retrieval_info(void);
void   FTP_retrieve_data(uint16 file_flags, RTC_type * file_date_p);
void   FTP_act_on_ftp_command(void);
uint8  FTP_set_logon(void);
void   FTP_schedule(void);
bool   FTP_busy(void);
void   FTP_task(void);
void   FTP_init(void);
