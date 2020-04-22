/******************************************************************************
** File:	Cfs.h	
**
** Notes:	Custom File System
** Allows real-time update of values reported to host via Mass Storage interface,
** clock-setting, and simultaneous logging to SD card while USB host connected.
*/

/* CHANGES
** v2.36 271010 PB  add CFS_find_youngest_file() prototype
**
** v2.45 130111 PB add CFS_activity_path, CFS_sigres_name, CFS_gsmres_name, CFS_networks_name strings
**
** v2.51 090211 PB CFS_gsmres_name changed to CFS_nwres_name "NWRES.TXT"
**
** v2.59 010311 PB DEL127 add CFS_network_name "NETWORK.TXT"
**
** V2.65 070411 PB  add CFS_details_name
**
** V2.74 250511 PB  DEL143 - add CFS_nitz_logon_name
**
** V2.90 100811 PB  add CFS_get_cfg_name, CFS_get_nv_name, CFS_current_name, CFS_nv_name
**
** V2.99 180112 PB  add CFS_eco_name, CFS_cal_path, and CFS_build_name
**
** V3.00 250911 PB  Waste water - add CFS_control_name, CFS_units_name
**
** V3.03 161111 PB  Waste water - add names for derived data calc files
**
** V3.08 280212 PB  add CFS_eco_name, CFS_cal_path, and CFS_build_name
**
** V3.17 091012 PB  bring up to date with Xilog+ V3.06 - CFS_open() returns a char
**
** V4.02 140414 PB  add cfs_state CFS_FAILED for catastrophic failure of CFS, mostly when opening
**
** V3.26 170613 PB  bring up to date with Xilog+ V3.04(new)
**					Remove CFS_FAILED_TO_OPEN state 
**					add SCRIPTS path and script file names
**
** V4.06		MA	File system modifications for faster creation of new files and avoidance of file system corruption
**
** V4.11 270814 PB  Add CFS_gps_name "GPS.TXT"
*/

#ifdef ECLIPSE
#include "FSDefs.h"
#elif
#include "MDD File System\FSDefs.h"
#endif

// Filename 8.3 + '\0'
#define CFS_FILE_NAME_SIZE	13
#define CFS_PATH_NAME_SIZE	10

// States
#define CFS_OFF				0
#define CFS_POWERING		1
#define CFS_INITIALISING	2
#define CFS_OPENING			3
#define CFS_OPEN			4
#define CFS_FAILED			5

extern uint8 CFS_state;
extern uint8 CFS_first_assert;
extern uint8 CFS_last_assert;
extern uint8 CFS_init_counter;
extern uint8 CFS_open_counter;

extern int CFS_timer_x20ms;

extern const char CFS_config_path[]
#ifdef extern
= "\\CONFIG"
#endif
;

extern const char CFS_activity_path[]
#ifdef extern
= "\\ACTIVITY"
#endif
;

extern const char CFS_cal_path[]
#ifdef extern
= "\\SYSTEM\\CAL"
#endif
;

extern const char CFS_scripts_path[]
#ifdef extern
= "\\SCRIPTS"
#endif
;

extern const char CFS_build_name[]
#ifdef extern
= "BUILD.CAL"
#endif
;

extern const char CFS_sigres_name[]
#ifdef extern
= "SIGRES.TXT"
#endif
;

extern const char CFS_nwres_name[]
#ifdef extern
= "NWRES.TXT"
#endif
;

extern const char CFS_network_name[]
#ifdef extern
= "NETWORK.TXT"
#endif
;

extern const char CFS_networks_name[]
#ifdef extern
= "NETWORKS.TXT"
#endif
;

extern const char CFS_details_name[]
#ifdef extern
= "DETAILS.TXT"
#endif
;

extern const char CFS_nitz_logon_name[]
#ifdef extern
= "NTZLOGON.TXT"
#endif
;

extern const char CFS_get_cfg_name[]
#ifdef extern
= "GETCFG.HCS"
#endif
;

extern const char CFS_get_nv_name[]
#ifdef extern
= "GETNV.HCS"
#endif
;

extern const char CFS_current_name[]
#ifdef extern
= "CURRENT.HCS"
#endif
;

extern const char CFS_nv_name[]
#ifdef extern
= "NV.HCS"
#endif
;

extern const char CFS_control_name[]
#ifdef extern
= "CONTROL.TXT"
#endif
;

extern const char CFS_units_name[]
#ifdef extern
= "UNITS.TXT"
#endif
;

extern const char CFS_volunits_name[]
#ifdef extern
= "VOLUNITS.TXT"
#endif
;

extern const char CFS_ftable_name[]
#ifdef extern
= "FTABLE.CAL"
#endif
;

extern const char CFS_vnotch_name[]
#ifdef extern
= "VNOTCH.CAL"
#endif
;

extern const char CFS_venturi_name[]
#ifdef extern
= "VENTURI.CAL"
#endif
;

extern const char CFS_rect_name[]
#ifdef extern
= "RECT.CAL"
#endif
;

extern const char CFS_apipe_name[]
#ifdef extern
= "APIPE.CAL"
#endif
;

extern const char CFS_eco_name[]
#ifdef extern
= "ECO.TXT"
#endif
;

extern const char CFS_gps_name[]
#ifdef extern
= "GPS.TXT"
#endif
;

bool CFS_file_exists(char * path, char * filename);
bool CFS_open(void);
void CFS_power_down(void);
void CFS_init(void);
void CFS_task(void);
MEDIA_INFORMATION * CFS_sd_card_ready(void);

void CFS_close_file(FSFILE *f);
bool CFS_read_file(char * path, char * filename, char * buffer, int max_bytes);
bool CFS_read_block(char * path, char * filename, char * buffer, long seek_pos, int bytes);
int CFS_read_line(char * path, char * filename, int n, char * buffer, int max_bytes);
bool CFS_write_file(char * path, char * filename, char * mode, char * buffer, int n_bytes);

int CFS_find_youngest_file(char * path, char * result);
int CFS_find_oldest_file(char * path, char * result);
bool CFS_purge_oldest_file(char * path);
int CFS_search_filesize(void);


