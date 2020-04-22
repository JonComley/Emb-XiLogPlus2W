/******************************************************************************
** File:	Cmd.c
**
** Notes:	Execute commands from USB interface, a script file, SMS or e-mail
**
*/

/* CHANGES
** v2.36 261010 PB 	In #AT command, allow command to be txed to modem if modem is not OFF 
**
** v2.38 031110 PB  Add #CALM command to control commssioning alarm
**
** v2.39 231110 MA  Modification to #ABT command
**
** V2.40 061210 MA	Character '\' or '/' may now be used to specify a path. On SMS replies only,
**					character '/' is used to report paths. (DEL90)
**					File contents now reported on #FRD reply in SMS mode.	
**
** V2.45 130111 PB  Add #GSMTST and #GSMRES for network test			
**
** V2.49 240111 PB  DEL112 - Corrections to #SIGRES and #GSMRES to prevent duplication in result string		
**					DEL109 - ensure seconds are set correctly in CMD_parse_rsd	
**							 CMD_parse_rsd must cope with both hh:mm and hh:mm:ss time format
**       250111 PB  Resynchronise batch counters in #dt and #tc if time of day changes 
**
** V2.50 030211 PB  DEL117 - simplified batch synchronisation - done when enqueue a dig or ana value
**                  DEL119 - add timestamp to GSMTST results file
**
** V2.51 040211 PB  Add #ABORT command
**                  #GSMxxx changed to #NWxxx
**                  changes to initial format of nwtst.txt
**                  reply to #NWRES now includes a status character before contents of results file 
**       090211 PB  Call new function COM_test_in_progress and return new ERROR 24 if necc		
**
** V2.52 150211 PB  DEL 122 - Correction to #ftpc to content of dummy ftplogon files
**                  Return MODEM_OFF error for #AT if CAL_build_info.modem flag is false or CM mode 2
**                  Return MODEM_OFF error for #CM=1 or #CMON if CAL_build_info.modem flag is false
**					Add dABORT reply to #abort command
**
** V2.55 230211 PB  DEL77 fixed
**
** v2.59 010311 PB  DEL63  #EC calls new function DIG_insert_event_headers
**       020311     DEL124 change #fas,#fap,#fws,#fwr flags to CMD_NON_VOLATILE
**
** v2.64 170311 PB  AGBAR requirements - add optional second parameter site ID string to #NAME
**       220311 PB  Call FRM_init() after CAL_read_build_info in #LI
**       230311 PB  Changes to #TSYNC for new format
**
** V2.65 070411 PB  added conditional compilation ifndef HDW_PRIMELOG_PLUS in cmd_at()
**                  removed setting COM_site_id[] in #name - now comes from details.txt file in config dir
**                  extra field in #LI reply if hardware/software mismatch when HDW_PRIMELOG_PLUS defined
**
** V2.77 020611 PB  DEL148 - in cmd_set_totaliser, add fractional part to INTEGER part of totaliser, not ALL of totaliser 
**
** V2.90 110811 PB  add new command #TSU - Transmit SetUp - to send setup information to ftp or sms destination
**                  use CFS_get_cfg_name, CFS_get_nv_name, CFS_current_name, CFS_nv_name
**
** V2.91 121011 PB  DEL155 - add flag bit CMD_TSU to cmd_dirty_flags to indicate creation of new nv.hcs and current.hcs was triggered by #TSU
**							 separate out transmission part of cmd_tsu() into cmd_tsu_tx() so it can be called when script files are completed
**							 in cmd_done() if dirty flags are cleared at end of script if CMD_TSU is set call cmd_tsu_tx() and clear flag
**							 add new string cmd_tsu_destination[32] for dedicated use by #TSU to keep tx destination intact until needed
**							 CMD_check_dirty_flags now returns bool for flags set or cleared
**
** V3.00 210911 PB  new commands for Waste Water Product:
**                  #SCO1, #SCO2
**					#TRO1, #TRO2
**		 220911 	call COP_recalc_wakeups() when time of day changes
**					#RCO1, #RCO2
**					#ACO1, #ACO2
**
** V3.01 041011		#CEC1A - #CEC2B
**
** V3.02 171011		#PFR
**
** V3.03 161111		#DFL
**		 201111		#RDA3 & #RDA4, #IDV
**		 251111		event gate format changed on #ACO and #RCO, high nibble 0 or 1, low nibble 0 to 4 - limit check added
**		
** V3.04 171211		nivus RS485 sensor test commands
**					#nvr, #nvw, #nvv, #nvm, #nvi, #nvh, #nvb, #nvt
**		 181211		doppler sensor logging configuration commands
**					#dsc, #dsv, #dst, #dsf
**       050112     extension of #nvi and #mps commands
**
** V3.05 120112 PB  Combined Xilog+ V 2.98 and WW V3.04
**
** V3.06 120112 PB  new commands for event trigger of control outputs:
**					#ECO1, #ECO2
**
** V3.08 230212 PB  new command #DSD doppler depth setup
**					ECO - add field to #LI reply to indicate E or N for ECO or not
**					followed by field for number of control outputs
**
** V3.09 270312 PB  add v2.91 mods to cmd_tsu as above
**					change cmd_bv reply, three fields and new contents
**
** V3.14 130612 PB  add call to ANA_retrieve_derived_conversion_data() in #ADD to read conversion data into RAM 
**
** V3.15 190612 PB  add #DBG command to control debug LED on control output 1
**
** V3.16 210612 PB  add call to DOP_retrieve_derived_conversion_data() in #DSF to read conversion data into RAM 
**
** V3.17 091012 PB  bring up to date with Xilog+ V3.06 - cmd_acal() must subtract user offset before zeroing
**					use return value of CFS_open() in cmd_do(), cmd_file_write(), cmd_nwtst(), CMD_check_dirty_flags()
**					keep file system open in cmd_ftx(), cmd_rsd()
**
** V3.21 		MA	DEL187 et al - using %u instead of %d for printf of unsigned variables
**
** V3.22 280513 PB  DEL174 - call DIG_insert_headers() in #CEC command to ensure headers inserted on event config changes
**					DEL185 - limit minutes to < 60 in #PRT command
**		 290513 PB  DEL183 - allow #ACO=0,00 to disable action without failing on channel number out of range
**
** V3.22 310513 PB  call ANA_insert_derived_headers() in #ADD command to ensure headers inserted on derived config changes
**					call DIG_configure_channel() from DCCx and CECDxx to set headers and start/stop logging
**					if #PRT successful in setting new time - set pump time switched on to time now
**
** V3.26 170613 PB	Adjustment to cmd_done() for end of script file handling
**					Add volatile command time of day alarm config #tod
**					Convert CMD_byte_mask[] to CMD_word_mask[] for general use for creating 8 or 16 bit mask from n
**
** V3.27 250613 PB	DEL194: add new error code 27 CMD_ERR_NIVUS_BUSY cmd.c: cmd.h:
**					DEL195: always call DOP_configure_sensor() for any doppler set up command - cmd.c:
**
** V3.29 171013 PB  bring up to date with Xilog+ V3.05
**					cmd_bv uses new PWR variable for battery voltage when failed
**					cmd_imv and cmd_rda use new function PWR_set_pending_batt_test()
**					use PWR_measure_in_progress() in CMD_task()
**
** V3.31 131113 PB  correction to use lower case string in STR_string_match() in cmd_done()
**					revised/added check on CFS_open() == CFS_OPEN in cmd_frd() and cmd_frl()
**
** V3.35 090114 PB  add FTP_reset_active_retrieval_info() to cmd_mps
**
** V3.36 140114 PB  call FTP_reset_active_retrieval_info(), then synchronise logging channels, in cmd_dt, cmd_tc and cmd_mps
**
** V4.00 220114 PB  if HDW_GPS disable all analogue calls and functions
**					add new command #GPS
**
** V4.04 010514 PB  add hh:mm time of day set and display to #GPS command
**				 	call GPS_recalc_wakeup() when time of day changes
**
** V4.05 070514 PB	do not allow #GPS=1 in freight mode
**
** V4.11 260814 PB  monitor DOP_sensor_present when #RDA5 or #ISV acted on, and do not insert data if false
**
** V4.12 221014 PB  RS485 #alms1\2\3 and #almsd1 to be recognised by cmd_report_alm_channel_ok()
**					cmd_execute() to recognise #alms1\2\3
**
** V5.00 231014 PB  new undocumented command #rhr - read hardware revision bits - return value of bits set with pullups
**					add hardware/software mismatch tests to cmd_li(), returning new CMD_ERR_INCOMPATIBLE_HARDWARE
**					field 12 of cmd_li() reply - 'S' for RS485 and 'G' for GPS versions
*/

#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <float.h>

#include "Custom.h"
#include "Compiler.h"
#include "MDD File System/SD-SPI.h"
#include "MDD File System/FSIO.h"
#include "HardwareProfile.h"
#include "rtc.h"
#include "Version.h"
#include "Cfs.h"
#include "Msg.h"
#include "Str.h"
#include "tsync.h"
#include "Com.h"
#include "Mdm.h"
#include "Scf.h"
#include "Log.h"
#include "Sns.h"
#include "Cal.h"
#include "Dig.h"
#include "usb.h"
#include "Pdu.h"
#include "ftp.h"
#include "Ana.h"
#include "alm.h"
#include "Pwr.h"
#include "Tim.h"
#include "Cop.h"
#include "Ser.h"
#include "Dop.h"
#include "Frm.h"
#include "gps.h"
#include "modbus.h"
#include "Ser.h"

#define extern
#include "Cmd.h"
#undef extern

//#define CMD_BCD_TO_VALUE(X)	((10 * ((X) >> 4)) + ((X) & 0x0F))

// Parser states:
#define CMD_IDLE				0
#define CMD_EXECUTE_AT_COMMAND	1
#define CMD_READ_LI_INFO		2
#define CMD_READ_SNS_COUNTERS	3
#define CMD_READ_ADV			4
#define CMD_IDV_POWER			5
#define CMD_IDV_ANA				6		// must follow CMD_IDV_POWER
#define CMD_IMV_POWER			7
#define CMD_IMV_ANA				8		// must follow CMD_IMV_POWER
#define CMD_RDA_POWER			9
#define CMD_RDA_ANA				10		// must follow CMD_RDA_POWER
#define CMD_AUTOCAL				11
#define CMD_NIVUS				12

const char * const cmd_day_of_week[7] =
{
	"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

const uint16 CMD_word_mask[16] =
{
	0x0001, 0x0002, 0x0004, 0x0008, 0x0010, 0x0020, 0x0040, 0x0080,
	0x0100, 0x0200, 0x0400, 0x0800, 0x1000, 0x2000, 0x4000, 0x8000
};

const char cmd_test_string[] = "This is a test message.";

// Vars:
bool cmd_equals;

char *cmd_input_ptr;
char *cmd_end_ptr;			// command can end with '\r', '\n' or '\0'
const char *cmd_command_string;
char *cmd_filename_ptr;

uint8 cmd_error_code;
uint8 cmd_channel_index;
uint8 cmd_source_mask;
uint8 cmd_source_index;
uint8 cmd_dirty_flags;
uint8 cmd_config_flags;

int cmd_20ms_timer;
int cmd_byte_count;

uint16 cmd_state;
uint16 cmd_address;

char * cmd_out_ptr;

FAR char cmd_destination[32];
FAR char cmd_tsu_destination[32];
FAR char cmd_path[80];

char *cmd_input[CMD_NUM_SOURCES];
char *cmd_output[CMD_NUM_SOURCES];

SearchRec cmd_srch;

extern DISK gDiskData;

#pragma region Command Table

typedef struct
{
	const char *cmd;
	void (*function)(void);
	uint8 config_flags;
} cmd_action_type;

// config flag values
#define CMD_NON_CFG				_B00000000
#define CMD_VOLATILE			_B00000001
#define CMD_NON_VOLATILE		_B00000010
#define CMD_NO_ACTIVITY_LOG		_B00000100
#define CMD_TSU					_B10000000

// Command function prototypes:
void cmd_abort(void);
void cmd_abt(void);
void cmd_acal(void);
void cmd_acc(void);
void cmd_aco(void);
void cmd_add(void);
void cmd_adv(void);
void cmd_alm(void);
void cmd_aeh1(void);
void cmd_aeh2(void);
void cmd_aeh3(void);
void cmd_aeh4(void);
void cmd_ael1(void);
void cmd_ael2(void);
void cmd_ael3(void);
void cmd_ael4(void);
void cmd_ap1(void);
void cmd_ap2(void);
void cmd_ap3(void);
void cmd_ap4(void);
void cmd_at(void);
void cmd_bv(void);
void cmd_calm(void);
void cmd_cec(void);
void cmd_cfi(void);
void cmd_clrb(void);
void cmd_cmoff(void);
void cmd_cmon(void);
void cmd_cm(void);
void cmd_dbg(void);
void cmd_dcc(void);
void cmd_ddac(void);
void cmd_diag(void);
void cmd_dir(void);
void cmd_do(void);
void cmd_dpc(void);
void cmd_dsc(void);
void cmd_dsd(void);
void cmd_dsf(void);
void cmd_dst(void);
void cmd_dsv(void);
void cmd_dt(void);
void cmd_ecd(void);
void cmd_echo(void);
void cmd_eco(void);
void cmd_fap(void);
void cmd_fas(void);
void cmd_fdel(void);
void cmd_frd(void);
void cmd_frl(void);
void cmd_fsh(void);
void cmd_ftpc(void);
void cmd_ftx(void);
void cmd_fwr(void);
void cmd_fws(void);
void cmd_gps(void);
void cmd_idv(void);
void cmd_imv(void);
void cmd_isv(void);
void cmd_li(void);
void cmd_log(void);
void cmd_mkdir(void);
void cmd_mps(void);
void cmd_msg(void);
void cmd_name(void);
void cmd_nvb(void);
void cmd_nvh(void);
void cmd_nvi(void);
void cmd_nvm(void);
void cmd_nvr(void);
void cmd_nvt(void);
void cmd_nvv(void);
void cmd_nvw(void);
void cmd_nwres(void);
void cmd_nwtst(void);
void cmd_pfr(void);
void cmd_prt(void);
void cmd_ramr(void);
void cmd_ramw(void);
void cmd_rco(void);
void cmd_rda(void);
void cmd_reset(void);
void cmd_rhr(void);
void cmd_rmdir(void);
void cmd_roam(void);
void cmd_rfd(void);
void cmd_rsd(void);
void cmd_sco(void);
void cmd_setb(void);
void cmd_sfp(void);
void cmd_sigres(void);
void cmd_sigtst(void);
void cmd_smsc(void);
void cmd_tc(void);
void cmd_tod(void);
void cmd_tot(void);
void cmd_tro(void);
void cmd_tstat(void);
void cmd_tsu(void);
void cmd_tsync(void);
void cmd_test(void);	// test commands
void cmd_frmw(void);
void cmd_frmr(void);
void cmd_mod(void);

// third parameter is a flag to indicate whether command alters configuration:
const cmd_action_type cmd_action_table[] =
{
	{ "abort",	cmd_abort,	CMD_NON_CFG							},	// abort a test
	{ "abt",	cmd_abt,	CMD_VOLATILE						},	// analogue boost time
	{ "acal",	cmd_acal,	CMD_VOLATILE						},	// autocal analogue channel
	{ "acc",	cmd_acc,	CMD_VOLATILE						},	// configure analogue channel
	{ "aco",	cmd_aco,	CMD_VOLATILE						},	// automatic control output
	{ "add",	cmd_add,	CMD_VOLATILE						},	// configure analog channel for depth to flow derived data
	{ "adv",	cmd_adv,	CMD_NON_CFG | CMD_NO_ACTIVITY_LOG	},	// A to D values
	{ "aeh1",	cmd_aeh1,	CMD_NON_VOLATILE					},	// alarm envelope high qtr day 1
	{ "aeh2",	cmd_aeh2,	CMD_NON_VOLATILE					},	// alarm envelope high qtr day 2
	{ "aeh3",	cmd_aeh3,	CMD_NON_VOLATILE					},	// alarm envelope high qtr day 3
	{ "aeh4",	cmd_aeh4,	CMD_NON_VOLATILE					},	// alarm envelope high qtr day 4
	{ "ael1",	cmd_ael1,	CMD_NON_VOLATILE					},	// alarm envelope low qtr day 1
	{ "ael2",	cmd_ael2,	CMD_NON_VOLATILE					},	// alarm envelope low qtr day 2
	{ "ael3",	cmd_ael3,	CMD_NON_VOLATILE					},	// alarm envelope low qtr day 3
	{ "ael4",	cmd_ael4,	CMD_NON_VOLATILE					},	// alarm envelope low qtr day 4
	{ "alm",	cmd_alm,	CMD_VOLATILE						},	// alarm channel config
	{ "ap1",	cmd_ap1,	CMD_NON_VOLATILE					},	// alarm profile qtr day 1
	{ "ap2",	cmd_ap2,	CMD_NON_VOLATILE					},	// alarm profile qtr day 2
	{ "ap3",	cmd_ap3,	CMD_NON_VOLATILE					},	// alarm profile qtr day 3
	{ "ap4",	cmd_ap4,	CMD_NON_VOLATILE					},	// alarm profile qtr day 4
	{ "at",		cmd_at,		CMD_NON_CFG							},	// pass AT command to modem, if it's on
	{ "bv",		cmd_bv,		CMD_NON_CFG							},	// report battery volts as last measured for alarm
	{ "calm",	cmd_calm,	CMD_VOLATILE						},	// enable/disable commission mode alarm
	{ "cec",	cmd_cec,	CMD_VOLATILE						},	// configure event channel
	{ "cfi",	cmd_cfi,	CMD_NON_CFG							},	// configure file invalid flags
	{ "clrb",	cmd_clrb,	CMD_NON_CFG							},	// clear RAM bit
	{ "cmoff",	cmd_cmoff,	CMD_VOLATILE						},	// commissioning mode off
	{ "cmon",	cmd_cmon,	CMD_VOLATILE						},	// commissioning mode on
	{ "cm",		cmd_cm,		CMD_VOLATILE						},	// set/report commissioning mode flag
	{ "dbg",	cmd_dbg,	CMD_NON_CFG							},  // enable/disable debug LED
	{ "dcc",	cmd_dcc,	CMD_VOLATILE						},	// configure digital channel
	{ "ddac",	cmd_ddac,	CMD_VOLATILE						},	// configure doppler damping, amplitude & control bits
	{ "diag",	cmd_diag,	CMD_NON_CFG,						},	// diagnostic command
	{ "dir",	cmd_dir,	CMD_NON_CFG	| CMD_NO_ACTIVITY_LOG	},	// dir command
	{ "do",		cmd_do,		CMD_NON_CFG							},	// execute script file
	{ "dpc",	cmd_dpc,	CMD_NON_CFG | CMD_NO_ACTIVITY_LOG	},	// read digital pulse counts
	{ "dsc",	cmd_dsc,	CMD_VOLATILE						},  // configure doppler sensor
	{ "dsd",	cmd_dsd,	CMD_VOLATILE						},  // configure doppler sensor depth
	{ "dsf",	cmd_dsf,	CMD_VOLATILE						},  // configure doppler sensor flow
	{ "dst",	cmd_dst,	CMD_VOLATILE						},  // configure doppler sensor temperature
	{ "dsv",	cmd_dsv,	CMD_VOLATILE						},  // configure doppler sensor velocity
	{ "dt",		cmd_dt,		CMD_NON_CFG							},	// date/time
	{ "ecd",	cmd_ecd,	CMD_NON_VOLATILE					},	// event configure: set channel event header string
	{ "echo",	cmd_echo,	CMD_NON_CFG | CMD_NO_ACTIVITY_LOG	},	// echo events on USB
	{ "eco",	cmd_eco,	CMD_VOLATILE						},	// event trigger of control output
	{ "fap",	cmd_fap,	CMD_NON_VOLATILE					},	// file append (USB only)
	{ "fas",	cmd_fas,	CMD_NON_VOLATILE					},	// file append string
	{ "fdel",	cmd_fdel,	CMD_NON_CFG							},	// file delete
	{ "frd",	cmd_frd,	CMD_NON_CFG	| CMD_NO_ACTIVITY_LOG	},	// file read
	{ "frl",	cmd_frl,	CMD_NON_CFG | CMD_NO_ACTIVITY_LOG	},	// file read line
	{ "fsh",	cmd_fsh,	CMD_NON_CFG							},	// file system health
	{ "ftpc",	cmd_ftpc,	CMD_NON_VOLATILE					},	// ftp configure; set ftplogon string contents
	{ "ftx",	cmd_ftx,	CMD_NON_CFG							},	// send file to ftp server
	{ "fwr",	cmd_fwr,	CMD_NON_VOLATILE					},	// file write (USB only)
	{ "fws",	cmd_fws,	CMD_NON_VOLATILE					},	// file write string
	{ "gps",	cmd_gps,	CMD_NON_CFG							},	// gps control and read
	{ "idv",	cmd_idv,	CMD_NON_CFG | CMD_NO_ACTIVITY_LOG	},	// immediate derived values
	{ "imv",	cmd_imv,	CMD_NON_CFG | CMD_NO_ACTIVITY_LOG	},	// immediate values
	{ "isv",	cmd_isv,	CMD_NON_CFG | CMD_NO_ACTIVITY_LOG	},	// immediate serial port values
	{ "li",		cmd_li,		CMD_NON_CFG							},	// logger ID
	{ "log",	cmd_log,	CMD_VOLATILE						},	// logging control
	{ "mkdir",	cmd_mkdir,	CMD_NON_CFG							},	// make directory
	{ "mps",	cmd_mps,	CMD_VOLATILE						},	// modem power schedule
	{ "msg",	cmd_msg,	CMD_NON_CFG							},	// test message
	{ "name",	cmd_name,	CMD_VOLATILE						},	// sitename
	{ "nvb",	cmd_nvb,	CMD_NON_CFG							},	// nivus test - set baudrate
	{ "nvh",	cmd_nvh,	CMD_NON_CFG							},	// nivus test - write height
	{ "nvi",	cmd_nvi,	CMD_NON_CFG							},	// nivus test - get id
	{ "nvm",	cmd_nvm,	CMD_NON_CFG							},	// nivus test - read measure
	{ "nvr",	cmd_nvr,	CMD_NON_CFG							},	// nivus test - read init
	{ "nvt",	cmd_nvt,	CMD_NON_CFG							},	// nivus test - reset sensor
	{ "nvv",	cmd_nvv,	CMD_NON_CFG							},	// nivus test - sound velocity
	{ "nvw",	cmd_nvw,	CMD_NON_CFG							},	// nivus test - write init
	{ "nwres",	cmd_nwres,	CMD_NON_CFG							},	// network test results
	{ "nwtst",	cmd_nwtst,	CMD_NON_CFG							},	// network test start
	{ "pfr",	cmd_pfr,	CMD_VOLATILE						},	// pump flow rate table
	{ "prt",	cmd_prt,	CMD_NON_CFG							},	// pump running times
	{ "ramr",	cmd_ramr,	CMD_NON_CFG | CMD_NO_ACTIVITY_LOG	},	// read RAM
	{ "ramw",	cmd_ramw,	CMD_NON_CFG							},	// write RAM
	{ "rco",	cmd_rco,	CMD_VOLATILE						},	// regular control output
	{ "rda",	cmd_rda,	CMD_NON_CFG							},	// Read all current values
	{ "rhr",	cmd_rhr,	CMD_NON_CFG							},	// Read hardware revision
	{ "reset",  cmd_reset,	CMD_NON_CFG							},  // reset (restart from power on)
	{ "rfd",	cmd_rfd,	CMD_NON_CFG							},	// retrieve ftp data
	{ "rmdir",	cmd_rmdir,	CMD_NON_CFG							},	// remove directory
	{ "roam",	cmd_roam,	CMD_VOLATILE						},	// roaming control
	{ "rsd",	cmd_rsd,	CMD_NON_CFG							},	// retrieve sms data
	{ "sco",	cmd_sco,	CMD_VOLATILE						},	// set control output
	{ "setb",	cmd_setb,	CMD_NON_CFG							},	// set RAM bit
	{ "sfp",	cmd_sfp,	CMD_NON_CFG							},	// script file progress
	{ "sigres",	cmd_sigres,	CMD_NON_CFG							},	// signal test results
	{ "sigtst",	cmd_sigtst,	CMD_NON_CFG							},	// signal test start
	{ "smsc",	cmd_smsc,	CMD_VOLATILE						},	// SMS configuration
	{ "tc",		cmd_tc,		CMD_NON_CFG							},	// time change
	{ "tod",	cmd_tod,	CMD_VOLATILE						},	// time of day config & readback
	{ "tot",	cmd_tot,	CMD_NON_CFG							},	// totaliser config & readback
	{ "tro",	cmd_tro,	CMD_NON_CFG							},	// trigger control output
	{ "tstat",	cmd_tstat,	CMD_NON_CFG							},	// time sync report status
	{ "tsu",	cmd_tsu,	CMD_NON_CFG							},	// transmit set up
	{ "tsync",	cmd_tsync,	CMD_VOLATILE						},	// time sync - set and report protocol and step
	{ "test",	cmd_test,	CMD_NON_CFG,						},	// test command - undocumented
	{ "frmw",	cmd_frmw,	CMD_NON_CFG,						},	// fram write - undocumented
	{ "frmr",	cmd_frmr,	CMD_NON_CFG							},	// fram read - undocumented
	{ "mod",	cmd_mod,	CMD_NON_CFG							}	// MODBUS test command
};

#pragma endregion

// Default alarm profile file contents is 24 x NO_VALUE
const char cmd_default_alarm_profile[] =
	"FFFF0FFFFF0FFFFF0FFFFF0FFFFF0FFFFF0FFFFF0FFFFF0FFFFF0FFFFF0FFFFF0FFFFF0F"
	"FFFF0FFFFF0FFFFF0FFFFF0FFFFF0FFFFF0FFFFF0FFFFF0FFFFF0FFFFF0FFFFF0FFFFF0F";

#pragma region Input Handling

/******************************************************************************
** Function:	Go to start of next field
**
** Notes:		Exits with cmd_input_ptr pointing to first char of next field, or '\0'
**				Ignores commas which are escaped by the '$' character
*/
void cmd_next_field(void)
{
	bool escape;

	if (cmd_error_code != CMD_ERR_NONE)	// leave input ptr where it is
		return;

	// skip to end of this field
	escape = false;
	while (cmd_input_ptr != cmd_end_ptr)
	{
		if (!escape && (*cmd_input_ptr == ','))
		{
			cmd_input_ptr++;
			break;
		}

		escape = (*cmd_input_ptr++ == '$');
	}
}

/******************************************************************************
** Function:	Check if field is null
**
** Notes:		If so & there are further fields, skip to next
*/
bool cmd_field_null(void)
{
	if (*cmd_input_ptr == ',')					// null field
	{
		cmd_input_ptr++;						// point to next field
		return true;
	}

	return (cmd_input_ptr == cmd_end_ptr);		// field is null if we've reached the end
}

/******************************************************************************
** Function:	Get next field as null-terminated string in STR_buffer
**
** Notes:		Moves cmd_input_ptr to start of next field
**				Returns field length
*/
int cmd_get_field(void)
{
	char *p;
	int i;

	p = cmd_input_ptr;
	cmd_next_field();
	i = 0;
	while ((p < cmd_input_ptr) && (*p != ','))
		STR_buffer[i++] = *p++;

	STR_buffer[i] = '\0';
	return i;
}

/******************************************************************************
** Function:	Parse uint32
**
** Notes:		returns true if non-null field parsed successfully
*/
bool cmd_set_uint32(uint32 *p)
{
	unsigned long x;

	if ((cmd_error_code != CMD_ERR_NONE) || cmd_field_null())
		return false;

	cmd_get_field();
	if (sscanf(STR_buffer, "%lu", &x) == 1)
	{
		*p = x;
		return true;
	}
	// else:

	cmd_error_code = CMD_ERR_INVALID_VALUE;
	return false;
}

/******************************************************************************
** Function:	Parse uint16
**
** Notes:		returns true if non-null field parsed successfully
*/
bool cmd_set_uint16(uint16 *p)
{
	unsigned int x;

	if ((cmd_error_code != CMD_ERR_NONE) || cmd_field_null())
		return false;

	cmd_get_field();
	if (sscanf(STR_buffer, "%u", &x) == 1)
	{
		*p = x;
		return true;
	}
	// else:

	cmd_error_code = CMD_ERR_INVALID_VALUE;
	return false;
}

/******************************************************************************
** Function:	Parse uint8
**
** Notes:
*/
void cmd_set_uint8(uint8 *p)
{
	uint16 x;

	if (!cmd_set_uint16(&x))
		return;

	if (HIGHBYTE(x) != 0)
		cmd_error_code = CMD_ERR_INVALID_VALUE;
	else
		*p = LOWBYTE(x);
}

/******************************************************************************
** Function:	Parse bool
**
** Notes:
*/
void cmd_set_bool(bool *p)
{
	uint16 x;

	if (!cmd_set_uint16(&x))
		return;

	if (x > 1)
		cmd_error_code = CMD_ERR_INVALID_VALUE;
	else
		*p = (x != 0);
}

/******************************************************************************
** Function:	Parse binary
**
** Notes:		parse n bits of binary from input string into a uint8
**				n must be > 0 and <= 8
**				only characters allowed are '0' and '1'
**				must be exactly n characters in field
*/
void cmd_set_binary(uint8 *p, uint8 n)
{
	uint8 result = 0;
	int   index = 0;

	if ((cmd_error_code != CMD_ERR_NONE) || cmd_field_null())
		return;

	if (n == 0) n = 1;
	else if (n > 8) n = 8;

	cmd_get_field();
	// note: will get "111," in STR_buffer if middle of the input stream
	//       or "111" if last entry in input stream
	if (STR_buffer[n] == ',') STR_buffer[n] = '\0';
	if (n != strlen(STR_buffer))
	{
		cmd_error_code = CMD_ERR_INVALID_VALUE;
		return;
	}

	while (index < n)
	{
		result <<= 1;
		if (STR_buffer[index] == '1')
			result |= 0x01;
		else if (STR_buffer[index] != '0')
		{
			cmd_error_code = CMD_ERR_INVALID_VALUE;
			return;
		}
		index++;
	}
	*p = result;
}

/******************************************************************************
** Function:	Parse float
**
** Notes:		returns true if non-null field parsed successfully
*/
bool cmd_set_float(float *p)
{
	float x;

	if ((cmd_error_code != CMD_ERR_NONE) || cmd_field_null())
		return false;

	cmd_get_field();
	if (sscanf(STR_buffer, "%f", &x) == 1)
	{
		*p = x;
		return true;
	}
	// else:

	cmd_error_code = CMD_ERR_INVALID_VALUE;
	return false;
}

/******************************************************************************
** Function:	Parse hex integer
**
** Notes:
*/
bool cmd_set_hex(uint16 *p)
{
	unsigned int x;

	if ((cmd_error_code != CMD_ERR_NONE) || cmd_field_null())
		return false;

	cmd_get_field();
	if (sscanf(STR_buffer, "%x", &x) == 1)
	{
		*p = x;
		return true;
	}
	// else:

	cmd_error_code = CMD_ERR_INVALID_VALUE;
	return false;
}

/******************************************************************************
** Function:	Parse hex byte
**
** Notes:
*/
bool cmd_set_hex_byte(uint8 *p)
{
	unsigned int x;

	if ((cmd_error_code != CMD_ERR_NONE) || cmd_field_null())
		return false;

	cmd_get_field();
	if (sscanf(STR_buffer, "%x", &x) == 1)
	{
		if ((x & 0xff00) == 0x0000)
		{
			*p = (uint8)(x & 0x00ff);
			return true;
		}
	}
	// else:

	cmd_error_code = CMD_ERR_INVALID_VALUE;
	return false;
}

/******************************************************************************
** Function:	Parse a 2-digit ASCII value to BCD
**
** Notes:		If separator is '\0' accept any termination, and exit pointing to
**				that separator. Otherwise termination should agree with separator,
**				and exit pointing to character after separator
*/
uint8 cmd_parse_bcd(char separator, uint8 max_bcd)
{
	uint8 c;

	c = 0;
	if (cmd_error_code == CMD_ERR_NONE)
	{
		if (!isdigit(*cmd_input_ptr) || !isdigit(*(cmd_input_ptr + 1)))
			cmd_error_code = CMD_ERR_INVALID_VALUE;
		else
		{
			c = (*cmd_input_ptr++ - '0') << 4;
			c += *cmd_input_ptr++ - '0';
			if (c > max_bcd)
				cmd_error_code = CMD_ERR_VALUE_OUT_OF_RANGE;
			else if (separator != '\0')
			{
				if (*cmd_input_ptr++ != separator)
					cmd_error_code = CMD_ERR_WRONG_SEPARATOR;
			}
		}
	}

	return c;
}

/******************************************************************************
** Function:	If a flag is supplied, set or clear bit in a mask appropriately
**
** Notes:
*/
void cmd_parse_flag(uint8 * p_flags, const uint8 mask)
{
	uint16 i;

	if (!cmd_set_uint16(&i))	// leave flag unchanged
		return;

	if (i != 0)
		*p_flags |= mask;
	else
		*p_flags &= ~mask;
}

/******************************************************************************
** Function:	Parse date into 3 bcd fields
**
** Notes:	Returns false if empty field or error
*/
bool cmd_set_date_bcd(uint8 *p_dd_bcd, uint8 *p_mm_bcd, uint8 *p_yy_bcd)
{
	if (cmd_field_null() ||	(cmd_error_code != CMD_ERR_NONE))
		return false;
	// else:

	*p_dd_bcd = cmd_parse_bcd('\0', 0x31);
	*p_mm_bcd = cmd_parse_bcd('\0', 0x12);
	*p_yy_bcd = cmd_parse_bcd('\0', 0x99);

	if (*p_dd_bcd < 1)
		*p_dd_bcd = 1;

	if (*p_mm_bcd < 1)
		*p_mm_bcd = 1;

	cmd_next_field();
	return (cmd_error_code == CMD_ERR_NONE);
}

/******************************************************************************
** Function:	Parse time into 2 or 3 bcd fields
**
** Notes:	Returns false if empty field or error
*/
bool cmd_set_time_bcd(uint8 *p_hh_bcd, uint8 *p_mm_bcd, uint8 *p_ss_bcd)
{
	if (cmd_field_null())
		return false;
	// else:

	*p_hh_bcd = cmd_parse_bcd(':', 0x23);
	if (p_ss_bcd != NULL)
	{
		*p_mm_bcd = cmd_parse_bcd(':', 0x59);
		*p_ss_bcd = cmd_parse_bcd('\0', 0x59);
	}
	else										// NULL pointer; no seconds
		*p_mm_bcd = cmd_parse_bcd('\0', 0x59);

	cmd_next_field();
	return (cmd_error_code == CMD_ERR_NONE);
}

/******************************************************************************
** Function:	Parse channel number
**
** Notes:		Returns false if unparsable channel id
**				Tolerant of excess characters in channel name
**				Waste Water - extended to parse derived analogue channels DA1 to DA7
**				serial sensor data channels S1, S2 and DS1
*/
bool cmd_set_channel_no(uint8 *p)
{
	bool result = false;

	if ((*cmd_input_ptr == 'a') || (*cmd_input_ptr == 'A'))
	{
		cmd_input_ptr++;
		if ((*cmd_input_ptr >= '1') && (*cmd_input_ptr <= '7'))
		{
			*p = (*cmd_input_ptr - '0') + 4;
			result = true;
		}
	}
#ifndef HDW_RS485
	else if ((*cmd_input_ptr == 'd') || (*cmd_input_ptr == 'D'))
	{
		cmd_input_ptr++;
		if (*cmd_input_ptr == '1')
		{
			*p = 1;
			cmd_input_ptr++;
			if ((*cmd_input_ptr == 'b') || (*cmd_input_ptr == 'B')) 
				*p = 2;
			result = true;
		}
		else if (*cmd_input_ptr == '2')
		{
			*p = 3;
			cmd_input_ptr++;
			if ((*cmd_input_ptr == 'b') || (*cmd_input_ptr == 'B')) 
				*p = 4;
			result = true;
		}
		else if ((*cmd_input_ptr == 'a') || (*cmd_input_ptr == 'A'))
		{
			cmd_input_ptr++;
			if ((*cmd_input_ptr >= '1') && (*cmd_input_ptr <= '7'))
			{
				*p = (*cmd_input_ptr - '0') + 15;
				result = true;
			}
		}
	}
#else
	else if ((*cmd_input_ptr == 's') || (*cmd_input_ptr == 'S'))
	{
		cmd_input_ptr++;
		if ((*cmd_input_ptr >= '1') && (*cmd_input_ptr <= '3'))		// S1, S2 or S2
		{
			*p = *cmd_input_ptr - '0';
			result = true;
		}
	}
	else if ((*cmd_input_ptr == 'd') || (*cmd_input_ptr == 'D'))
	{
		cmd_input_ptr++;
		if ((*cmd_input_ptr == 's') || (*cmd_input_ptr == 'S'))
		{
			cmd_input_ptr++;
			if (*cmd_input_ptr == '1')
			{
				*p = 12;											// DS1
				result = true;
			}
		}
	}

#endif

	cmd_next_field();
	return result;
}

/******************************************************************************
** Function:	Parse unquoted string
**
** Notes:		Warning: Sets return to 0 and p_out to empty string if null field OR if string is "$0".
**				Handles escape sequences $, $> and $$. Returns length of output string.
**				If parsing into cmd_path, handles '\', '/' or " /" for '\'.
*/
int cmd_set_string(char *p_out, int max_length)
{
	bool escape;
	bool is_path;
	int i;

	escape = false;
	is_path = (p_out == &cmd_path[1]);
	i = 0;

	// decrement max_length to allow space for '\0':
	max_length--;
	while ((cmd_input_ptr != cmd_end_ptr) && (i < max_length))
	{
		*p_out++ = *cmd_input_ptr;
		i++;
		if (escape)
		{
			escape = false;
			if (*cmd_input_ptr == '>')		// escape sequence for \r\n
			{
				p_out--;					// overwite >
				*p_out++ = '\r';
				if (i < max_length)
				{
					*p_out++ = '\n';
					i++;
				}
			}
			else if (*cmd_input_ptr == '0')	// escape sequence for \0
			{
				p_out--;					// overwite 0
				i--;
			}
		}
		else if (*cmd_input_ptr == '$')		// start of escape sequence
		{
			escape = true;
			p_out--;						// overwrite $ next time
			i--;							// 1 more space available
		}
		else if (*cmd_input_ptr == ',')		// unescaped comma = end-of-string
		{
			p_out--;						// overwite comma with \0
			i--;
			cmd_input_ptr++;				// point to next field
			break;
		}
		else if (is_path)					// '/' or " /" -> '\'
		{
			if (*cmd_input_ptr == ' ')		// probably the SMS escape character
			{
				p_out--;					// overwrite space next time
				i--;						// 1 more space available
			}
			else if (*cmd_input_ptr == '/')
				*(p_out - 1) = '\\';		// substitute '\' for '/'
		}
		cmd_input_ptr++;
	}

	*p_out = '\0';
	return i;
}

/******************************************************************************
** Function:	Output a string with characters escaped
**
** Notes:		Checks output length not exceeded
*/
void cmd_escape_string(char *s, char *p_out)
{
	do
	{
		if (p_out >= cmd_out_ptr + CMD_MAX_LENGTH - 1)
		{
			*p_out = '\0';
			break;
		}

		if ((*s == ',') || (*s == '$'))
		{
			*p_out++ = '$';
			if (p_out < cmd_out_ptr + CMD_MAX_LENGTH - 1)
				*p_out++ = *s;
		}
		else if ((*s == '\r') || (*s == '\n'))
		{
			*p_out++ = '$';
			if (p_out < cmd_out_ptr + CMD_MAX_LENGTH - 1)
			{
				*p_out++ = '>';
				if (*(s + 1) == '\n')
					s++;
			}
		}
		else
			*p_out++ = *s;
	
	} while (*s++ != '\0');
}


/******************************************************************************
** Function:	Parse destination into cmd_destination
**
** Notes:
*/
void cmd_parse_destination(void)
{
	if (cmd_set_string(cmd_destination, sizeof(cmd_destination)) == 0)
		return;							// empty string is OK

	if ((STR_match(cmd_destination, "sms:")) || (STR_match(cmd_destination, "pdu:")))			// PB 8.7.09
	{
		if (!STR_phone_number_ok(&cmd_destination[4]))
			cmd_error_code = CMD_ERR_INVALID_VALUE;
	}
	else if (!STR_match(cmd_destination, "monitor") &&
			 !STR_match(cmd_destination, "ftp") && !STR_match(cmd_destination, "pdu"))
		cmd_error_code = CMD_ERR_INVALID_DESTINATION;
}

/******************************************************************************
** Function:	Parse string into cmd_path & cmd_filename_ptr
**
** Notes:		Adds string terminator to cmd_path & sets cmd_filename_ptr
** Character '\' is not a valid 7-bit SMS character. This routine allows
** '/' to be used instead, or the string " /" to represent '\'. If an SMS
** is received with the '\' in it, and this is read in CMGF=1 format, the
** '\' appears as " /".
*/
void cmd_parse_path(void)
{
	int length;
	int i;

	// implicit root at beginning of path:
	cmd_path[0] = '\\';
	switch (*cmd_input_ptr)
	{
	case '\\':
	case '/':
		cmd_input_ptr++;
		break;

	case ' ':
		if (*(cmd_input_ptr + 1) == '/')
			cmd_input_ptr += 2;
		break;
	}

	length = cmd_set_string(&cmd_path[1], sizeof(cmd_path) - 1);
	// go to last \ in path:
	// start at index = length, as we parsed into &cmd_path[1]
	for (i = length; i > 0; i--)
	{
		if (cmd_path[i] == '\\')
			break;
	}

	if (i != 0)									// path specified
	{
		cmd_path[i] = '\0';
		cmd_filename_ptr = &cmd_path[i + 1];
	}
	else										// just a filename, path = root
	{
		for (i = length + 1; i > 0; i--)
			cmd_path[i + 1] = cmd_path[i];

		cmd_path[1] = '\0';
		cmd_filename_ptr = &cmd_path[2];
	}
}

/******************************************************************************
** Function:	Print full path into STR_buffer
**
** Notes:		Path gets parsed into cmd_path & cmd_filename_ptr. This gets full path.
*/
void cmd_get_full_path(void)
{
	char *p;

	sprintf(STR_buffer, "%s\\%s", (cmd_path[1] == '\0') ? "" : cmd_path, cmd_filename_ptr);

	if (cmd_source_index == CMD_SOURCE_SMS) // use '/' instead of '\'
	{
		for (p = STR_buffer; *p != '\0'; p++)
		{
			if (*p == '\\')
				*p = '/';
		}
	}
}

/******************************************************************************
** Function:	Generate RAMR reply
**
** Notes:		Used by #RAMR, #RAMW, #SETB, #CLRB
*/
void cmd_generate_ramr_reply(void)
{
	int i;
	uint8 c;

	if (cmd_error_code == CMD_ERR_NONE)
	{
		if (cmd_byte_count > 74)
			cmd_byte_count = 74;

		sprintf(cmd_out_ptr, "dRAMR=%04X,", cmd_address);
		i = 11;
		while (cmd_byte_count-- != 0)
		{
			c = *(uint8 *)cmd_address++;
			cmd_out_ptr[i++] = STR_hex_char[c >> 4];
			cmd_out_ptr[i++] = STR_hex_char[c & 0x0F];
		}
		cmd_out_ptr[i] = '\0';
	}
}

/******************************************************************************
** Function:	convert flag contents of in_byte to binary out_string of length n_bits
**
** Notes:
*/
void cmd_convert_flags_to_string(char * out_string, uint8 in_byte, uint8 n_bits)
{
	uint8 mask;

	if (n_bits == 0) n_bits = 1;
	else if (n_bits > 8) n_bits = 8;
	out_string[0] = '\0';
	// create mask
	mask = (uint8)CMD_word_mask[n_bits - 1];
	// convert byte to binary string
	do
	{
		if ((in_byte & mask) == 0) strcat(out_string, "0");
		else strcat(out_string, "1");
		mask >>= 1;
	}
	while (mask != 0);
}

#pragma endregion

#pragma region Non MODBUS stuff

/******************************************************************************
** Function:	check whether reporting alarm channel has a valid analogue or digital channel for this build
**
** Notes:		channel index (0 - 21) comes from #alm, #ap
*/
bool cmd_report_alarm_channel_ok(void)
{
#ifndef HDW_RS485
	if (cmd_channel_index < ALM_ANA_ALARM_CHANNEL0)												// index 0..3 = digital channel
		return (cmd_channel_index < 2 * CAL_build_info.num_digital_channels);	
  #ifndef HDW_GPS			
	else if (cmd_channel_index < ALM_ALARM_DERIVED_CHANNEL0)									// index 4..10 = analogue channel
		return ANA_channel_exists(cmd_channel_index - LOG_ANALOGUE_1_INDEX + 1);
  #endif
	else if ((cmd_channel_index - 11) < ALM_ANA_ALARM_CHANNEL0)									// index 11..14 = derived digital channel
		return ((cmd_channel_index - 11) < 2 * CAL_build_info.num_digital_channels);				
	else 
  #ifndef HDW_GPS			
		return ANA_channel_exists((cmd_channel_index - 11) - LOG_ANALOGUE_1_INDEX + 1);			// index 15..21 = derived analogue channel
  #else
		return false;
  #endif
#else
	if (cmd_channel_index <= ALM_ANA_ALARM_CHANNEL0)											// serial modbus/doppler channels
		return true;
	else if (cmd_channel_index < ALM_ALARM_DERIVED_CHANNEL0)									// index 5..10 = analogue channel
		return ANA_channel_exists(cmd_channel_index - 5);
	else
		return (cmd_channel_index == 11);														// index 11 - derived serial doppler channel

#endif
}

/******************************************************************************
** Function:	Clear or set bit
**
** Notes:
*/
void cmd_set_ram_bit(bool set)
{
	int i;

	i = 8;
	if (cmd_equals && cmd_set_hex(&cmd_address))
	{
		sscanf(cmd_input_ptr, "%d", &i);
		if (i > 7)
			cmd_error_code = CMD_ERR_VALUE_OUT_OF_RANGE;
		else if (set)
			*(uint8 *)cmd_address |= 1 << i;
		else
			*(uint8 *)cmd_address &= ~(1 << i);
	}
	else
		cmd_error_code = CMD_ERR_INVALID_VALUE;

	cmd_byte_count = 1;
	cmd_generate_ramr_reply();
}

/******************************************************************************
** Function:	Report no value as 0.
**
** Notes:
*/
float cmd_get_print_value(float f)
{
	return (f == FLT_MAX) ? 0.0f : f;
}

/******************************************************************************
** Function:	Abort a com task test
**
** Notes:		
*/
void cmd_abort(void)
{
	com_pending_abort = true;
	if (cmd_error_code == CMD_ERR_NONE)
		sprintf(cmd_out_ptr, "dABORT");
}

/******************************************************************************
** Function:	Analogue boost time
**
** Notes:		Get/set time in ms
*/
void cmd_abt(void)
{
#ifdef HDW_GPS
	cmd_error_code = CMD_ERR_UNRECOGNISED_COMMAND;
#else
	if (cmd_equals)
		cmd_set_uint16(&ANA_boost_time_ms);

	if (cmd_error_code == CMD_ERR_NONE)
		sprintf(cmd_out_ptr, "dABT=%u", ANA_boost_time_ms);
#endif
}

/******************************************************************************
** Function:	Autocal analogue channel
**
** Notes:
*/
void cmd_acal(void)
{
#ifdef HDW_GPS
	cmd_error_code = CMD_ERR_UNRECOGNISED_COMMAND;
#else
	int i;

	if (!ANA_channel_exists(cmd_channel_index))
	{
		cmd_error_code = CMD_ERR_INVALID_CHANNEL_NUMBER;
		return;
	}
	// else:

	if (cmd_state == CMD_IDLE)				// start off the read process
	{
		ANA_config[cmd_channel_index].auto_offset = 0.0f;	// set temporarily
		ANA_start_adc_read(cmd_channel_index);
		cmd_state = CMD_AUTOCAL;
	}
	else									// A/D reads complete
	{
		ANA_config[cmd_channel_index].auto_offset = (ANA_channel[cmd_channel_index].sample_value == FLT_MAX) ?
			0.0f : -(ANA_channel[cmd_channel_index].sample_value - ANA_config[cmd_channel_index].user_offset);

#if (HDW_NUM_CHANNELS == 3)					// keep shadow channel configs in sync
		if (cmd_channel_index < 2)			// have just ACALed main channel
			ANA_config[cmd_channel_index + 2].auto_offset = ANA_config[cmd_channel_index].auto_offset;
		else								// have just ACALed shadow channel
			ANA_config[cmd_channel_index - 2].auto_offset = ANA_config[cmd_channel_index].auto_offset;
#endif

		i = sprintf(cmd_out_ptr, "dACAL%u=", cmd_channel_index + 1);
		i += STR_print_float(cmd_out_ptr + i, ANA_config[cmd_channel_index].auto_offset);
		sprintf(cmd_out_ptr + i, ",%u", ANA_config[cmd_channel_index].units_index);

		cmd_equals = true;					// force command to be logged in use.txt
	}
#endif
}

/******************************************************************************
** Function:	Configure analogue channel
**u
** Notes:		Channel index already set in cmd_channel_index
*/
void cmd_acc(void)
{
#ifdef HDW_GPS
	cmd_error_code = CMD_ERR_UNRECOGNISED_COMMAND;
#else
	ANA_config_type *p;
	int i;

	if (!ANA_channel_exists(cmd_channel_index))
	{
		cmd_error_code = CMD_ERR_INVALID_CHANNEL_NUMBER;
		return;
	}

	p = &ANA_config[cmd_channel_index];

	if (cmd_equals)
	{
		cmd_error_code = CMD_parse_acc(p, cmd_input_ptr, cmd_end_ptr);
		if (!ANA_configure_channel(cmd_channel_index))
			cmd_error_code = CMD_ERR_READ_CAL_FAILED;
		else
			ALM_update_profile();		// trigger alarm profile fetch due to changed channel setup
	}

	if (cmd_error_code == CMD_ERR_NONE)
	{
		// Do reply up to stop time (inclusive):
		i = sprintf(cmd_out_ptr, "dACC%u=", cmd_channel_index + 1);
		ANA_print_config(cmd_channel_index, &cmd_out_ptr[i]);
	}
#endif
}

/******************************************************************************
** Function:	Automatic control output
**
** Notes:
*/
void cmd_aco(void)
{
	COP_config_type *p;
	int j;
	uint8 sensor_type, channel, sub_channel, gate;

	if (cmd_channel_index >= COP_NUM_CHANNELS)
	{
		cmd_error_code = CMD_ERR_INVALID_CHANNEL_NUMBER;
		return;
	}

	p = &COP_config[cmd_channel_index];

	if (cmd_equals)
	{																							// parse contents of equals command
		cmd_parse_flag(&p->flags, COP_MASK_AUTO_ENABLED);
		cmd_set_hex_byte(&p->auto_channel_id);
		if ((p->flags & COP_MASK_AUTO_ENABLED) != 0x00)											// if enabling
		{
			sensor_type = p->auto_channel_id & 0xc0;											// test for valid channel
#ifdef HDW_RS485
			if (sensor_type == 0x80)															// if serial sensor
			{
				sub_channel = (p->auto_channel_id & 0x08);												// use sub_channel to check derived flag
				channel = (p->auto_channel_id & 0x03) - 1;
				if (((sub_channel != 0) && (channel > 0)) || (channel > 1))						// check for valid channels
				{
					cmd_error_code = CMD_ERR_INVALID_CHANNEL_NUMBER;
					return;
				}
			}
#else
			if (sensor_type == 0x00)															// if digital
			{
				channel = (p->auto_channel_id & 0x03) - 1;
				sub_channel = (p->auto_channel_id & 0x20) >> 5;
				if (channel >= DIG_NUM_CHANNELS)												// check for valid channels and sub channels
				{
					cmd_error_code = CMD_ERR_INVALID_CHANNEL_NUMBER;
					return;
				}
				if (sub_channel > 1)
				{
					cmd_error_code = CMD_ERR_INVALID_CHANNEL_NUMBER;
					return;
				}
			}
#endif
#ifndef HDW_GPS
			else if (sensor_type == 0x40)														// if analogue
			{
				channel = (p->auto_channel_id & 0x07) - 1;
				if (!ANA_channel_exists(channel))
				{
					cmd_error_code = CMD_ERR_INVALID_CHANNEL_NUMBER;
					return;
				}
			}
			else
			{
				cmd_error_code = CMD_ERR_INVALID_CHANNEL_NUMBER;
				return;
			}
#endif
		}
		if (cmd_set_hex_byte(&gate))															// event gate - limit check
		{
			if (((gate & 0x0f) < 0x05) && ((gate & 0xf0) < 0x20))
				p->auto_event_gate = gate;
			else
			{
				cmd_error_code = CMD_ERR_VALUE_OUT_OF_RANGE;
				return;
			}
		}
		cmd_set_time_bcd(&p->auto_window.start.hr_bcd, &p->auto_window.start.min_bcd, NULL);
		cmd_set_time_bcd(&p->auto_window.stop.hr_bcd, &p->auto_window.stop.min_bcd, NULL);
//		cmd_set_binary(&p->auto_window.day_mask, 7);											// commented out - may be needed TBD
		cmd_set_uint8(&p->auto_window.interval);
		cmd_parse_flag(&p->flags, COP_MASK_QUANTITY);
		cmd_set_float(&p->quantity);
		cmd_set_float(&p->high_threshold);
		cmd_set_float(&p->low_threshold);
		cmd_set_float(&p->deadband);
		cmd_set_uint8(&p->debounce_delay);
		cmd_set_binary(&p->threshold_trigger_mask, 4);

		if (cmd_error_code == CMD_ERR_NONE)
		{
			COP_recalc_wakeups();																// recalculate wake up times
			COP_start_auto(cmd_channel_index);													// initiate auto sampling
		}
	}

	if (cmd_error_code == CMD_ERR_NONE)
	{
		j = sprintf(cmd_out_ptr, "dACO%u=", cmd_channel_index + 1);
		j+= sprintf(&cmd_out_ptr[j], "%d,%02x,%02x,%02x:%02x,%02x:%02x,%u,%d,",
					  ((p->flags & COP_MASK_AUTO_ENABLED) != 0x00) ? 1 : 0,
					  p->auto_channel_id,
					  p->auto_event_gate,
					  p->auto_window.start.hr_bcd, p->auto_window.start.min_bcd,
					  p->auto_window.stop.hr_bcd, p->auto_window.stop.min_bcd,
					  p->auto_window.interval,
					  ((p->flags & COP_MASK_QUANTITY) != 0x00) ? 1 : 0);
		j += STR_print_float(&cmd_out_ptr[j], p->quantity);
		cmd_out_ptr[j++] = ',';
		j += STR_print_float(&cmd_out_ptr[j], p->high_threshold);
		cmd_out_ptr[j++] = ',';
		j += STR_print_float(&cmd_out_ptr[j], p->low_threshold);
		cmd_out_ptr[j++] = ',';
		j += STR_print_float(&cmd_out_ptr[j], p->deadband),
		j += sprintf(&cmd_out_ptr[j], ",%u,", p->debounce_delay);
		cmd_convert_flags_to_string(STR_buffer, p->threshold_trigger_mask, 4);
		j += sprintf(&cmd_out_ptr[j], "%s",STR_buffer);
	}
}

/******************************************************************************
** Function:	analogue derived data channel configuration
**
** Notes:		Channel index already set in cmd_channel_index
*/
void cmd_add(void)
{
#ifdef HDW_GPS
	cmd_error_code = CMD_ERR_UNRECOGNISED_COMMAND;
#else
	ANA_config_type *p;
	int i;

	if (!ANA_channel_exists(cmd_channel_index))
	{
		cmd_error_code = CMD_ERR_INVALID_CHANNEL_NUMBER;
		return;
	}

	p = &ANA_config[cmd_channel_index];

	if (cmd_equals)
	{
		cmd_parse_flag(&p->flags, ANA_MASK_DERIVED_DATA_ENABLED);
		cmd_set_uint8(&p->derived_type);
		if (cmd_error_code == CMD_ERR_NONE)
		{
			if (!ANA_retrieve_derived_conversion_data(cmd_channel_index, p->derived_type))
				cmd_error_code = CMD_ERR_FILE_READ_LINE_FAILED;
		}
		cmd_parse_flag(&p->flags, ANA_MASK_DERIVED_MESSAGING_ENABLED);
		cmd_set_uint8(&p->derived_sensor_index);
		cmd_set_hex_byte(&p->derived_sms_message_type);
		cmd_set_uint8(&p->derived_description_index);
		cmd_set_uint8(&p->derived_units_index);
		ANA_insert_derived_header(cmd_channel_index);											// ensure derived header inserted when start logging
	}

	if (cmd_error_code == CMD_ERR_NONE)
	{
		i = sprintf(cmd_out_ptr, "dADD%u=", cmd_channel_index + 1);								// Do reply
		i += sprintf(&cmd_out_ptr[i], "%d,%u,%d,%u,%02X,%u,%u",
			((p->flags & ANA_MASK_DERIVED_DATA_ENABLED) != 0) ? 1 : 0,
			p->derived_type,
			((p->flags & ANA_MASK_DERIVED_MESSAGING_ENABLED) != 0) ? 1 : 0,
			p->derived_sensor_index,
			p->derived_sms_message_type,
			p->derived_description_index,
			p->derived_units_index);
	}
#endif
}

/******************************************************************************
** Function:	Read A to D converter values for analogue channel
**
** Notes:
*/
void cmd_adv(void)
{
#ifdef HDW_GPS
	cmd_error_code = CMD_ERR_UNRECOGNISED_COMMAND;
#else
	int i;

	if (!ANA_channel_exists(cmd_channel_index))
	{
		cmd_error_code = CMD_ERR_INVALID_CHANNEL_NUMBER;
		return;
	}
	// else:

	if (cmd_state == CMD_IDLE)			// start off the read process
	{
		ANA_start_adc_read(cmd_channel_index);
		cmd_state = CMD_READ_ADV;
	}
	else								// read complete - print results
	{
		if (ANA_config[cmd_channel_index].sensor_type == ANA_SENSOR_DIFF_MV)
			i = sprintf(cmd_out_ptr, "dADV%u=%u,%u,", cmd_channel_index + 1, ANA_zero_counts, SNS_adc_value);
		else
			i = sprintf(cmd_out_ptr, "dADV%u=%u,%u,", cmd_channel_index + 1, ANA_vref_counts, SNS_adc_value);

		STR_print_float(cmd_out_ptr + i, ANA_channel[cmd_channel_index].sample_value);
	}
#endif
}

/******************************************************************************
** Function:	Configure alarm channel
**
** Notes:		Channel index already set in cmd_channel_index
*/
void cmd_alm(void)
{
	ALM_config_type *p;
	int j;

	if (!cmd_report_alarm_channel_ok())
	{
		cmd_error_code = CMD_ERR_INVALID_CHANNEL_NUMBER;
		return;
	}

	p = &ALM_config[cmd_channel_index];

	if (cmd_equals)
	{
		cmd_set_bool(&p->enabled);
		cmd_set_uint8(&p->sample_interval);
		cmd_set_float(&p->default_high_threshold);
		cmd_set_float(&p->default_low_threshold);
		cmd_set_float(&p->deadband);
		cmd_set_uint8(&p->debounce_delay);
		cmd_set_binary(&p->high_enable_mask, 3);
		cmd_set_binary(&p->high_clear_mask, 3);
		cmd_set_binary(&p->low_enable_mask, 3);
		cmd_set_binary(&p->low_clear_mask, 3);
		cmd_set_uint8(&p->type);
		cmd_set_float(&p->profile_width);
		cmd_set_bool(&p->width_is_multiplier);

		if (cmd_error_code == CMD_ERR_NONE)
		{
			ALM_configure_channel(cmd_channel_index);
			ALM_update_profile();		// trigger alarm profile fetch due to possible changed data
		}
	}

	if (cmd_error_code == CMD_ERR_NONE)
	{
		// Do reply up to debounce delay (inclusive):
		j = sprintf(cmd_out_ptr, "dALM%s=%d,%u,", LOG_channel_id[cmd_channel_index + 1],
				p->enabled, p->sample_interval);
		j += STR_print_float(&cmd_out_ptr[j], p->default_high_threshold);
		cmd_out_ptr[j++] = ',';
		j += STR_print_float(&cmd_out_ptr[j], p->default_low_threshold);
		cmd_out_ptr[j++] = ',';
		j += STR_print_float(&cmd_out_ptr[j], p->deadband),
		j += sprintf(&cmd_out_ptr[j], ",%u,", p->debounce_delay);

		// convert each flag byte into string
		cmd_convert_flags_to_string(STR_buffer, p->high_enable_mask, 3);
		j += sprintf(&cmd_out_ptr[j], "%s,",STR_buffer);

		cmd_convert_flags_to_string(STR_buffer, p->high_clear_mask, 3);
		j += sprintf(&cmd_out_ptr[j], "%s,",STR_buffer);

		cmd_convert_flags_to_string(STR_buffer, p->low_enable_mask, 3);
		j += sprintf(&cmd_out_ptr[j], "%s,",STR_buffer);

		cmd_convert_flags_to_string(STR_buffer, p->low_clear_mask, 3);
		j += sprintf(&cmd_out_ptr[j], "%s,",STR_buffer);

		// Do rest of reply:
		j += sprintf(&cmd_out_ptr[j], "%u,", p->type);
		j += STR_print_float(&cmd_out_ptr[j], p->profile_width);
		sprintf(&cmd_out_ptr[j], ",%d", p->width_is_multiplier);
	}
}

/******************************************************************************
** Function:	AINFO generic command
**
** Notes:	type_string "P" for profile, "EH" for envelope high, "EL" for envelope low
*/
void cmd_ainfo(const char * type_string, uint8 quarter)
{
	int i;

	if (!cmd_report_alarm_channel_ok())
	{
		cmd_error_code = CMD_ERR_INVALID_CHANNEL_NUMBER;
		return;
	}

	// Generate filename for profile or envelope values:
	sprintf(cmd_path, "%s%u%s.TXT", type_string, quarter, LOG_channel_id[cmd_channel_index + 1]);

	// If parameter is present, check it's a 144-digit hex string
	if (cmd_equals)
	{
		for (i = 0; i < 144; i++)
		{
			if (STR_parse_hex_digit(cmd_input_ptr[i]) > 0x0F)
			{
				cmd_input_ptr += i;							// report error at the correct char
				cmd_error_code = CMD_ERR_INVALID_VALUE;
				return;
			}
		}

		if (cmd_input_ptr[144] != '\0')
		{
			cmd_input_ptr += 144;							// report error at the correct char
			cmd_error_code = CMD_ERR_COMMAND_TOO_LONG;
			return;
		}

		// else string OK - write to the file, including '\0'
		CFS_write_file((char *)ALM_PROFILES_PATH, cmd_path, "w", cmd_input_ptr, 145);
		ALM_update_profile();
	}

	if (!CFS_read_file((char *)ALM_PROFILES_PATH, cmd_path, STR_buffer, sizeof(STR_buffer)))
		strcpy(STR_buffer, cmd_default_alarm_profile);

	sprintf(cmd_out_ptr, "dA%s%u%s=%s", type_string, quarter, LOG_channel_id[cmd_channel_index + 1], STR_buffer);
}

/******************************************************************************
** Function:	AEH1 command
**
** Notes:
*/
void cmd_aeh1(void)
{
	cmd_ainfo("EH", 1);
}

/******************************************************************************
** Function:	AEH2 command
**
** Notes:
*/
void cmd_aeh2(void)
{
	cmd_ainfo("EH", 2);
}

/******************************************************************************
** Function:	AEH3 command
**
** Notes:
*/
void cmd_aeh3(void)
{
	cmd_ainfo("EH", 3);
}

/******************************************************************************
** Function:	AEH4 command
**
** Notes:
*/
void cmd_aeh4(void)
{
	cmd_ainfo("EH", 4);
}

/******************************************************************************
** Function:	AEL1 command
**
** Notes:
*/
void cmd_ael1(void)
{
	cmd_ainfo("EL", 1);
}

/******************************************************************************
** Function:	AEL2 command
**
** Notes:
*/
void cmd_ael2(void)
{
	cmd_ainfo("EL", 2);
}

/******************************************************************************
** Function:	AEL3 command
**
** Notes:
*/
void cmd_ael3(void)
{
	cmd_ainfo("EL", 3);
}

/******************************************************************************
** Function:	AEL4 command
**
** Notes:
*/
void cmd_ael4(void)
{
	cmd_ainfo("EL", 4);
}

/******************************************************************************
** Function:	AP1 command
**
** Notes:
*/
void cmd_ap1(void)
{
	cmd_ainfo("P", 1);
}

/******************************************************************************
** Function:	AP2 command
**
** Notes:
*/
void cmd_ap2(void)
{
	cmd_ainfo("P", 2);
}

/******************************************************************************
** Function:	AP3 command
**
** Notes:
*/
void cmd_ap3(void)
{
	cmd_ainfo("P", 3);
}

/******************************************************************************
** Function:	AP4 command
**
** Notes:
*/
void cmd_ap4(void)
{
	cmd_ainfo("P", 4);
}

/******************************************************************************
** Function:	Modem AT command
**
** Notes:
*/
void cmd_at(void)
{
#ifndef HDW_PRIMELOG_PLUS
	if (cmd_state == CMD_IDLE)						// send command to modem
	{
		if ((MDM_state == MDM_OFF) || !CAL_build_info.modem || (COM_commissioning_mode == 2))
		{
			cmd_error_code = CMD_ERR_MODEM_OFF;
			return;
		}
		// else:

		cmd_input_ptr -= 2;							// point to beginning of AT command
		*cmd_end_ptr++ = '\r';
		*cmd_end_ptr = '\0';
		strcpy(MDM_tx_buffer, cmd_input_ptr);
		MDM_send_cmd(MDM_tx_buffer);
		MDM_cmd_timer_x20ms = 30 * 50;				// 30s to wait for "OK" reply
		cmd_state = CMD_EXECUTE_AT_COMMAND;
		return;
	}

	// else finished:
	strcpy(cmd_out_ptr, "dAT=");
	cmd_escape_string(MDM_rx_buffer, &cmd_out_ptr[4]);
	cmd_equals = true;								// ensure command gets logged in use.txt
#else
	cmd_error_code = CMD_ERR_UNRECOGNISED_COMMAND;
#endif
}

/******************************************************************************
** Function:	report battery volts as last measured for alarm
**
** Notes:	
*/
void cmd_bv(void)
{
	sprintf(cmd_out_ptr, "dBV=%4.2f,%4.2f,%02X",
		(double)PWR_int_bat_when_failed, (double)PWR_last_ext_supply, PWR_measurement_flags);
}

/******************************************************************************
** Function:	Turn commission mode alarm on or off
**
** Notes:
*/
void cmd_calm(void)
{
	if (cmd_equals)
		cmd_set_bool(&ALM_com_mode_alarm_enable);

	if (cmd_error_code == CMD_ERR_NONE)
		sprintf(cmd_out_ptr, "dCALM=%d", (int)ALM_com_mode_alarm_enable);
}

/******************************************************************************
** Function:	Configure event channel for measurement and derived value
**
** Notes:
*/
void cmd_cec(void)
{
#ifdef HDW_RS485
	cmd_error_code = CMD_ERR_UNRECOGNISED_COMMAND;
#else
	int channel, j;
	DIG_sub_event_config_type * p;

	if (cmd_channel_index >= (2 * CAL_build_info.num_digital_channels))
	{
		cmd_error_code = CMD_ERR_INVALID_CHANNEL_NUMBER;
		return;
	}
	
	channel = cmd_channel_index / 2;
	p = &(DIG_config[channel].ec[cmd_channel_index - (channel * 2)]);									// set up pointer to config memory

	if (cmd_equals)
	{
		cmd_parse_flag(&p->flags, DIG_MASK_CHANNEL_ENABLED);
		cmd_set_uint8(&p->sensor_type);
		cmd_parse_flag(&p->flags, DIG_MASK_MESSAGING_ENABLED);
		cmd_set_uint8(&p->log_interval);
		cmd_set_uint8(&p->min_max_sample_interval);
		cmd_set_uint8(&p->sms_data_interval);
		cmd_set_float(&p->cal);
		cmd_set_uint8(&p->rate_enumeration);
		cmd_set_uint8(&p->sensor_index);
		cmd_set_hex_byte(&p->sms_message_type);
		cmd_set_uint8(&p->description_index);
		cmd_set_uint8(&p->event_units_index);
		cmd_set_uint8(&p->output_units_index);

		DIG_configure_channel(channel);																// reconfigure channel - sets headers and starts or stops logging
	}

	if (cmd_error_code == CMD_ERR_NONE)
		j = sprintf(cmd_out_ptr, "dCEC%s=%d,%u,%d,%u,%u,%u,", &LOG_channel_id[cmd_channel_index + 1][0],
				((p->flags & DIG_MASK_CHANNEL_ENABLED) != 0) ? 1 : 0,
				p->sensor_type,
				((p->flags & DIG_MASK_MESSAGING_ENABLED) != 0) ? 1 : 0,
				p->log_interval, p->min_max_sample_interval, p->sms_data_interval);
		j += STR_print_float(&cmd_out_ptr[j], p->cal);
		j += sprintf(&cmd_out_ptr[j], ",%u,%u,%02X,%u,%u,%u",
				p->rate_enumeration, p->sensor_index, p->sms_message_type, p->description_index, p->event_units_index, p->output_units_index);
#endif
}

/******************************************************************************
** Function:	Config file invalid flags
**
** Notes:
*/
void cmd_cfi(void)
{
	uint8 v, nv;

	if (cmd_equals)
	{
		v = 255;
		nv = 255;
		cmd_set_uint8(&v);
		cmd_set_uint8(&nv);
		if (cmd_error_code != CMD_ERR_NONE)
			return;

		if (v < 2)		// volatile flag supplied
		{
			if (v == 0)
				cmd_dirty_flags &= ~CMD_VOLATILE;
			else
				cmd_dirty_flags |= CMD_VOLATILE;
		}

		if (nv < 2)		// non-volatile flag supplied
		{
			if (nv == 0)
				cmd_dirty_flags &= ~CMD_NON_VOLATILE;
			else
				cmd_dirty_flags |= CMD_NON_VOLATILE;
		}
	}

	sprintf(cmd_out_ptr, "dCFI=%d,%d",
		((cmd_dirty_flags & CMD_VOLATILE) != 0) ? 1 : 0,
		((cmd_dirty_flags & CMD_NON_VOLATILE) != 0) ? 1 : 0);
}

/******************************************************************************
** Function:	Clear RAM bit
**
** Notes:
*/
void cmd_clrb(void)
{
	cmd_set_ram_bit(false);
}

/******************************************************************************
** Function:	Report commissioning mode
**
** Notes:	
*/
void cmd_report_cm(void)
{
	sprintf(cmd_out_ptr, "dCM=%u", COM_commissioning_mode);
}

/******************************************************************************
** Function:	Commissioning mode OFF
**
** Notes:
*/
void cmd_cmoff(void)
{
	// Fix in case modem was stuck in state MDM_SIGN_ON_WAIT
	MDM_recalc_wakeup();

	COM_commissioning_mode = 0;
	COM_cancel_com_mode();
	cmd_report_cm();
}

/******************************************************************************
** Function:	Commissioning mode ON
**
** Notes:
*/
void cmd_cmon(void)
{
	// do not set to 1 if no modem
	if (!CAL_build_info.modem)
		cmd_error_code = CMD_ERR_MODEM_OFF;
	else
	{
		// Fix in case modem was stuck in state MDM_SIGN_ON_WAIT
		MDM_recalc_wakeup();

		COM_commissioning_mode = 1;
		// need to set up re-registration interval
		COM_wakeup_time = 0;
		COM_schedule_control();
		cmd_report_cm();
	}
}

/******************************************************************************
** Function:	Set or report commissioning mode
**
** Notes:
*/
void cmd_cm(void)
{
	uint8 temp;

	if (cmd_equals)
	{
		cmd_set_uint8(&temp);

		// do not set to 1 if no modem
		if ((temp == 1) && !CAL_build_info.modem)
			cmd_error_code = CMD_ERR_MODEM_OFF;
		else
		{
			// Fix in case modem was stuck in state MDM_SIGN_ON_WAIT
			MDM_recalc_wakeup();

			COM_commissioning_mode = temp;
			if (COM_commissioning_mode == 1)
			{
				// need to set up re-registration interval
				COM_wakeup_time = 0;
				COM_schedule_control();
			}
			else if (COM_commissioning_mode >= 2)
			{
				COM_commissioning_mode = 2;
#ifdef HDW_1FM
				if (MAG_logger_on)					// Must do this conditionally, or 1fm_off script may recurse
					MAG_switch_off();
#endif
				COM_cancel_com_mode();
			}
			else							// COM_commissioning_mode == 0
				COM_cancel_com_mode();
		}
	}

	cmd_report_cm();
}

/******************************************************************************
** Function:	Debug LED enable/disable
**
** Notes:		
*/
void cmd_dbg(void)
{
	uint8 temp;

	if (cmd_equals)
	{
		cmd_set_uint8(&temp);
		if (temp > 1)
			cmd_error_code = CMD_ERR_INVALID_VALUE;
		else
		{
			if (temp == 0)
				PWR_drive_debug_led(false);											// drive it off before disabling			
			PWR_enable_debug_led(temp == 1);
			if (temp == 1)
				PWR_drive_debug_led(true);											// drive it on after enabling			
		}
	}

	if (cmd_error_code == CMD_ERR_NONE)
		sprintf(cmd_out_ptr, "dDBG=%u", PWR_debug_led_enabled() ? 1 : 0);
}

/******************************************************************************
** Function:	Digital channel configuration
**
** Notes:		Channel index already set in cmd_channel_index
*/
void cmd_dcc(void)
{
#ifdef HDW_RS485
	cmd_error_code = CMD_ERR_UNRECOGNISED_COMMAND;
#else
	DIG_config_type *p;
	int j;

	if (cmd_channel_index >= CAL_build_info.num_digital_channels)
	{
		cmd_error_code = CMD_ERR_INVALID_CHANNEL_NUMBER;
		return;
	}

	p = &DIG_config[cmd_channel_index];

	if (cmd_equals)
	{
		cmd_error_code = CMD_parse_dcc(p, cmd_input_ptr, cmd_end_ptr);
		if (cmd_error_code == CMD_ERR_NONE)
		{
			DIG_configure_channel(cmd_channel_index);
			ALM_update_profile();		// trigger alarm profile fetch due to changed channel setup
		}
	}

	if (cmd_error_code == CMD_ERR_NONE)
	{
		j = sprintf(cmd_out_ptr, "dDCC%u=", cmd_channel_index + 1);
		j += DIG_print_config(cmd_channel_index, &cmd_out_ptr[j]);
	}
#endif
}

/******************************************************************************
** Function:	Configure doppler damping, amplitude & control bits
**
** Notes:		Also no. of samples
*/
void cmd_ddac(void)
{
#ifndef HDW_RS485_
	cmd_error_code = CMD_ERR_UNRECOGNISED_COMMAND;
#else
	float f;

	if (cmd_equals)
	{
		cmd_set_uint16(&SER_init_damping_sec);
		if (cmd_set_float(&f))						// parse signed value
			SER_init_amp_dbx10 = (int16)f;
		cmd_set_hex(&SER_init_control_bits);
		cmd_set_uint8(&SER_n_samples);
	}

	if (cmd_error_code == CMD_ERR_NONE)
		sprintf(cmd_out_ptr, "dDDAC=%d,%d,%04X,%d", SER_init_damping_sec, SER_init_amp_dbx10,
				SER_init_control_bits, SER_n_samples);
#endif
}

/******************************************************************************
** Function:	Diagnostic command
**
** Notes:
*/
void cmd_diag(void)
{
	sprintf(cmd_out_ptr, "dDIAG=%x.%02x,%u,%u", VER_FIRMWARE_BCD >> 8, VER_FIRMWARE_BCD & 0xFF, 
			COM_sign_on_status, LOG_state);
}

/******************************************************************************
** Function:	Execute the #dir command
**
** Notes:		Just path specified: USB lists all files; SMS just replies.
**				Filename specified: just list file details
*/
void cmd_dir(void)
{
	int i;

	if (!cmd_equals)								// assume root directory
	{
		cmd_path[0] = '\\';
		cmd_path[1] = '\0';
		cmd_filename_ptr = &cmd_path[1];
	}
	else
		cmd_parse_path();							// filename may in fact be a folder

	if ((CFS_state != CFS_OPEN) || (FSchdir(cmd_path) != 0))
	{
		cmd_error_code = CMD_ERR_INVALID_WORKING_DIRECTORY;
		return;
	}

	cmd_srch.attributes = ATTR_DIRECTORY;			// currently in a directory
	if (*cmd_filename_ptr != '\0')					// filename or directory specified
	{
		if (FindFirst(cmd_filename_ptr, ATTR_MASK, &cmd_srch) != 0)
		{
			cmd_error_code = CMD_ERR_FILE_OR_DIRECTORY_NOT_FOUND;
			return;
		}

		if ((cmd_srch.attributes & ATTR_DIRECTORY) != 0)	// it's a directory
		{
			if (FSchdir(cmd_filename_ptr) != 0)
			{
				cmd_error_code = CMD_ERR_INVALID_WORKING_DIRECTORY;
				return;
			}
		}
	}

	// if a path was specified we've CD'd to it.
	// If a filename was specified, we've got its details.
	cmd_get_full_path();		// get full path in STR_buffer
	i = sprintf(cmd_out_ptr, "dDIR=%s", STR_buffer);
	if ((cmd_srch.attributes & ATTR_DIRECTORY) != 0)
	{
		if (cmd_source_index == CMD_SOURCE_USB)
			USB_dir(STR_buffer);
	}
	else												// it's a file
	{
		STR_print_file_timestamp(cmd_srch.timestamp);
		sprintf(cmd_out_ptr + i, ",%s,%ld,%02X",
			STR_buffer, cmd_srch.filesize, cmd_srch.attributes);
	}
}

/******************************************************************************
** Function:	Schedule a script file to be executed, and optionally sets target file for output
**
** Notes:		Script files must be in \CONFIG\ directory, no path should be supplied
**			*** Can detect erroneous path in source filename, but not in target ***
*/
void cmd_do(void)
{
	int i;

	(void)CFS_open();				// keep file system awake

	// if script busy
	if (SCF_progress() != 100)
		cmd_error_code = CMD_ERR_SCRIPT_IN_PROGRESS;
	else if (cmd_equals)
	{
		// parse source filename
		cmd_parse_path();
		if ((cmd_path[1] != '\0') || (*cmd_filename_ptr == '\0'))
			cmd_error_code = CMD_ERR_INVALID_FILENAME;
		else // has a source filename
		{
			// get optional target filename
			if (cmd_set_string(SCF_output_filename, CFS_FILE_NAME_SIZE) == 0)
				*SCF_output_filename = '\0';
			else if (CFS_open())
			{
				if (CFS_state == CFS_OPEN)
				{
					FSchdir((char *)CFS_config_path);
					FSremove(SCF_output_filename);
				}
			}

			// start config script file execution
			if (!SCF_execute((char *)CFS_config_path, cmd_filename_ptr, true))
				cmd_error_code = CMD_ERR_FILE_OR_DIRECTORY_NOT_FOUND;
		}
	}
	else
		cmd_error_code = CMD_ERR_NO_PARAMETERS;

	if (cmd_error_code == CMD_ERR_NONE)
		i = sprintf(cmd_out_ptr, "dDO=%s,%s", cmd_filename_ptr, SCF_output_filename);
}

/******************************************************************************
** Function:	#DPC: digital pulse counts
**
** Notes:
*/
void cmd_dpc(void)
{
#ifdef HDW_RS485
	cmd_error_code = CMD_ERR_UNRECOGNISED_COMMAND;
#else
	if (cmd_channel_index >= CAL_build_info.num_digital_channels)
	{
		cmd_error_code = CMD_ERR_INVALID_CHANNEL_NUMBER;
		return;
	}

	if (cmd_state == CMD_IDLE)							// start off the read process
	{
#if (HDW_NUM_CHANNELS == 9)
		if (cmd_channel_index == 1)
			SNS_read_digital_counters_2 = true;
		else
#endif
		SNS_read_digital_counters = true;
		cmd_state = CMD_READ_SNS_COUNTERS;
	}
	else												// have now read PIC counters
		sprintf(cmd_out_ptr, "dDPC%u=%u,%u",
			cmd_channel_index + 1, SNS_counters.channel_a, SNS_counters.channel_b);
#endif
}

/******************************************************************************
** Function:	#DSC - doppler sensor configure
**
** Notes:
*/
void cmd_dsc(void)
{
#ifndef HDW_RS485_
	cmd_error_code = CMD_ERR_UNRECOGNISED_COMMAND;
#else
	DOP_config_type *p;
	uint8 log_interval;
	uint8 min_max_sample_interval;
	uint8 sms_data_interval;

	p = &DOP_config;
	log_interval = p->log_interval;
	min_max_sample_interval = p->min_max_sample_interval;
	sms_data_interval = p->sms_data_interval;

	if (cmd_equals)
	{
		cmd_parse_flag(&p->sensor_flags, DOP_MASK_SENSOR_ENABLED);
		cmd_set_uint8(&p->sensor_index);
		cmd_parse_flag(&p->sensor_flags, DOP_MASK_DEPTH_SOURCE);
		cmd_set_uint8(&p->depth_data_channel);
		cmd_set_uint8(&log_interval);
		cmd_set_uint8(&min_max_sample_interval);
		cmd_set_uint8(&sms_data_interval);
		if ((log_interval < 9) || (min_max_sample_interval < 9) || (sms_data_interval < 9))	// limit intervals to 1 minute minimum 
			cmd_error_code = CMD_ERR_VALUE_OUT_OF_RANGE;
		if (cmd_error_code == CMD_ERR_NONE)
		{
			p->log_interval = log_interval;
			p->min_max_sample_interval = min_max_sample_interval;
			p->sms_data_interval = sms_data_interval;
			DOP_configure_sensor();
		}
	}
	
	if (cmd_error_code == CMD_ERR_NONE)
	{
		sprintf(cmd_out_ptr, "dDSC=%d,%u,%d,%u,%u,%u,%u",
				((p->sensor_flags & DOP_MASK_SENSOR_ENABLED) != 0) ? 1 : 0,
				p->sensor_index,
				((p->sensor_flags & DOP_MASK_DEPTH_SOURCE) != 0) ? 1 : 0,
				p->depth_data_channel,
				p->log_interval, 
				p->min_max_sample_interval,
				p->sms_data_interval);
	}
#endif
}

/******************************************************************************
** Function:	#DSD - doppler sensor depth configure
**
** Notes:
*/
void cmd_dsd(void)
{
#ifndef HDW_RS485_
	cmd_error_code = CMD_ERR_UNRECOGNISED_COMMAND;
#else
	int j;
	DOP_config_type *p;

	p = &DOP_config;

	if (cmd_equals)
	{
		cmd_parse_flag(&p->flags, DOP_MASK_DEPTH_LOG_ENABLED);
		cmd_parse_flag(&p->flags, DOP_MASK_DEPTH_MESSAGING_ENABLED);
		cmd_set_hex_byte(&p->depth_sms_message_type);
		cmd_set_float(&p->depth_cal);
		cmd_set_uint8(&p->depth_description_index);
		cmd_set_uint8(&p->depth_units_index);
		DOP_configure_sensor();
	}

	if (cmd_error_code == CMD_ERR_NONE)
	{
		j = sprintf(cmd_out_ptr, "dDSD=%d,%d,%02x,",
				((p->flags & DOP_MASK_DEPTH_LOG_ENABLED) != 0) ? 1 : 0,
				((p->flags & DOP_MASK_DEPTH_MESSAGING_ENABLED) != 0) ? 1 : 0,
				p->depth_sms_message_type);
		j += STR_print_float(&cmd_out_ptr[j], p->depth_cal);
		j += sprintf(&cmd_out_ptr[j], ",%u,%u",
				p->depth_description_index, 
				p->depth_units_index);
	}
#endif
}

/******************************************************************************
** Function:	#DSF - doppler sensor flow configure
**
** Notes:
*/
void cmd_dsf(void)
{
#ifndef HDW_RS485_
	cmd_error_code = CMD_ERR_UNRECOGNISED_COMMAND;
#else
	int j;
	DOP_config_type *p;

	p = &DOP_config;

	if (cmd_equals)
	{
		cmd_parse_flag(&p->flags, DOP_MASK_DERIVED_FLOW_LOG_ENABLED);
		cmd_parse_flag(&p->flags, DOP_MASK_DERIVED_FLOW_MESSAGING_ENABLED);
		cmd_set_uint8(&p->flow_pipe_area_source);
		if ((cmd_error_code == CMD_ERR_NONE) && (p->flow_pipe_area_source == 0))
		{
			if (!DOP_retrieve_derived_conversion_data())
				cmd_error_code = CMD_ERR_FILE_READ_LINE_FAILED;
		}
		cmd_set_float(&p->flow_pipe_diameter_m);
		cmd_set_float(&p->sensor_offset_m);
		cmd_set_hex_byte(&p->flow_sms_message_type);
		cmd_set_float(&p->flow_cal);
		cmd_set_uint8(&p->flow_description_index);
		cmd_set_uint8(&p->flow_units_index);
		DOP_configure_sensor();
	}

	if (cmd_error_code == CMD_ERR_NONE)
	{
		j = sprintf(cmd_out_ptr, "dDSF=%d,%d,%u,",
				((p->flags & DOP_MASK_DERIVED_FLOW_LOG_ENABLED) != 0) ? 1 : 0,
				((p->flags & DOP_MASK_DERIVED_FLOW_MESSAGING_ENABLED) != 0) ? 1 : 0,
				p->flow_pipe_area_source);
		j += STR_print_float(&cmd_out_ptr[j], p->flow_pipe_diameter_m);
		cmd_out_ptr[j++] = ',';
		j += STR_print_float(&cmd_out_ptr[j], p->sensor_offset_m);
		j += sprintf(&cmd_out_ptr[j], ",%02x,",
				p->flow_sms_message_type);
		j += STR_print_float(&cmd_out_ptr[j], p->flow_cal);
		j += sprintf(&cmd_out_ptr[j], ",%u,%u",
				p->flow_description_index, 
				p->flow_units_index);
	}
#endif
}

/******************************************************************************
** Function:	#DST - doppler sensor temperature configure
**
** Notes:
*/
void cmd_dst(void)
{
#ifndef HDW_RS485_
	cmd_error_code = CMD_ERR_UNRECOGNISED_COMMAND;
#else
	DOP_config_type *p;

	p = &DOP_config;

	if (cmd_equals)
	{
		cmd_parse_flag(&p->flags, DOP_MASK_TEMPERATURE_LOG_ENABLED);
		cmd_parse_flag(&p->flags, DOP_MASK_TEMPERATURE_MESSAGING_ENABLED);
		cmd_set_hex_byte(&p->temperature_sms_message_type);
		cmd_set_uint8(&p->temperature_description_index);
		cmd_set_uint8(&p->temperature_units_index);
		DOP_configure_sensor();
	}

	if (cmd_error_code == CMD_ERR_NONE)
	{
		sprintf(cmd_out_ptr, "dDST=%d,%d,%02x,%u,%u",
				((p->flags & DOP_MASK_TEMPERATURE_LOG_ENABLED) != 0) ? 1 : 0,
				((p->flags & DOP_MASK_TEMPERATURE_MESSAGING_ENABLED) != 0) ? 1 : 0,
				p->temperature_sms_message_type,
				p->temperature_description_index, 
				p->temperature_units_index);
	}
#endif
}

/******************************************************************************
** Function:	#DSV - doppler sensor velocity configure
**
** Notes:
*/
void cmd_dsv(void)
{
#ifndef HDW_RS485_
	cmd_error_code = CMD_ERR_UNRECOGNISED_COMMAND;
#else
	int j;
	DOP_config_type *p;

	p = &DOP_config;

	if (cmd_equals)
	{
		cmd_parse_flag(&p->flags, DOP_MASK_VELOCITY_LOG_ENABLED);
		cmd_parse_flag(&p->flags, DOP_MASK_VELOCITY_MESSAGING_ENABLED);
		cmd_set_hex_byte(&p->velocity_sms_message_type);
		cmd_set_float(&p->velocity_cal);
		cmd_set_uint8(&p->velocity_description_index);
		cmd_set_uint8(&p->velocity_units_index);
		DOP_configure_sensor();
	}

	if (cmd_error_code == CMD_ERR_NONE)
	{
		j = sprintf(cmd_out_ptr, "dDSV=%d,%d,%02x,",
				((p->flags & DOP_MASK_VELOCITY_LOG_ENABLED) != 0) ? 1 : 0,
				((p->flags & DOP_MASK_VELOCITY_MESSAGING_ENABLED) != 0) ? 1 : 0,
				p->velocity_sms_message_type);
		j += STR_print_float(&cmd_out_ptr[j], p->velocity_cal);
		j += sprintf(&cmd_out_ptr[j], ",%u,%u",
				p->velocity_description_index, 
				p->velocity_units_index);
	}
#endif
}

/******************************************************************************
** Function:	#DT
**
** Notes:
*/
void cmd_dt(void)
{
	uint8 a, b, c;

	if (cmd_equals)
	{
		if (cmd_set_date_bcd(&a, &b, &c))
		{
			if (!RTC_set_date(a, b, c))
				cmd_error_code = CMD_ERR_INVALID_DATE;
		}

		if ((cmd_error_code == CMD_ERR_NONE) && cmd_set_time_bcd(&a, &b, &c))
		{
			RTC_set_time(a, b, c);
			TSYNC_change_clock();
		}
	}

	if (cmd_error_code == CMD_ERR_NONE)
	{
		RTC_get_time_now();
																									// format reply
		sprintf(cmd_out_ptr, "dDT=%02x%02x%02x,%02x:%02x:%02x,%s",
				RTC_now.day_bcd, RTC_now.mth_bcd, RTC_now.yr_bcd,
				RTC_now.hr_bcd, RTC_now.min_bcd, RTC_now.sec_bcd,
				cmd_day_of_week[RTC_now.wkd]);
	}
}

/******************************************************************************
** Function:	ECD command
**
** Notes:
*/
void cmd_ecd(void)
{
#ifdef HDW_RS485
	cmd_error_code = CMD_ERR_UNRECOGNISED_COMMAND;
#else
	int j;

	if (cmd_channel_index >= (2 * CAL_build_info.num_digital_channels))
	{
		cmd_error_code = CMD_ERR_INVALID_CHANNEL_NUMBER;
		return;
	}

	// create filename
	sprintf(cmd_destination, "EVENT%s.TXT", &LOG_channel_id[cmd_channel_index + 1][0]);

	// if equals, write all of remaining input string to event configuration file
	if (cmd_equals)
	{
		strcpy(STR_buffer, cmd_input_ptr);
		j = strlen(STR_buffer);
		if ((j > 0) && (j < sizeof(STR_buffer) - 3) && (STR_buffer[j - 1] != '\n'))
		{
			STR_buffer[j++] = '\r';
			STR_buffer[j++] = '\n';
			STR_buffer[j++] = '\0';
		}

		if (!CFS_write_file((char *)CFS_config_path, (char *)cmd_destination, "w", STR_buffer, j))
			cmd_error_code = CMD_ERR_FILE_WRITE_FAILED;
	}

	// reply with contents of event config file for this channel
	if (cmd_error_code == CMD_ERR_NONE)
	{
		// If file doesn't exist, leave the response blank
		CFS_read_line((char *)CFS_config_path, (char *)cmd_destination, 1, STR_buffer, sizeof(STR_buffer));
		sprintf(cmd_out_ptr, "dEC%s=%s", &LOG_channel_id[cmd_channel_index + 1][0], STR_buffer);
	}
#endif
}

/******************************************************************************
** Function:	Turn echo on or off
**
** Notes:
*/
void cmd_echo(void)
{
	if (cmd_equals)
		cmd_set_bool(&USB_echo);

	if (cmd_error_code == CMD_ERR_NONE)
		sprintf(cmd_out_ptr, "dECHO=%d", (int)USB_echo);
}

/******************************************************************************
** Function:	Control output from event input setup
**
** Notes:
*/
void cmd_eco(void)
{
#ifdef HDW_RS485
	cmd_error_code = CMD_ERR_UNRECOGNISED_COMMAND;
#else
	COP_config_type *p;
	int j;

	if (cmd_channel_index >= COP_NUM_CHANNELS)
	{
		cmd_error_code = CMD_ERR_INVALID_CHANNEL_NUMBER;
		return;
	}

	p = &COP_config[cmd_channel_index];

	if (cmd_equals)
	{																							// parse contents of equals command
		cmd_parse_flag(&p->flags2, COP_MASK_EVENT_ENABLED);
		cmd_set_uint8(&p->event_channel_id);
		if (p->event_channel_id >= (CAL_build_info.num_digital_channels * 2))
			cmd_error_code = CMD_ERR_INVALID_CHANNEL_NUMBER;

		if (cmd_error_code == CMD_ERR_NONE)
			COP_configure_channel(cmd_channel_index);											// act on new configuration
	}

	if (cmd_error_code == CMD_ERR_NONE)
	{
		j = sprintf(cmd_out_ptr, "dECO%u=", cmd_channel_index + 1);
		j+= sprintf(&cmd_out_ptr[j], "%d,%u",
					  ((p->flags2 & COP_MASK_EVENT_ENABLED) != 0x00) ? 1 : 0,
					    p->event_channel_id);
	}
#endif
}

/******************************************************************************
** Function:	E-mail file to recipient
**
** Notes:		A new message is enqueued so the file will be sent in due course
*
void cmd_emf(void)
{
	if (!cmd_equals)
	{
		cmd_error_code = CMD_ERR_NO_PARAMETERS;
		return;
	}

	cmd_set_string(cmd_destination, sizeof(cmd_destination));
	if (!STR_match(cmd_destination, "mail:"))
	{
		cmd_error_code = CMD_ERR_INVALID_DESTINATION;
		return;
	}

	cmd_parse_path();
	if (!CFS_file_exists(cmd_path, cmd_filename_ptr))
		cmd_error_code = CMD_ERR_FILE_OR_DIRECTORY_NOT_FOUND;
	else
	{
		// Generate message (type = file)
		cmd_get_full_path();
		MSG_send('F', STR_buffer, &cmd_destination[5]);

		// Generate reply
		sprintf(cmd_out_ptr, "dEMF=%s,%s", cmd_destination, STR_buffer);
	}
} */

/******************************************************************************
** Function:	Delete file
**
** Notes:
*/
void cmd_fdel(void)
{
	if (!cmd_equals)
	{
		cmd_error_code = CMD_ERR_NO_PARAMETERS;
		return;
	}

	cmd_parse_path();
	cmd_get_full_path();

	(void)CFS_open();				// keep file system awake
	if (CFS_state != CFS_OPEN)
		cmd_error_code = CMD_ERR_FILE_DELETE_FAILED;
	else if (FSchdir((cmd_path[0] == '\0') ? "\\" : cmd_path) != 0)
		cmd_error_code = CMD_ERR_INVALID_WORKING_DIRECTORY;
	else if (FSremove(cmd_filename_ptr) != 0)
		cmd_error_code = CMD_ERR_FILE_DELETE_FAILED;
	else
		sprintf(cmd_out_ptr, "dFDEL=%s", STR_buffer);
}

/******************************************************************************
** Function:	Read file
**
** Notes:		SMS: just transmit as much of file as poss. USB/FTP: read whole file
*/
void cmd_frd(void)
{
	FSFILE *f;
	int i;

	if (!cmd_equals)
	{
		cmd_error_code = CMD_ERR_NO_PARAMETERS;
		return;
	}

	cmd_parse_path();

	(void)CFS_open();
	if (CFS_state != CFS_OPEN)													// check file system
	{
		cmd_error_code = CMD_ERR_FILE_OR_DIRECTORY_NOT_FOUND;
		return;
	}

	if (FSchdir(cmd_path) != 0)													// can't set working directory
		cmd_error_code = CMD_ERR_INVALID_WORKING_DIRECTORY;
	else
	{
		f = FSfopen(cmd_filename_ptr, "r");
		if (f == NULL)
			cmd_error_code = CMD_ERR_INVALID_FILENAME;
		else
		{
			cmd_get_full_path();												// reply
			i = sprintf(cmd_out_ptr, "dFRD=%s,%ld", STR_buffer, f->size);
			// go do it
			if (cmd_source_index == CMD_SOURCE_USB)
				USB_transfer_file(cmd_path, cmd_filename_ptr, false);
			else if (cmd_source_index == CMD_SOURCE_FTP)
			{
				CFS_close_file(f);												// close the file first
				cmd_error_code = FTP_frd_send(cmd_path, cmd_filename_ptr);		// because this command opens the outbox for writing
				return;															// all done so escape
			}
			else																// else SMS
			{
				FSfread(STR_buffer, CMD_MAX_LENGTH, 1, f);	// ignore return code
				if (f->size < CMD_MAX_LENGTH)
					STR_buffer[f->size] = '\0';
				else
					STR_buffer[CMD_MAX_LENGTH] = '\0';
				cmd_out_ptr[i++] = ',';
				cmd_escape_string(STR_buffer, &cmd_out_ptr[i]);
			}
			CFS_close_file(f);
		}
	}
}

/******************************************************************************
** Function:	Read specific line from file
**
** Notes:
*/
void cmd_frl(void)
{
	int i;
	uint16 line_number;

	if (!cmd_equals)
	{
		cmd_error_code = CMD_ERR_NO_PARAMETERS;
		return;
	}

	(void)CFS_open();
	if (CFS_state != CFS_OPEN)													// check file system
	{
		cmd_error_code = CMD_ERR_FILE_OR_DIRECTORY_NOT_FOUND;
		return;
	}

	line_number = 1;
	cmd_parse_path();
	cmd_set_uint16(&line_number);

	cmd_get_full_path();
	i = sprintf(cmd_out_ptr, "dFRL=%s,%u,", STR_buffer, line_number);
	if (CFS_read_line(cmd_path, cmd_filename_ptr, line_number, STR_buffer, sizeof(STR_buffer)) < 1)
		cmd_error_code = CMD_ERR_FILE_READ_LINE_FAILED;
	else
		cmd_escape_string(STR_buffer, &cmd_out_ptr[i]);
}

/******************************************************************************
** Function:	Check health of file system
**
** Notes:
*/
void cmd_fsh(void)
{
	if (cmd_equals)
		cmd_set_uint8(&CFS_first_assert);

	if (cmd_error_code == 0)
		sprintf(cmd_out_ptr, "dFSH=%d,%d,%d,%d,%d",
			CFS_first_assert, CFS_last_assert, CFS_init_counter, CFS_open_counter, CFS_state);
}

/******************************************************************************
** Function:	Write string or input buffer to file
**
** Notes:		Adds \r\n at the end if there is none
*/
void cmd_file_write(char *write_mode, bool string_supplied)
{
	int i;
	int j;

	if (!cmd_equals)
	{
		cmd_error_code = CMD_ERR_NO_PARAMETERS;
		return;
	}

	(void)CFS_open();
	if (CFS_state != CFS_OPEN)													// check file system
	{
		cmd_error_code = CMD_ERR_FILE_WRITE_FAILED;
		return;
	}

	cmd_parse_path();
	cmd_get_full_path();

	if (string_supplied)
	{
		i = sprintf(cmd_out_ptr, "dF%s=%s,", (*write_mode == 'a') ? "AS" : "WS", STR_buffer);

		j = cmd_set_string(STR_buffer, sizeof(STR_buffer));
		if ((j > 0) && (j < sizeof(STR_buffer) - 3) && (STR_buffer[j - 1] != '\n'))
		{
			STR_buffer[j++] = '\r';
			STR_buffer[j++] = '\n';
			STR_buffer[j] = '\0';
		}

		if (!CFS_write_file(cmd_path, cmd_filename_ptr, write_mode, STR_buffer, j))
			cmd_error_code = CMD_ERR_FILE_WRITE_FAILED;
		else
			cmd_escape_string(STR_buffer, &cmd_out_ptr[i]);
	}
	else if ((cmd_source_index == CMD_SOURCE_USB) || (cmd_source_index == CMD_SOURCE_FTP))	// no string: write from USB input
	{
		if (*write_mode == 'w')						// delete file first, if it exists
		{
			if (FSchdir(cmd_path) == 0)				// working directory OK
				FSremove(cmd_filename_ptr);			// ignore whether file exists
			else
				cmd_error_code = CMD_ERR_INVALID_WORKING_DIRECTORY;
		}

		if (cmd_error_code == CMD_ERR_NONE)
		{
			sprintf(cmd_out_ptr, "dF%s=%s", (*write_mode == 'a') ? "AP" : "WR", STR_buffer);
			if (cmd_source_index == CMD_SOURCE_USB)
				USB_transfer_file(cmd_path, cmd_filename_ptr, true);
			else
				COM_ftp_transfer_file(cmd_path, cmd_filename_ptr);
		}
	}
	else
		cmd_error_code = CMD_ERR_REQUIRES_USB;
}

/******************************************************************************
** Function:	Append USB input to file
**
** Notes:		Can't be a macro, as belongs in the command table
*/
void cmd_fap(void)
{
	cmd_file_write("a", false);
}

/******************************************************************************
** Function:	Append string to file
**
** Notes:		Can't be a macro, as belongs in the command table
*/
void cmd_fas(void)
{
	cmd_file_write("a", true);
}

/******************************************************************************
** Function:	FTP configure
**
** Notes:
*/
void cmd_ftpc(void)
{
	char * p1;
	char * p2;
	int i;

	// get existing FTP logon command + encrypted password into STR_buffer[256]
	// if no file present create dummy contents
	if (!CFS_read_file((char *)CFS_config_path, (char *)FTP_logon_filename, &STR_buffer[256], 128))
		strcpy(&STR_buffer[256], "at#ftpopen=,,,1\r\n");

	// assemble new AT command into STR_buffer
	if (!cmd_equals)							// use the old one
		i = sprintf(STR_buffer, "%s", &STR_buffer[256]);
	else
	{
		// get p1 & p2 pointing to 1st & 2nd commas in existing FTP logon command:
		p1 = strchr(&STR_buffer[256], ',');
		if (p1 == NULL)		// fix corrupt existing command
		{
			strcpy(&STR_buffer[256], "at#ftpopen=,,,1\r\n");
			p1 = &STR_buffer[267];
		}

		p2 = strchr(p1 + 1, ',');
		if (p2 == NULL)		// fix corrupt existing command
		{
			p2 = p1 + 1;
			strcpy(p2, ",,1\r\n");
		}

		// get new or existing server_name:port
		if (cmd_field_null())				// leave server_name:port as-is
		{
			*p1 = '\0';						// string-terminate existing server_name:port
			i = sprintf(STR_buffer, "at#ftpopen=%s,", &STR_buffer[267]);
		}
		else								// get new server_name:port
		{
			strcpy(STR_buffer, "at#ftpopen=");
			i = 11 + cmd_set_string(&STR_buffer[11], 80);
			STR_buffer[i++] = ',';
		}

		// get new or existing user id
		if (cmd_field_null())				// leave user id as-is
		{
			*p2 = '\0';						// string-terminate existing user id
			i += sprintf(&STR_buffer[i], "%s,", p1 + 1);
		}
		else								// get new user id
		{
			i += cmd_set_string(&STR_buffer[i], 80);
			STR_buffer[i++] = ',';
		}

		// Now re-use P1 to point to end of password:
		p1 = strchr(p2 + 1, ',');
		if (p1 == NULL)		// fix corrupt existing command
		{
			p1 = p2 + 1;
			strcpy(p1, ",1\r\n");
		}

		// get new password & encrypt, or get existing encrypted password
		if (cmd_field_null())								// leave encrypted password as-is
		{
			*p1 = '\0';										// string terminate existing encrypted PW
			i += sprintf(&STR_buffer[i], "%s", p2 + 1);
		}
		else												// get new password & encrypt
		{
			(void)cmd_set_string(&STR_buffer[i], 18);
			i += FTP_encrypt_password(&STR_buffer[i]);
		}

		// get new active/passive flag, or use existing, or default to 1.
		if (!cmd_field_null())								// new flag supplied
			p1 = cmd_input_ptr;								// point to new one
		else												// no new flag supplied
			p1++;											// point to existing one

		// default to passive mode FTP if new or existing flag invalid
		i += sprintf(&STR_buffer[i], ",%s\r\n", (*p1 == '0') ? "0" : "1");

		cmd_error_code = FTP_set_logon();
	}

	// do reply
	if (cmd_error_code == CMD_ERR_NONE)
	{
		// knock off the "\r\n":
		STR_buffer[i - 2] = '\0';
		sprintf(cmd_out_ptr, "dFTPC=%s", &STR_buffer[11]);
	}
}

/******************************************************************************
** Function:	Send file to ftp server
**
** Notes:
*/
void cmd_ftx(void)
{
	uint32 length = 0;

	(void)CFS_open();															// keep file system awake
	if (CFS_state != CFS_OPEN)													// check file system
	{
		cmd_error_code = CMD_ERR_FILE_OR_DIRECTORY_NOT_FOUND;
		return;
	}

	// check equals
	if (cmd_equals)
	{
		// parse path and filename
		cmd_parse_path();

		if (FSchdir((cmd_path[0] == '\0') ? "\\" : cmd_path) != 0)
			cmd_error_code = CMD_ERR_INVALID_WORKING_DIRECTORY;
		else																	// parse data length
			cmd_set_uint32(&length);
	}

	// do reply
	if (cmd_error_code == CMD_ERR_NONE)
	{
		sprintf(cmd_out_ptr, "dFTX=%s\\%s,%ld",
				cmd_path, cmd_filename_ptr, length);
		if (cmd_equals)															// go do it
			cmd_error_code = FTP_frd_send(cmd_path, cmd_filename_ptr);
	}
}

/******************************************************************************
** Function:	Write USB input to file
**
** Notes:		Can't be a macro, as belongs in the command table
*/
void cmd_fwr(void)
{
	cmd_file_write("w", false);
}

/******************************************************************************
** Function:	Write string to file
**
** Notes:		Can't be a macro, as belongs in the command table
*/
void cmd_fws(void)
{
	cmd_file_write("w", true);
}

/******************************************************************************
** Function:	GPS control and read
**
** Notes:
*/
void cmd_gps(void)
{
#ifndef HDW_GPS
	cmd_error_code = CMD_ERR_UNRECOGNISED_COMMAND;
#else
	uint8 state, code;
	
	if (cmd_equals)
	{
		cmd_set_uint8(&code);
		if (cmd_error_code == CMD_ERR_NONE)
		{
			if ((code & 0x02) != 0)
				GPS_config.truck_mode = true;
			else if (code == 0)						// #gps=0,... is the only way to clear truck mode
				GPS_config.truck_mode = false;

			if ((code & 1) == 0)
				GPS_off();
			else									// turn on, and leave truck mode as already set
			{
				if (COM_commissioning_mode == 2)
					cmd_error_code = CMD_ERR_VALUE_OUT_OF_RANGE;
				else
					GPS_on();
			}
		}

		if (!cmd_field_null())
			cmd_set_time_bcd(&GPS_config.trigger_time.hr_bcd, &GPS_config.trigger_time.min_bcd, NULL);

		GPS_recalc_wakeup();	// do this unconditionally in case we have just changed truck mode
	}

	// do reply
	if (cmd_error_code == CMD_ERR_NONE)
	{
		state = GPS_is_on ? 1 : 0;
		if (GPS_config.truck_mode)
			state |= 0x02;
		sprintf(cmd_out_ptr, "dGPS=%u,%02x:%02x,%s,%s,%s,%s,%s,%s",
			state, GPS_config.trigger_time.hr_bcd, GPS_config.trigger_time.min_bcd, GPS_time, GPS_latitude, GPS_NS, GPS_longitude, GPS_EW, GPS_fix);
	}
#endif
}

/******************************************************************************
** Function:	Immediate Derived Values
**
** Notes:
*/
void cmd_idv(void)
{
	int i, j;

	if (cmd_state == CMD_IDLE)															// start off the read process
	{
		HDW_BATTEST_INT_ON = true;
		HDW_BATTEST_EXT_ON = true;

		cmd_20ms_timer = 2;
		cmd_state = CMD_IDV_POWER;

#ifndef HDW_GPS
		if (!ANA_power_transducer_required())	// start analogue channel reads
		{
  #if (HDW_NUM_CHANNELS == 9)																// 9-channel: read all analogues
			for (i = 0; i < ANA_NUM_CHANNELS; i++)
  #else																					// 3-channel: read main channels only, not shadows
			for (i = 0; i < 2; i++)
  #endif
			{
				if (ANA_channel_exists(i) && ((ANA_config[i].flags & ANA_MASK_CHANNEL_ENABLED) != 0))
					ANA_start_adc_read(i);
			}
		}
#endif
	}
	else																				// read process finished
	{
																						// Digitals:
#ifdef HDW_RS485
		sprintf(STR_buffer, ",,,,");													// null fields for digitals
#else
		DIG_print_immediate_derived_values(0, false);
#endif
		j = sprintf(cmd_out_ptr, "dIDV=%s", STR_buffer);
#ifdef HDW_RS485
		j += sprintf(&cmd_out_ptr[j], ",,,,");											// null fields for digitals
  #if (HDW_NUM_CHANNELS == 9)
		for (i = 0; i < ANA_NUM_CHANNELS; i++)											// 9 channel Analogues:
  #else
		for (i = 0; i < 2; i++)															// 3 channel Analogues:
  #endif
#else
  #if (HDW_NUM_CHANNELS == 9)															// 9-channel: read D2A & D2B plus all analogues
		DIG_print_immediate_derived_values(1, false);
		j += sprintf(&cmd_out_ptr[j], "%s", STR_buffer);

		for (i = 0; i < ANA_NUM_CHANNELS; i++)											// 9 channel Analogues:
  #else																					// 3-channel: no D2A or D2B, + analogue channels only, not shadows
		j += sprintf(&cmd_out_ptr[j], ",,,,");											// null fields for digitals

		for (i = 0; i < 2; i++)															// 3 channel Analogues:
  #endif
#endif
		{
#ifndef HDW_GPS
			if ((LOG_state > LOG_STOPPED) && ((ANA_config[i].flags & ANA_MASK_CHANNEL_ENABLED) != 0) && ((ANA_config[i].flags & ANA_MASK_DERIVED_DATA_ENABLED) != 0))
			{
				j += STR_print_float(&cmd_out_ptr[j], cmd_get_print_value(ANA_channel[i].derived_sample_value));
				j += sprintf(&cmd_out_ptr[j], ",%u,", ANA_config[i].derived_units_index);
			}
			else
#endif
				j += sprintf(&cmd_out_ptr[j], ",,");
		}

#if (HDW_NUM_CHANNELS == 3)																// 3-channel: null the shadow & non-implemented channels
		j += sprintf(&cmd_out_ptr[j], ",,,,,,,,,,");
#endif

		sprintf(&cmd_out_ptr[j], "%4.2f,%4.2f,%4.2f",									// Power +  GSM signal strength as a fraction
			(double)PWR_int_bat_volts, (double)PWR_ext_supply_volts, (double)COM_csq / 31.0);
	}
}

/******************************************************************************
** Function:	Immediate Values
**
** Notes:
*/
void cmd_imv(void)
{
	int i, j;

	if (cmd_state == CMD_IDLE)															// start off the read process
	{
		PWR_set_pending_batt_test(false);
		cmd_state = CMD_IMV_POWER;

#ifndef HDW_GPS
		if (!ANA_power_transducer_required())	// start analogue channel reads
		{
  #if (HDW_NUM_CHANNELS == 9)																// 9-channel: read all analogues
			for (i = 0; i < ANA_NUM_CHANNELS; i++)
  #else																					// 3-channel: read main channels only, not shadows
			for (i = 0; i < 2; i++)
  #endif
			{
				if (ANA_channel_exists(i) && ((ANA_config[i].flags & ANA_MASK_CHANNEL_ENABLED) != 0))
					ANA_start_adc_read(i);
			}
		}
#endif
	}
	else																				// read process finished
	{
																						// Digitals:
#ifdef HDW_RS485
		sprintf(STR_buffer, ",,,,");													// null fields for digitals
#else
		DIG_print_immediate_values(0, false);
#endif
		j = sprintf(cmd_out_ptr, "dIMV=%s", STR_buffer);
#ifdef HDW_RS485
		j += sprintf(&cmd_out_ptr[j], ",,,,");											// null fields for digitals
  #if (HDW_NUM_CHANNELS == 9)
		for (i = 0; i < ANA_NUM_CHANNELS; i++)											// 9 channel Analogues:
  #else
		for (i = 0; i < 2; i++)															// 3 channel Analogues:
  #endif
#else
  #if (HDW_NUM_CHANNELS == 9)															// 9-channel: read D2A & D2B plus all analogues
		DIG_print_immediate_values(1, false);
		j += sprintf(&cmd_out_ptr[j], "%s", STR_buffer);

		for (i = 0; i < ANA_NUM_CHANNELS; i++)											// 9 channel Analogues:
  #else																					// 3-channel: no D2A or D2B, + analogue channels only, not shadows
		j += sprintf(&cmd_out_ptr[j], ",,,,");											// null fields for digitals

		for (i = 0; i < 2; i++)															// 3 channel Analogues:
  #endif
#endif
		{
#ifndef HDW_GPS
			if ((LOG_state > LOG_STOPPED) && ((ANA_config[i].flags & ANA_MASK_CHANNEL_ENABLED) != 0))
			{
				j += STR_print_float(&cmd_out_ptr[j], cmd_get_print_value(ANA_channel[i].sample_value));
				j += sprintf(&cmd_out_ptr[j], ",%u,", ANA_config[i].units_index);
			}
			else
#endif
				j += sprintf(&cmd_out_ptr[j], ",,");
		}

#if (HDW_NUM_CHANNELS == 3)																// 3-channel: null the shadow & non-implemented channels
		j += sprintf(&cmd_out_ptr[j], ",,,,,,,,,,");
#endif

		sprintf(&cmd_out_ptr[j], "%4.2f,%4.2f,%4.2f",									// Power +  GSM signal strength as a fraction
			(double)PWR_int_bat_volts, (double)PWR_ext_supply_volts, (double)COM_csq / 31.0);
	}
}

/******************************************************************************
** Function:	Immediate Serial Port Values
**
** Notes:
*/
void cmd_isv(void)
{
#ifndef HDW_RS485
	cmd_error_code = CMD_ERR_UNRECOGNISED_COMMAND;
#else
	int j;

	j = sprintf(cmd_out_ptr, "dISV=");
	if ((LOG_state > LOG_STOPPED) && ((DOP_config.sensor_flags & DOP_MASK_SENSOR_ENABLED) != 0) && DOP_sensor_present)
	{
		if ((DOP_config.flags & DOP_MASK_VELOCITY_LOG_ENABLED) != 0)
		{
			j += STR_print_float(&cmd_out_ptr[j], cmd_get_print_value(DOP_channel.velocity_value));
			j += sprintf(&cmd_out_ptr[j], ",%u,", DOP_config.velocity_units_index);
		}
		else
			j += sprintf(&cmd_out_ptr[j], ",,");
		if ((DOP_config.flags & DOP_MASK_TEMPERATURE_LOG_ENABLED) != 0)
		{
			j += STR_print_float(&cmd_out_ptr[j], cmd_get_print_value(DOP_channel.temperature_value));
			j += sprintf(&cmd_out_ptr[j], ",%u,", DOP_config.temperature_units_index);
		}
		else
			j += sprintf(&cmd_out_ptr[j], ",,");
		if ((DOP_config.flags & DOP_MASK_DEPTH_LOG_ENABLED) != 0)
		{
			j += STR_print_float(&cmd_out_ptr[j], cmd_get_print_value(DOP_channel.depth_value));
			j += sprintf(&cmd_out_ptr[j], ",%u,", DOP_config.depth_units_index);
		}
		else
			j += sprintf(&cmd_out_ptr[j], ",,");
		if ((DOP_config.flags & DOP_MASK_DERIVED_FLOW_LOG_ENABLED) != 0)
		{
			j += STR_print_float(&cmd_out_ptr[j], cmd_get_print_value(DOP_channel.derived_flow_value));
			j += sprintf(&cmd_out_ptr[j], ",%u,", DOP_config.flow_units_index);
		}
		else
			j += sprintf(&cmd_out_ptr[j], ",,");
	}
	else
		j += sprintf(&cmd_out_ptr[j], ",,,,,,,,");

	sprintf(&cmd_out_ptr[j], "%4.2f,%4.2f,%4.2f",												// Power +  GSM signal strength as a fraction
			(double)PWR_int_bat_volts, (double)PWR_ext_supply_volts, (double)COM_csq / 31.0);
#endif
}

/******************************************************************************
** Function:	#LI
**
** Notes:		*LI=<Logger type string>,<App Version>,<Bootloader Version>,<Sensor PIC Version>,<Ser no.>,
**				<Num digital channels>,<Num analogue channels>,<Num internal pressure channels>,<Modem flag>
** Side-effect: The build file is loaded
*/
void cmd_li(void)
{
	uint32 disc_size;
	int j;


		// check for hardware/software mismatch
		// if HDW_PRIMELOG_PLUS - can only be 3 channel version 3 with no modem in build file
#ifdef HDW_PRIMELOG_PLUS
		if (CAL_build_info.modem || (HDW_NUM_CHANNELS != 3))
		{
			cmd_error_code = CMD_ERR_INCOMPATIBLE_HARDWARE;
			return;
		}
#endif
		// if HDW_RS485 - cannot work on issue 3 boards
#ifdef HDW_DBG
#ifndef WIN32
#warning HDW_revision detection DISABLED. FOR DEBUG ONLY. (HDW_DBG)
#endif
#else
#ifdef HDW_RS485
		if (HDW_revision == 0)
		{
			cmd_error_code = CMD_ERR_INCOMPATIBLE_HARDWARE;
			return;
		}
#endif
#endif

	if (cmd_state == CMD_IDLE)							// start off the read process
	{
		// Cause 9-ch app to report wrong sensor PIC 2 version on 3-ch HW
		SNS_version = 0;

#if (HDW_NUM_CHANNELS == 9)
		if (cmd_channel_index == 1)
			SNS_read_version_2 = true;
		else
#endif
			SNS_read_version = true;

		cmd_state = CMD_READ_LI_INFO;
	}
	else												// have now read sensor PIC version
	{
		STR_buffer[0] = '\0';							// clear STR_buffer
		CAL_read_build_info();							// puts logger type & pressure rating in STR_buffer
//		FRM_init();

		j = sprintf(cmd_out_ptr, (cmd_channel_index == 0) ? "dLI=" : "dLI2=");

#if (HDW_NUM_CHANNELS == 3)
		if (cmd_channel_index == 1)		// #LI2 on 3-ch logger
			SNS_version = 0;			// report sensor PIC 2 version as 0.0
#endif

		// NB: the first string parameter is actually 2 fields: logger type + pressure rating
		j += sprintf(&cmd_out_ptr[j], "%s,%d,%x.%02x,",
			STR_buffer, CAL_build_info.modem, VER_FIRMWARE_BCD >> 8, VER_FIRMWARE_BCD & 0xFF);

		// Read bootloader version no. from locations 0x040C & 040E in program memory
		TBLPAG = 0x0000;
		STR_buffer[0] = (char)__builtin_tblrdl(0x040C);			// character of major rev. no.
		if ((STR_buffer[0] >= '0') && (STR_buffer[0] <= '9'))	// Bootloader present
		{
			STR_buffer[1] = '.';
			STR_buffer[2] = (char)__builtin_tblrdl(0x040E);		// character of minor rev. no.
			STR_buffer[3] = '\0';
		}
		else													// bootloader not present
			STR_buffer[0] = '\0';

		j += sprintf(&cmd_out_ptr[j], "%s,%x.%x,%s,%d,%s,%d,",
			STR_buffer, SNS_version >> 4, SNS_version & 0x0F, CAL_build_info.serial_number,
			CAL_build_info.num_digital_channels, CAL_build_info.analogue_channel_types,
			HDW_NUM_CHANNELS);

		disc_size = 0;									// by default
		if (CFS_state == CFS_OPEN)
		{
			disc_size = gDiskData.sectorSize * gDiskData.SecPerClus;	// = cluster size in bytes
			disc_size >>= 12;											// = cluster size in 4KB units
			disc_size *= gDiskData.maxcls;								// = disc size in 4KB units
		}

		if (disc_size == 0)
			j += sprintf(&cmd_out_ptr[j], "card fault,");
		else
			j += sprintf(&cmd_out_ptr[j], "%lu,", disc_size);

		cmd_out_ptr[j++] = HDW_REV + '0';
		cmd_out_ptr[j++] = ',';
#ifdef HDW_RS485
		//cmd_out_ptr[j++] = 'S';	// doppler
		cmd_out_ptr[j++] = 'M';		// modbus
#else
	#ifdef HDW_GPS
		cmd_out_ptr[j++] = 'G';
	#else
		cmd_out_ptr[j++] = CAL_build_info.digital_wiring_option;
	#endif
#endif
		cmd_out_ptr[j++] = ',';
		if ((PWR_eco_config.flags & PWR_ECO_PRODUCT) != 0x00)
			cmd_out_ptr[j++] = 'E';
		else
			cmd_out_ptr[j++] = 'N';
		j += sprintf(&cmd_out_ptr[j], ",%d", CAL_build_info.num_control_outputs);
		cmd_out_ptr[j] = '\0';
	}
}

/******************************************************************************
** Function:	Logging control command
**
** Notes:
*/
void cmd_log(void)
{
	if (cmd_equals)
	{
		cmd_set_date_bcd(&LOG_config.start.day_bcd, &LOG_config.start.mth_bcd, &LOG_config.start.yr_bcd);
		cmd_set_time_bcd(&LOG_config.start.hr_bcd, &LOG_config.start.min_bcd, NULL);
		cmd_set_date_bcd(&LOG_config.stop.day_bcd, &LOG_config.stop.mth_bcd, &LOG_config.stop.yr_bcd);
		cmd_set_time_bcd(&LOG_config.stop.hr_bcd, &LOG_config.stop.min_bcd, NULL);
		cmd_set_uint32(&LOG_config.min_space_remaining);
		if (cmd_error_code == CMD_ERR_NONE)
		{
			if (RTC_start_stop_event(&LOG_config.start))	// start time past or present
				RTC_set_start_stop_now(&LOG_config.start);

			if (RTC_start_stop_event(&LOG_config.stop))		// stop time past or present
				RTC_set_start_stop_now(&LOG_config.stop);

			ALM_update_profile();		// trigger alarm profile fetch
		}
	}

	// do reply
	if (cmd_error_code == CMD_ERR_NONE)
		sprintf(cmd_out_ptr, "dLOG=%02X%02X%02X,%02X:%02X,%02X%02X%02X,%02X:%02X,%lu",
			LOG_config.start.day_bcd, LOG_config.start.mth_bcd, LOG_config.start.yr_bcd,
			LOG_config.start.hr_bcd, LOG_config.start.min_bcd,
			LOG_config.stop.day_bcd, LOG_config.stop.mth_bcd, LOG_config.stop.yr_bcd,
			LOG_config.stop.hr_bcd, LOG_config.stop.min_bcd,
			LOG_config.min_space_remaining);
}

/******************************************************************************
** Function:	Make directory
**
** Notes:
*/
void cmd_mkdir(void)
{
	if (!cmd_equals)
	{
		cmd_error_code = CMD_ERR_NO_PARAMETERS;
		return;
	}
	// else:

	cmd_parse_path();
	cmd_get_full_path();		// get full path in STR_buffer

	(void)CFS_open();			// keep file system awake
	if ((CFS_state != CFS_OPEN) || (FSmkdir(STR_buffer) != 0))
		cmd_error_code = CMD_ERR_FAILED_TO_CREATE_DIRECTORY;
	else
		sprintf(cmd_out_ptr, "dMKDIR=%s", STR_buffer);
}

/******************************************************************************
** Function:	modem power schedule
**
** Notes:		to specify what time on what days of the week
**				the modem will switch on and wait for communications
*/
void cmd_mps(void)
{
	COM_schedule_type * p;
	int j;

	p = &COM_schedule;

	if (cmd_equals)
	{
		cmd_set_bool(&p->ftp_enable);
		cmd_set_bool(&p->batch_enable);
		cmd_set_time_bcd(&p->tx_window.start.hr_bcd, &p->tx_window.start.min_bcd, NULL);
		cmd_set_time_bcd(&p->tx_window.stop.hr_bcd, &p->tx_window.stop.min_bcd, NULL);
		cmd_set_binary(&p->tx_window.day_mask, 7);
		cmd_set_uint8(&p->tx_window.interval);
		cmd_set_time_bcd(&p->rx_window.start.hr_bcd, &p->rx_window.start.min_bcd, NULL);
		cmd_set_time_bcd(&p->rx_window.stop.hr_bcd, &p->rx_window.stop.min_bcd, NULL);
		cmd_set_binary(&p->rx_window.day_mask, 7);
		cmd_set_uint8(&p->rx_window.interval);	
		cmd_set_time_bcd(&p->modem_standby.start.hr_bcd, &p->modem_standby.start.min_bcd, NULL);
		cmd_set_time_bcd(&p->modem_standby.stop.hr_bcd, &p->modem_standby.stop.min_bcd, NULL);
		cmd_set_binary(&p->modem_standby.day_mask, 7);
		cmd_set_uint8(&p->modem_standby.interval);												// used to be &p->default_ftp_poll_interval
		cmd_set_bool(&p->tx_oldest_first);
		cmd_set_uint8(&p->_1fm_standby_mins);

		if (cmd_error_code == CMD_ERR_NONE)
		{
			if (!p->ftp_enable)
				p->modem_standby.interval = 0;

			COM_recalc_wakeups();																		// recalculate wake up times
			MDM_recalc_wakeup();
			FTP_reset_active_retrieval_info();															// reset ftp partial file retrieval
#ifndef HDW_GPS
			ANA_synchronise();																			// synchronise logging
#endif
#ifndef HDW_RS485
			DIG_synchronise();
#endif										
			MDM_preset_state_flag();																	// preset modem state flag to preserve modem condition before time change
		}
	}

	// do reply
	if (cmd_error_code == CMD_ERR_NONE)
	{
		cmd_convert_flags_to_string(STR_buffer, p->tx_window.day_mask, 7);
		j = sprintf(cmd_out_ptr, "dMPS=%d,%d,%02x:%02x,%02x:%02x,%s,%u,",
			p->ftp_enable, p->batch_enable,
			p->tx_window.start.hr_bcd, p->tx_window.start.min_bcd,
			p->tx_window.stop.hr_bcd, p->tx_window.stop.min_bcd,
			STR_buffer, p->tx_window.interval);

		cmd_convert_flags_to_string(STR_buffer, p->rx_window.day_mask, 7);
		j += sprintf(&cmd_out_ptr[j], "%02x:%02x,%02x:%02x,%s,%u,",
			p->rx_window.start.hr_bcd, p->rx_window.start.min_bcd,
			p->rx_window.stop.hr_bcd, p->rx_window.stop.min_bcd,
			STR_buffer, p->rx_window.interval);

		cmd_convert_flags_to_string(STR_buffer, p->modem_standby.day_mask, 7);
		j += sprintf(&cmd_out_ptr[j], "%02x:%02x,%02x:%02x,%s,%u,%d,%u",
			p->modem_standby.start.hr_bcd, p->modem_standby.start.min_bcd,
			p->modem_standby.stop.hr_bcd, p->modem_standby.stop.min_bcd,
			STR_buffer, p->modem_standby.interval, p->tx_oldest_first,
			p->_1fm_standby_mins);
	}
}

/******************************************************************************
** Function:	#MSG
**
** Notes:		#MSG=<Comms type & destination>,<Message>
**				Comms type: sms:Phone number, pdu:phone number, ftp, monitor channel,
**				or null field to reply to sender only.
*/
void cmd_msg(void)
{
	char * p;
	int i;

	if (cmd_equals)
	{
		cmd_parse_destination();
		if (cmd_error_code == CMD_ERR_NONE)
			cmd_set_string(STR_buffer, sizeof(STR_buffer));
	}
	else
	{
		cmd_destination[0] = '\0';
		STR_buffer[0] = '\0';
	}

	if (cmd_error_code == CMD_ERR_NONE)
	{
		p = (*STR_buffer == '\0') ? (char *)cmd_test_string : STR_buffer;
		i = sprintf(cmd_out_ptr, "dMSG=%s,", cmd_destination);
		cmd_escape_string(p, &cmd_out_ptr[i]);

		if (STR_match(cmd_destination, "sms:"))
		{
			// Flush message to outbox immediately, so outbox buffer can be re-used for reply
			MSG_send(MSG_TYPE_SMS_TEXT, p, &cmd_destination[4]);
			MSG_flush_outbox_buffer(true);
		}
		else if (STR_match(cmd_destination, "pdu:"))				// PB 8.7.09
		{
			// send PDU test message
			PDU_test_message( p, &cmd_destination[4]);
		}
		else if (STR_match(cmd_destination, "ftp:"))
		{
			// Flush message to outbox immediately, so outbox buffer can be re-used for reply
			MSG_send(MSG_TYPE_FTP_MSG, p, &cmd_destination[4]);
			MSG_flush_outbox_buffer(true);
		}
		else if (STR_match(cmd_destination, "monitor"))
		{
			i = (int)USB_echo;
			USB_echo = true;
			USB_monitor_string(p);
			USB_echo = (bool)i;
		}
	}
}

/******************************************************************************
** Function:	Set or read sitename and id
**
** Notes:
*/
void cmd_name(void)
{
	int i;

	if (cmd_equals)
	{
		// check for null field
		if (!cmd_field_null())
			cmd_set_string(COM_sitename, sizeof(COM_sitename));
	}

	// reply
	i = sprintf(cmd_out_ptr, "dNAME=");
	cmd_escape_string(COM_sitename, &cmd_out_ptr[i]);
}

/******************************************************************************
** Function:	Generic command for talking to the Nivus sensor
**
** Notes:		
*/
void cmd_nv(char command, int timeout_x20ms)
{
#ifndef HDW_RS485_
	cmd_error_code = CMD_ERR_UNRECOGNISED_COMMAND;
#else
	if (DOP_busy())
		cmd_error_code = CMD_ERR_NIVUS_BUSY;
	else
	{
		DOP_nivus_code = command;						// remember reply character 
		DOP_start_sensor();
		cmd_20ms_timer = timeout_x20ms;
		cmd_state = CMD_NIVUS;
	}
#endif
}

/******************************************************************************
** Function:	NIVUS sensor test - set baudrate
**
** Notes:		if echo is on then NIVUS communications are echoed in hex ascii
*/
void cmd_nvb(void)
{
	cmd_nv('B', 2 * 50);
}

/******************************************************************************
** Function:	NIVUS sensor test - write height 
**
** Notes:		if echo is on then NIVUS communications are echoed in hex ascii
*/
void cmd_nvh(void)
{
	cmd_nv('H', 2 * 50);
}

/******************************************************************************
** Function:	NIVUS sensor test - get id
**
** Notes:		if echo is on then NIVUS communications are echoed in hex ascii
*/
void cmd_nvi(void)
{
	cmd_nv('I', 2 * 50);
}

/******************************************************************************
** Function:	NIVUS sensor test - read measure
**
** Notes:		if echo is on then NIVUS communications are echoed in hex ascii
*/
void cmd_nvm(void)
{
#ifndef HDW_RS485_
	cmd_error_code = CMD_ERR_UNRECOGNISED_COMMAND;
#else
	cmd_nv('M', (SER_n_samples + 2) * 50);
#endif
}

/******************************************************************************
** Function:	NIVUS sensor test - read init
**
** Notes:		if echo is on then NIVUS communications are echoed in hex ascii
*/
void cmd_nvr(void)
{
	cmd_nv('R', 2 * 50);
}

/******************************************************************************
** Function:	NIVUS sensor test - reset sensor
**
** Notes:		if echo is on then NIVUS communications are echoed in hex ascii
*/
void cmd_nvt(void)
{
	cmd_nv('T', 2 * 50);
}

/******************************************************************************
** Function:	NIVUS sensor test - sound velocity
**
** Notes:		if echo is on then NIVUS communications are echoed in hex ascii
*/
void cmd_nvv(void)
{
	cmd_nv('V', 2 * 50);
}

/******************************************************************************
** Function:	NIVUS sensor test - write init
**
** Notes:		if echo is on then NIVUS communications are echoed in hex ascii
*/
void cmd_nvw(void)
{
	cmd_nv('W', 2 * 50);
}

/******************************************************************************
** Function:	NIVUS sensor test - common finish
**
** Notes:
*/
void cmd_nv_finish(void)
{
#ifdef HDW_RS485_
	int j;
	int16  temperature;

	if (cmd_20ms_timer == 0)													// if timed out
		cmd_error_code = CMD_ERR_SERIAL_TIMED_OUT;								// timed out error
	else if (DOP_nivus_code == 'F')												// else if NIVUS comms failed
		cmd_error_code = CMD_ERR_SERIAL_COMMS_FAIL;								// failed error
	else
	{																			// reply
		if (DOP_nivus_code == 'I')												// if NVI
			sprintf(cmd_out_ptr, "dNVI=%s", SER_id);							// reply with id
		else if (DOP_nivus_code == 'M')											// if NVM
		{																		// reply with measurement results
			j = sprintf(cmd_out_ptr, "dNVM=%dmm/s,%u%%,%ddB,",
				SER_water_speed_mmps, SER_quality, SER_amplification/10);
			temperature = SER_temperature;
			if (temperature < 0)
			{
				cmd_out_ptr[j] = '-';
				j++;
				temperature = -temperature;
			}
			sprintf(&cmd_out_ptr[j], "%d.%ddegC,%dmm,%04X,%02X",
			temperature/10,	temperature%10, SER_level_mm, SER_pressure, SER_status);
		}
		else
			sprintf(cmd_out_ptr, "dNV%c", DOP_nivus_code);						
	}
#endif
	cmd_state = CMD_IDLE;
}

/******************************************************************************
** Function:	Network test results
**
** Notes:
*/
void cmd_nwres(void)
{
	// reply with contents of file ACTIVITY\nwres.txt
	if ((CFS_state != CFS_OPEN) || (FSchdir((char *)CFS_activity_path) != 0))			// can't set working directory
		cmd_error_code = CMD_ERR_FILE_OR_DIRECTORY_NOT_FOUND;
	else
	{
		if (!CFS_read_file((char *)CFS_activity_path, (char *)CFS_nwres_name, (char *)STR_buffer, 256))
			cmd_error_code = CMD_ERR_FILE_OR_DIRECTORY_NOT_FOUND;
		else
		{
			// truncate file contents to 151 chars
			STR_buffer[151] = '\0';
			sprintf(cmd_out_ptr, "dNWRES=%c,%s", COM_nwtest_progress, STR_buffer);
		}
	}
}

/******************************************************************************
** Function:	Network test start
**
** Notes:
*/
void cmd_nwtst(void)
{
	// set default values
	COM_nwtst_delay = 60;
	if (cmd_equals)
	{
		// parse parameters
		cmd_set_uint16(&COM_nwtst_delay);
		// limit
		if (COM_nwtst_delay < 1)
			COM_nwtst_delay = 1;
		else if (COM_nwtst_delay > 3600)
			COM_nwtst_delay = 3600;
	}

	// do reply
	if (cmd_error_code == CMD_ERR_NONE)
	{
		// log outgoing command
		sprintf(cmd_out_ptr, "dNWTST=%u", COM_nwtst_delay);

		(void)CFS_open();											// keep file system awake
		if (CFS_state != CFS_OPEN)									// check file system
		{
			cmd_error_code = CMD_ERR_FILE_OR_DIRECTORY_NOT_FOUND;
			return;
		}

		// start results file
		// if no ACTIVITY directory
		if (FSchdir((char *)CFS_activity_path) != 0)
		{
			// create it
    		if (FSmkdir((char *)CFS_activity_path) != 0) return;
			FSchdir((char *)CFS_activity_path);
		}
		// create initial network and results file contents
		// first line of networks.txt is source of command in text
		if (cmd_source_index == CMD_SOURCE_USB)
			sprintf(STR_buffer, "USB\r\n");
		else if (cmd_source_index == CMD_SOURCE_SMS)
			sprintf(STR_buffer, "SMS\r\n");
		else if (cmd_source_index == CMD_SOURCE_FTP)
			sprintf(STR_buffer, "FTP\r\n");
		else
			sprintf(STR_buffer, "SCRIPT\r\n");
		if (CFS_open())
		{
			// write it to file
			if (CFS_write_file((char *)CFS_activity_path, (char *)CFS_networks_name, "w", STR_buffer, strlen(STR_buffer)))
			{
				// create initial file contents
				sprintf(STR_buffer, "%02x%02x%02x,%02x:%02x:%02x",
						RTC_now.day_bcd, RTC_now.mth_bcd, RTC_now.yr_bcd,
						RTC_now.hr_bcd, RTC_now.min_bcd, RTC_now.sec_bcd);
				// write it to file
				if (CFS_write_file((char *)CFS_activity_path, (char *)CFS_nwres_name, "w", STR_buffer, strlen(STR_buffer)))
				{
					// start test
					COM_initiate_nw_test();
				}
			}
		}
	}
}

/******************************************************************************
** Function:	Set or read pump flow rate table
**
** Notes:
*/
void cmd_pfr(void)
{
#ifdef HDW_RS485
	cmd_error_code = CMD_ERR_UNRECOGNISED_COMMAND;
#else
	int i,j,limit;

  #if (HDW_NUM_CHANNELS == 3)
	limit = 4;
  #else
	limit = 16;
  #endif

	if (cmd_equals)
	{
		for (i = 0; i < limit; i++)
			cmd_set_float(&DIG_pfr_table[i]);
	}
	if (cmd_error_code == CMD_ERR_NONE)
	{
		j = sprintf(cmd_out_ptr, "dPFR=");
		for (i = 0; i < limit; i++)
		{
			j += STR_print_float(&cmd_out_ptr[j], DIG_pfr_table[i]);
			if (i < (limit - 1))
				cmd_out_ptr[j++] = ',';
		}
	}
#endif
}

/******************************************************************************
** Function:	Set or read pump running times
**
** Notes:
*/
void cmd_prt(void)
{
#ifdef HDW_RS485
	cmd_error_code = CMD_ERR_UNRECOGNISED_COMMAND;
#else
	int i,j,limit;
	uint8 minutes;

  #if (HDW_NUM_CHANNELS == 3)
	limit = 2;
  #else
	limit = 4;
  #endif

	if (cmd_equals)
	{
		for (i = 0; i < limit; i++)
		{
			if (!cmd_field_null())
			{
				cmd_set_uint32(&DIG_system_pump[i].running_time.hrs);
				DIG_system_pump[i].running_time.secs = 0;												// zero seconds if either hours or minutes set
			}
			if (!cmd_field_null())
			{
				cmd_set_uint8(&minutes);																// limit check minutes
				if (minutes < 60)
				{
					DIG_system_pump[i].running_time.mins = minutes;
					DIG_system_pump[i].running_time.secs = 0;
				}
				else
				{
					cmd_error_code = CMD_ERR_VALUE_OUT_OF_RANGE;
					i = limit;
				}
			}
			if (cmd_error_code == CMD_ERR_NONE)															// if setting was successful
			{
				if (DIG_system_pump[i].on)																// if pump is on set turn on time to now
					DIG_system_pump[i].time_on_500ms = (RTC_time_sec << 1) + RTC_half_sec;
			}
		}
	}
	if (cmd_error_code == CMD_ERR_NONE)
	{
		j = sprintf(cmd_out_ptr, "dPRT=");
		for (i = 0; i < limit; i++)
		{
			j+= sprintf(&cmd_out_ptr[j], "%lu,%u", DIG_system_pump[i].running_time.hrs, DIG_system_pump[i].running_time.mins);
			if (i < (limit - 1))
				cmd_out_ptr[j++] = ',';
		}
	}
#endif
}

/******************************************************************************
** Function:	Read RAM
**
** Notes:
*/
void cmd_ramr(void)
{
	if (cmd_equals && cmd_set_hex(&cmd_address))
	{
		if (sscanf(cmd_input_ptr, "%d", &cmd_byte_count) != 1)
			cmd_error_code = CMD_ERR_INVALID_VALUE;
	}
	else
		cmd_error_code = CMD_ERR_INVALID_VALUE;

	cmd_generate_ramr_reply();
}

/******************************************************************************
** Function:	Write RAM
**
** Notes:
*/
void cmd_ramw(void)
{
	uint8 * address;
	uint8 high_nibble;
	uint8 low_nibble;

	if (cmd_equals && cmd_set_hex(&cmd_address))
	{
		address = (uint8 *)cmd_address;
		cmd_byte_count = 0;
		while (cmd_input_ptr != cmd_end_ptr)
		{
			high_nibble = STR_parse_hex_digit(*cmd_input_ptr++);
			if (high_nibble > 0x0F)
				break;

			low_nibble = STR_parse_hex_digit(*cmd_input_ptr++);
			if (low_nibble > 0x0F)
				break;

			*address++ = (high_nibble << 4) | low_nibble;
			cmd_byte_count++;
		}

		cmd_generate_ramr_reply();
	}
	else
		cmd_error_code = CMD_ERR_INVALID_VALUE;
}

/******************************************************************************
** Function:	Regular control output
**
** Notes:
*/
void cmd_rco(void)
{
	COP_config_type *p;
	int j;
	uint8 gate;

	if (cmd_channel_index >= COP_NUM_CHANNELS)
	{
		cmd_error_code = CMD_ERR_INVALID_CHANNEL_NUMBER;
		return;
	}

	p = &COP_config[cmd_channel_index];

	if (cmd_equals)
	{																							// parse contents of equals command
		cmd_parse_flag(&p->flags2, COP_MASK_REGULAR_ENABLED);
		if (cmd_set_hex_byte(&gate))															// event gate - limit check
		{
			if (((gate & 0x0f) < 0x05) && ((gate & 0xf0) < 0x02))
				p->regular_event_gate = gate;
			else
			{
				cmd_error_code = CMD_ERR_VALUE_OUT_OF_RANGE;
				return;
			}
		}
		cmd_set_time_bcd(&p->regular_window.start.hr_bcd, &p->regular_window.start.min_bcd, NULL);
		cmd_set_time_bcd(&p->regular_window.stop.hr_bcd, &p->regular_window.stop.min_bcd, NULL);
//		cmd_set_binary(&p->regular_window.day_mask, 7);												// commented out - may be needed TBD
		cmd_set_uint8(&p->regular_window.interval);

		if (cmd_error_code == CMD_ERR_NONE)
			COP_recalc_wakeups();																// recalculate wake up times
	}

	if (cmd_error_code == CMD_ERR_NONE)
	{
		j = sprintf(cmd_out_ptr, "dRCO%u=", cmd_channel_index + 1);
		j+= sprintf(&cmd_out_ptr[j], "%d,%02x,%02x:%02x,%02x:%02x,%u",
					  ((p->flags2 & COP_MASK_REGULAR_ENABLED) != 0x00) ? 1 : 0,
					  p->regular_event_gate,
					  p->regular_window.start.hr_bcd, p->regular_window.start.min_bcd,
					  p->regular_window.stop.hr_bcd, p->regular_window.stop.min_bcd,
					  p->regular_window.interval);
	}
}

/******************************************************************************
** Function:	RDA
**
** Notes:
*/
void cmd_rda(void)
{
	int i, j;

	if (cmd_channel_index == 0)																	// #RDA or #RDA1
	{
		if (cmd_state == CMD_IDLE)																// start off the read process
		{
			PWR_set_pending_batt_test(false);
			cmd_state = CMD_RDA_POWER;

#ifndef HDW_GPS
			if (!ANA_power_transducer_required())	// start analogue channel reads
			{
  #if (HDW_NUM_CHANNELS == 9)																		// 9-channel: read analogues A1 to A3 for RDA1
				for (i = 0; i < 3; i++)
  #else																							// 3-channel: read main channels only, not shadows
				for (i = 0; i < 2; i++)
  #endif
				{
					if (ANA_channel_exists(i))
					{
						if ((ANA_config[i].flags & ANA_MASK_CHANNEL_ENABLED) != 0)
							ANA_start_adc_read(i);
					}
				}
			}
#endif
		}
		else
		{
#ifndef HDW_RS485
			DIG_print_immediate_values(0, true);												// Sitename, Digital D1A and D1B:
			j = sprintf(cmd_out_ptr, "dRDA1=%s,%s", COM_sitename, STR_buffer);
#else
			j = sprintf(cmd_out_ptr, "dRDA1=%s,,,", COM_sitename);
#endif
																								// Analogues A1 to A3:
#if (HDW_NUM_CHANNELS == 9)																		// 9-channel: print analogues A1 to A3 for RDA1
			for (i = 0; i < 3; i++)
#else																							// 3-channel: print main channels only, not shadows
			for (i = 0; i < 2; i++)
#endif
			{
#ifndef HDW_GPS
				if ((LOG_state > LOG_STOPPED) && ((ANA_config[i].flags & ANA_MASK_CHANNEL_ENABLED) != 0))
				{
					j += sprintf(&cmd_out_ptr[j], "%s=", LOG_channel_id[i + LOG_ANALOGUE_1_INDEX]);
					j += STR_print_float(&cmd_out_ptr[j], cmd_get_print_value(ANA_channel[i].sample_value));
					CFS_read_line("\\Config", "Units.txt", ANA_config[i].units_index + 1, STR_buffer, 8);
					j += sprintf(&cmd_out_ptr[j], "%s,", STR_buffer);
				}
				else
#endif
					j += sprintf(&cmd_out_ptr[j], ",");
			}

#if (HDW_NUM_CHANNELS == 3)																		// add null field for A3 (shadow channels not reported)
			j += sprintf(&cmd_out_ptr[j], ",");
#endif
			sprintf(&cmd_out_ptr[j], "%4.2f,%4.2f,%d%%",										// Power +  GSM signal strength as a %:
					(double)PWR_int_bat_volts, (double)PWR_ext_supply_volts, (COM_csq * 100) / 31);
		}
	}
	else if (cmd_channel_index == 1)															// #RDA2
	{
#if (HDW_NUM_CHANNELS == 9)																		// 9-channel: read analogues A4 to A7 for RDA2
		// start off the read process if necessary
		if ((cmd_state == CMD_IDLE) && !ANA_power_transducer_required())
		{
			for (i = 3; i < 7; i++)																// Analogues A4 to A7:
			{
				if (ANA_channel_exists(i) && ((ANA_config[i].flags & ANA_MASK_CHANNEL_ENABLED) != 0))
					ANA_start_adc_read(i);
			}
			cmd_state = CMD_RDA_ANA;
		}
		else
		{
  #ifndef HDW_RS485
			DIG_print_immediate_values(1, true);
			j = sprintf(cmd_out_ptr, "dRDA2=%s,%s", COM_sitename, STR_buffer);
  #else
			j = sprintf(cmd_out_ptr, "dRDA2=%s,,,", COM_sitename);
  #endif
			for (i = 3; i < 7; i++)																// Analogues A4 to A7:
			{
				if ((LOG_state > LOG_STOPPED) && ((ANA_config[i].flags & ANA_MASK_CHANNEL_ENABLED) != 0))
				{
					j += sprintf(&cmd_out_ptr[j], "%s=", LOG_channel_id[i + LOG_ANALOGUE_1_INDEX]);
					j += STR_print_float(&cmd_out_ptr[j], cmd_get_print_value(ANA_channel[i].sample_value));
					CFS_read_line("\\Config", "Units.txt", ANA_config[i].units_index + 1, STR_buffer, 8);
					j += sprintf(&cmd_out_ptr[j], "%s,", STR_buffer);
				}
				else
					j += sprintf(&cmd_out_ptr[j], ",");
			}
			cmd_out_ptr[j - 1] = '\0';															// delete last comma
		}
#else																							// 3-channel - no RDA2
		cmd_error_code = CMD_ERR_VALUE_OUT_OF_RANGE;
#endif
	}
	else if (cmd_channel_index == 2)															// #RDA3
	{																							// derived channels for D1A and D1B and A1 - A3
		if (cmd_state == CMD_IDLE)																// start off the read process
		{
			HDW_BATTEST_INT_ON = true;
			HDW_BATTEST_EXT_ON = true;
			cmd_20ms_timer = 2;
			cmd_state = CMD_RDA_POWER;

#ifndef HDW_GPS
  #if (HDW_NUM_CHANNELS == 9)																		// 9-channel: read analogues A1 to A3 for RDA1
			for (i = 0; i < 3; i++)
  #else																							// 3-channel: read main channels only, not shadows
			for (i = 0; i < 2; i++)
  #endif
			{
				if (ANA_channel_exists(i))
				{
					if ((ANA_config[i].flags & ANA_MASK_CHANNEL_ENABLED) != 0)
						ANA_start_adc_read(i);
				}
			}
#endif
		}
		else
		{
#ifndef HDW_RS485
			DIG_print_immediate_derived_values(0, true);										// Sitename, Digital D1A and D1B:
			j = sprintf(cmd_out_ptr, "dRDA3=%s,%s", COM_sitename, STR_buffer);
#else
			j = sprintf(cmd_out_ptr, "dRDA3=%s,,,", COM_sitename);
#endif
																								// Analogues A1 to A3:
#if (HDW_NUM_CHANNELS == 9)																		// 9-channel: print analogues A1 to A3 for RDA1
			for (i = 0; i < 3; i++)
#else																							// 3-channel: print main channels only, not shadows
			for (i = 0; i < 2; i++)
#endif
			{
#ifndef HDW_GPS
				if ((LOG_state > LOG_STOPPED) && ((ANA_config[i].flags & ANA_MASK_CHANNEL_ENABLED) != 0) && ((ANA_config[i].flags & ANA_MASK_DERIVED_DATA_ENABLED) != 0))
				{
					j += sprintf(&cmd_out_ptr[j], "%s=", LOG_channel_id[i + LOG_ANALOGUE_1_INDEX + 11]);
					j += STR_print_float(&cmd_out_ptr[j], cmd_get_print_value(ANA_channel[i].derived_sample_value));
					CFS_read_line("\\Config", "Units.txt", ANA_config[i].derived_units_index + 1, STR_buffer, 8);
					j += sprintf(&cmd_out_ptr[j], "%s,", STR_buffer);
				}
				else
#endif
					j += sprintf(&cmd_out_ptr[j], ",");
			}

#if (HDW_NUM_CHANNELS == 3)																		// add null field for A3 (shadow channels not reported)
			j += sprintf(&cmd_out_ptr[j], ",");
#endif
			sprintf(&cmd_out_ptr[j], "%4.2f,%4.2f,%d%%",										// Power +  GSM signal strength as a %:
					(double)PWR_int_bat_volts, (double)PWR_ext_supply_volts, (COM_csq * 100) / 31);
		}
	}
	else if (cmd_channel_index == 3)															// #RDA4
	{																							//derived channels for A4 - A7
#if (HDW_NUM_CHANNELS == 9)																		// 9-channel: read analogues A4 to A7 for RDA2
		if (cmd_state == CMD_IDLE)																// start off the read process
		{
			for (i = 3; i < 7; i++)																// Analogues A4 to A7:
			{
				if (ANA_channel_exists(i) && ((ANA_config[i].flags & ANA_MASK_CHANNEL_ENABLED) != 0))
					ANA_start_adc_read(i);
			}
			cmd_state = CMD_RDA_ANA;
		}
		else
		{
  #ifndef HDW_RS485
			DIG_print_immediate_derived_values(1, true);
			j = sprintf(cmd_out_ptr, "dRDA4=%s,%s", COM_sitename, STR_buffer);
  #else
			j = sprintf(cmd_out_ptr, "dRDA4=%s,,,", COM_sitename);
  #endif
			for (i = 3; i < 7; i++)																// Analogues A4 to A7:
			{
#ifndef HDW_GPS
				if ((LOG_state > LOG_STOPPED) && ((ANA_config[i].flags & ANA_MASK_CHANNEL_ENABLED) != 0) && ((ANA_config[i].flags & ANA_MASK_DERIVED_DATA_ENABLED) != 0))
				{
					j += sprintf(&cmd_out_ptr[j], "%s=", LOG_channel_id[i + LOG_ANALOGUE_1_INDEX + 11]);
					j += STR_print_float(&cmd_out_ptr[j], cmd_get_print_value(ANA_channel[i].derived_sample_value));
					CFS_read_line("\\Config", "Units.txt", ANA_config[i].derived_units_index + 1, STR_buffer, 8);
					j += sprintf(&cmd_out_ptr[j], "%s,", STR_buffer);
				}
				else

#endif
					j += sprintf(&cmd_out_ptr[j], ",");
			}
			cmd_out_ptr[j - 1] = '\0';															// delete last comma
		}
#else																							// 3-channel - no RDA2
		cmd_error_code = CMD_ERR_VALUE_OUT_OF_RANGE;
#endif
	}
	else if (cmd_channel_index == 4)															// #RDA5 - serial port channels
	{
#ifndef HDW_RS485
		cmd_error_code = CMD_ERR_VALUE_OUT_OF_RANGE;
#else
		j = sprintf(cmd_out_ptr, "dRDA5=%s,", COM_sitename);
		if ((LOG_state > LOG_STOPPED) && ((DOP_config.sensor_flags & DOP_MASK_SENSOR_ENABLED) != 0) && DOP_sensor_present)
		{
			if ((DOP_config.flags & DOP_MASK_VELOCITY_LOG_ENABLED) != 0)
			{
				j += sprintf(&cmd_out_ptr[j], "%s=", LOG_channel_id[1]);
				j += STR_print_float(&cmd_out_ptr[j], cmd_get_print_value(DOP_channel.velocity_value));
				CFS_read_line("\\Config", "Units.txt", DOP_config.velocity_units_index + 1, STR_buffer, 8);
				j += sprintf(&cmd_out_ptr[j], "%s,", STR_buffer);
			}
			else
				j += sprintf(&cmd_out_ptr[j], ",");
			if ((DOP_config.flags & DOP_MASK_TEMPERATURE_LOG_ENABLED) != 0)
			{
				j += sprintf(&cmd_out_ptr[j], "%s=", LOG_channel_id[2]);
				j += STR_print_float(&cmd_out_ptr[j], cmd_get_print_value(DOP_channel.temperature_value));
				CFS_read_line("\\Config", "Units.txt", DOP_config.temperature_units_index + 1, STR_buffer, 8);
				j += sprintf(&cmd_out_ptr[j], "%s,", STR_buffer);
			}
			else
				j += sprintf(&cmd_out_ptr[j], ",");
			if ((DOP_config.flags & DOP_MASK_DEPTH_LOG_ENABLED) != 0)
			{
				j += sprintf(&cmd_out_ptr[j], "%s=", LOG_channel_id[3]);
				j += STR_print_float(&cmd_out_ptr[j], cmd_get_print_value(DOP_channel.depth_value));
				CFS_read_line("\\Config", "Units.txt", DOP_config.depth_units_index + 1, STR_buffer, 8);
				j += sprintf(&cmd_out_ptr[j], "%s", STR_buffer);
			}
		}
		else
			j += sprintf(&cmd_out_ptr[j], ",,");
#endif
	}
	else if (cmd_channel_index == 5)															// #RDA6
	{
#ifndef HDW_RS485
		cmd_error_code = CMD_ERR_VALUE_OUT_OF_RANGE;
#else
		j = sprintf(cmd_out_ptr, "dRDA6=%s,", COM_sitename);
		if ((LOG_state > LOG_STOPPED) && ((DOP_config.sensor_flags & DOP_MASK_SENSOR_ENABLED) != 0) && DOP_sensor_present)
		{
			if ((DOP_config.flags & DOP_MASK_DERIVED_FLOW_LOG_ENABLED) != 0)
			{
				j += sprintf(&cmd_out_ptr[j], "%s=", LOG_channel_id[12]);
				j += STR_print_float(&cmd_out_ptr[j], cmd_get_print_value(DOP_channel.derived_flow_value));
				CFS_read_line("\\Config", "Units.txt", DOP_config.flow_units_index + 1, STR_buffer, 8);
				j += sprintf(&cmd_out_ptr[j], "%s", STR_buffer);
			}
		}
#endif
	}
}

/******************************************************************************
** Function:	read hardware revision
**
** Notes:		undocumented test command
*/
void cmd_rhr(void)
{
	if (cmd_error_code == CMD_ERR_NONE)
		sprintf(cmd_out_ptr, "dRHR=%d", (int)HDW_revision);
}

/******************************************************************************
** Function:	reset from power on
**
** Notes:		must be #reset=1 or #reset=9
*/
void cmd_reset(void)
{
	uint8 value;

	// check equals
	if (cmd_equals)
	{
		cmd_set_uint8(&value);
		if (cmd_error_code != CMD_ERR_NONE)
			return;
		// else:

		if (value == 1)			// deferred reset
		{
			COM_reset_logger = COM_RESET_LOGGER_KEY;
			sprintf(cmd_out_ptr, "dRESET=1");
			return;
		}
		else if (value == 9)	// reset now
		{
			if (cmd_source_index == CMD_SOURCE_USB)
				COM_reset();
			else
				cmd_error_code = CMD_ERR_REQUIRES_USB;
		}
	}
	// else:

	cmd_error_code = CMD_ERR_INVALID_VALUE;
}

/******************************************************************************
** Function:	retrieve FTP data
**
** Notes:		sends all logged data for a particular day in one ftp file
**				This is a one shot command to send a file over ftp
*/
void cmd_rfd(void)
{
	RTC_type file_date;
	uint16	 normal_files_present, derived_files_present;

	if (cmd_equals)																				// check equals
	{
		cmd_set_date_bcd(&file_date.day_bcd, &file_date.mth_bcd, &file_date.yr_bcd);			// parse date
		file_date.reg32[0] = 0;
	}
	normal_files_present = FTP_flag_normal_files_present(&file_date);							// check that files are present for this date
	derived_files_present = FTP_flag_derived_files_present(&file_date);
	if ((normal_files_present == 0) && (derived_files_present == 0))
		cmd_error_code = CMD_ERR_FILE_OR_DIRECTORY_NOT_FOUND;

	if (cmd_error_code == CMD_ERR_NONE)															// do reply
	{
		sprintf(cmd_out_ptr, "dRFD=%02x%02x%02x",
				file_date.day_bcd, file_date.mth_bcd, file_date.yr_bcd);
		if (normal_files_present != 0)															// go do it depending on files found
			FTP_retrieve_data(normal_files_present, &file_date);
		if (derived_files_present != 0)
			FTP_retrieve_data(derived_files_present | 0x8000, &file_date);						// set top bit of file flags for derived data
	}
}

/******************************************************************************
** Function:	Remove directory
**
** Notes:		2nd Parameter = 1 if OK to remove non-empty folder.
*/
void cmd_rmdir(void)
{
	bool flag;

	if (!cmd_equals)
	{
		cmd_error_code = CMD_ERR_NO_PARAMETERS;
		return;
	}
	// else:
	// set root directory - work around problem with FSrmdir
	cmd_path[0] = '\\';
	cmd_path[1] = '\0';
	cmd_filename_ptr = &cmd_path[1];

	(void)CFS_open();						// keep file system awake
	if (CFS_state != CFS_OPEN)													// check file system
	{
		cmd_error_code = CMD_ERR_FILE_OR_DIRECTORY_NOT_FOUND;
		return;
	}

	if (FSchdir(cmd_path) == 0)
	{
		flag = false;
		cmd_parse_path();
		cmd_set_bool(&flag);

		if (cmd_error_code == CMD_ERR_NONE)
		{
			cmd_get_full_path();		// get full path in STR_buffer

			if (FSrmdir(STR_buffer, flag) != 0)
				cmd_error_code = CMD_ERR_FAILED_TO_REMOVE_DIRECTORY;
			else
				sprintf(cmd_out_ptr, "dRMDIR=%s,%d", STR_buffer, (int)flag);
		}
	}
	else
		cmd_error_code = CMD_ERR_FAILED_TO_REMOVE_DIRECTORY;
}

/******************************************************************************
** Function:	Roaming control
**
** Notes:		Sets flag for enabling a roaming SIM, and ID of network in use.
** If roaming is enabled, setting ID to 0 will force a roam at next modem sign-on.
** If roaming is diabled, setting a non-zero ID will force the modem to always
** use the specified network.
*/
void cmd_roam(void)
{
	if (cmd_equals)
	{
		cmd_set_bool(&COM_roaming_enabled);
		if (cmd_error_code == CMD_ERR_NONE)
			cmd_set_uint32(&COM_gsm_network_id);
	}

	if (cmd_error_code == CMD_ERR_NONE)
		sprintf(cmd_out_ptr, "dROAM=%d,%lu", COM_roaming_enabled, COM_gsm_network_id);
}

/******************************************************************************
** Function:	retrieve SMS data
**
** Notes:		This is a one shot command to resend an old data transmission
*/
void cmd_rsd(void)
{
	PDU_sms_retrieve_type *p;
	int  j;
	bool to_host;
	char flag = '\0';

	p = &PDU_rsd_retrieve;
	to_host = true;				// by default

	// check equals
	if (cmd_equals)
	{
		cmd_error_code = CMD_parse_rsd(p, cmd_input_ptr, cmd_end_ptr);
		if (cmd_error_code == CMD_ERR_NONE)
		{
			cmd_next_field();
			if (!cmd_field_null())
			{
				flag = *cmd_input_ptr;
				to_host = ((flag == 'H') || (flag == 'h'));
			}
		}
	}
	// do reply
	if (cmd_error_code == CMD_ERR_NONE)
	{
		(void)CFS_open();															// keep file system awake

		j = sprintf(cmd_out_ptr, "dRSD=%s,%02x%02x%02x,%02x:%02x",
				LOG_channel_id[p->channel + 1],
				p->when.day_bcd, p->when.mth_bcd, p->when.yr_bcd,
				p->when.hr_bcd, p->when.min_bcd);
		if (flag != '\0')
			j+= sprintf(&cmd_out_ptr[j], ",%c", flag);

		// go do it
		cmd_error_code = PDU_retrieve_data(to_host);
	}
}

/******************************************************************************
** Function:	Set control output
**
** Notes:
*/
void cmd_sco(void)
{
	COP_config_type *p;
	int j;

	if (cmd_channel_index >= COP_NUM_CHANNELS)
	{
		cmd_error_code = CMD_ERR_INVALID_CHANNEL_NUMBER;
		return;
	}

	p = &COP_config[cmd_channel_index];

	if (cmd_equals)
	{																							// parse contents of equals command
		cmd_parse_flag(&p->flags, COP_MASK_OUTPUT_ENABLED);
		cmd_parse_flag(&p->flags, COP_MASK_OUTPUT_TYPE);
		cmd_parse_flag(&p->flags, COP_MASK_CURRENT_STATE);
		cmd_set_binary(&p->report_enable_mask, 3);
		cmd_set_uint16(&p->pulse_width_msec);
		cmd_set_uint16(&p->pulse_interval_msec);
		cmd_set_uint8(&p->number_of_pulses);

		if (cmd_error_code == CMD_ERR_NONE)
			COP_configure_channel(cmd_channel_index);											// act on new configuration
	}

	if (cmd_error_code == CMD_ERR_NONE)
	{
		j = sprintf(cmd_out_ptr, "dSCO%u=", cmd_channel_index + 1);
		j+= sprintf(&cmd_out_ptr[j], "%d,%d,%d,",
					  ((p->flags & COP_MASK_OUTPUT_ENABLED) != 0x00) ? 1 : 0,
					  ((p->flags & COP_MASK_OUTPUT_TYPE) != 0x00) ? 1 : 0,
					  ((p->flags & COP_MASK_CURRENT_STATE) != 0x00) ? 1 : 0);
																								// convert flag byte into string
		cmd_convert_flags_to_string(STR_buffer, p->report_enable_mask, 3);
		j += sprintf(&cmd_out_ptr[j], "%s,%u,%u,%u",STR_buffer,									// add flags and times
				  	 p->pulse_width_msec, 
				  	 p->pulse_interval_msec, 
				  	 p->number_of_pulses);
	}
}

/******************************************************************************
** Function:	Set RAM bit
**
** Notes:
*/
void cmd_setb(void)
{
	cmd_set_ram_bit(true);
}

/******************************************************************************
** Function:	script file progress
**
** Notes:
*/
void cmd_sfp(void)
{
	// reply
	sprintf(cmd_out_ptr, "dSFP=%u", SCF_progress());
}

/******************************************************************************
** Function:	Signal test results
**
** Notes:
*/
void cmd_sigres(void)
{
	if (!CFS_read_file((char *)CFS_activity_path, (char *)CFS_sigres_name, (char *)cmd_out_ptr, 150))
		cmd_error_code = CMD_ERR_FILE_OR_DIRECTORY_NOT_FOUND;
}

/******************************************************************************
** Function:	Signal test start
**
** Notes:
*/
void cmd_sigtst(void)
{
	// set default values
	COM_sigtst_delay = 60;
	COM_sigtst_interval = 10;
	COM_sigtst_samples = 30;
	// check equals
	if (cmd_equals)
	{
		// parse parameters
		cmd_set_uint16(&COM_sigtst_delay);
		cmd_set_uint8(&COM_sigtst_interval);
		cmd_set_uint8(&COM_sigtst_samples);
		// limit
		if (COM_sigtst_delay < 1) COM_sigtst_delay = 1;
		else if (COM_sigtst_delay > 3600) COM_sigtst_delay = 3600;
		if (COM_sigtst_interval < 1) COM_sigtst_interval = 1;
		else if (COM_sigtst_interval > 60) COM_sigtst_interval = 60;
		if (COM_sigtst_samples < 1) COM_sigtst_samples = 1;
		else if (COM_sigtst_samples > 30) COM_sigtst_samples = 30;
	}
	// do reply
	if (cmd_error_code == CMD_ERR_NONE)
	{
		sprintf(cmd_out_ptr, "dSIGTST=%u,%u,%u",
				COM_sigtst_delay, COM_sigtst_interval, COM_sigtst_samples);

		(void)CFS_open();				// keep file system awake

		// create initial file contents
		sprintf(STR_buffer, "dSIGRES=%02x%02x%02x,%02x:%02x:%02x",
				RTC_now.day_bcd, RTC_now.mth_bcd, RTC_now.yr_bcd,
				RTC_now.hr_bcd, RTC_now.min_bcd, RTC_now.sec_bcd);
		// write it to file
		if (CFS_write_file((char *)CFS_activity_path, (char *)CFS_sigres_name, "w", STR_buffer, strlen(STR_buffer)))
		{
			// start test
			COM_sigtst_count = COM_sigtst_samples;
		}
	}
}

/******************************************************************************
** Function:	Parse phone number into specified location
**
** Notes:		Sets cmd_error_code if necessary
*/
void cmd_parse_phone_number(char * p)
{
	int length;

	if ((cmd_error_code != CMD_ERR_NONE) || cmd_field_null())
		return;

	length = cmd_set_string(STR_buffer, MSG_PHONE_NUMBER_LENGTH);

	// if valid phone number, or cleared to 0, copy to destination
	if ((length == 0) || STR_phone_number_ok(STR_buffer))
		memcpy(p, STR_buffer, MSG_PHONE_NUMBER_LENGTH);
	else
		cmd_error_code = CMD_ERR_INVALID_VALUE;
}

/******************************************************************************
** Function:	Set or read sms configuration
**
** Notes:
*/
void cmd_smsc(void)
{
	if (cmd_equals)
	{
		cmd_parse_phone_number(COM_host1);
		cmd_parse_phone_number(COM_host2);
		cmd_parse_phone_number(COM_host3);
		cmd_parse_phone_number(COM_alarm1);
		cmd_parse_phone_number(COM_alarm2);
	}

	if (cmd_error_code == CMD_ERR_NONE)
	{
		// send sms config data
		sprintf(cmd_out_ptr, "dSMSC=%s,%s,%s,%s,%s",
				COM_host1, COM_host2, COM_host3, COM_alarm1, COM_alarm2);
	}
}

/******************************************************************************
** Function:	#tc
**
** Notes:
*/
void cmd_tc(void)
{
	uint8 a, b, c;
	bool  plus = false;

	if (cmd_equals)
	{
		if (*cmd_input_ptr == '-')
		{
			plus = false;
			cmd_input_ptr++;
		}
		else if (*cmd_input_ptr == '+')
		{
			plus = true;
			cmd_input_ptr++;
		}
		else
			cmd_error_code = CMD_ERR_INVALID_CHARACTER;

		if ((cmd_error_code == CMD_ERR_NONE) && cmd_set_time_bcd(&a, &b, &c))
		{
			if (RTC_add_time(plus, a, b, c))														// if time has changed
				TSYNC_change_clock();
		}
	}

	if (cmd_error_code == CMD_ERR_NONE)
	{
																									// reply with reply to #DT
		cmd_equals = false;
		cmd_dt();
	}
}

#ifndef HDW_RS485

/******************************************************************************
** Function:	Set totaliser
**
** Notes:
*/
void cmd_set_totaliser(DIG_totaliser_type *p)
{
	long long int value;
	float f;

	if (!cmd_field_null())		// up to 10-digit new value of totaliser
	{
		if (cmd_get_field() > 10)
		{
			cmd_error_code = CMD_ERR_INVALID_VALUE;
			return;
		}
		if (sscanf(STR_buffer, "%lld", &p->value_x10000) != 1)
		{
			cmd_error_code = CMD_ERR_INVALID_VALUE;
			return;
		}

		p->value_x10000 *= 10000;
	}

	if (cmd_set_float(&f))		// fractional part of totaliser
	{
		f *= 10000.0;
		f += 0.5;
								// add it to INTEGER part of totaliser, not ALL of totaliser (DEL 148) 
		value = (long long int)((long long int)f + ((p->value_x10000 / 10000) * 10000));
		p->value_x10000 = value;
	}

	if (cmd_set_float(&f))		
	{
		f *= 10000.0;
		f += 0.5;
		p->tcal_x10000 = (uint32)f;
	}
}

/******************************************************************************
** Function:	Print totaliser to referenced buffer, no trailing comma
**
** Notes:		Returns characters printed
*/
int cmd_print_totaliser(char *buffer, DIG_totaliser_type *p)
{
	double tcal;
	int c;

	tcal = (double)p->tcal_x10000;
	tcal *= 0.0001;

	c = DIG_print_totaliser_int(buffer, &p->value_x10000);
	return c + sprintf(&buffer[c], ",0.%04d,%1.4G", (int)(p->value_x10000 % 10000), tcal);
}
#endif

/******************************************************************************
** Function:	Configure time of day alarm
**
** Notes:		
*/
void cmd_tod(void)
{
	int i,j;
	ALM_tod_config_type *p;

	if (cmd_equals)
	{
		for (i = 0; i < ALM_TOD_NUMBER; i++)
		{
			p = &ALM_tod_config[i];
			cmd_set_bool(&p->enabled);
			cmd_set_time_bcd(&p->trigger_time.hr_bcd, &p->trigger_time.min_bcd, &p->trigger_time.sec_bcd);
		}

		if (cmd_error_code == CMD_ERR_NONE)
			ALM_update_profile();											// act on new trigger
	}

	if (cmd_error_code == CMD_ERR_NONE)
	{
		j = sprintf(cmd_out_ptr, "dTOD=");
		for (i = 0; i < ALM_TOD_NUMBER; i++)
		{
			p = &ALM_tod_config[i];
			if (i > 0)
				cmd_out_ptr[j++] = ',';
			j+= sprintf(&cmd_out_ptr[j], "%d,%02x:%02x:%02x",
						  p->enabled ? 1 : 0,
						  p->trigger_time.hr_bcd,
						  p->trigger_time.min_bcd,
						  p->trigger_time.sec_bcd);
		}
	}
}

/******************************************************************************
** Function:	Set, read and configure totaliser
**
** Notes:
*/
void cmd_tot(void)
{
#ifndef HDW_RS485
	int i;

	if (cmd_channel_index >= CAL_build_info.num_digital_channels)
	{
		cmd_error_code = CMD_ERR_INVALID_CHANNEL_NUMBER;
		return;
	}

	if (cmd_equals)
	{
		cmd_set_totaliser(&DIG_channel[cmd_channel_index].sub[0].totaliser);
		cmd_set_totaliser(&DIG_channel[cmd_channel_index].sub[1].totaliser);
		cmd_set_uint8(&DIG_channel[cmd_channel_index].sub[0].totaliser.units_enumeration);
		cmd_set_uint8(&DIG_channel[cmd_channel_index].sub[1].totaliser.units_enumeration);
		cmd_set_uint8(&DIG_channel[cmd_channel_index].sub[0].totaliser.log_interval_enumeration);
		cmd_set_uint8(&DIG_channel[cmd_channel_index].sub[1].totaliser.log_interval_enumeration);
	}

	cmd_print_totaliser(STR_buffer, &DIG_channel[cmd_channel_index].sub[0].totaliser);
	i = sprintf(cmd_out_ptr, "dTOT%u=%s,", cmd_channel_index + 1, STR_buffer);

	cmd_print_totaliser(STR_buffer, &DIG_channel[cmd_channel_index].sub[1].totaliser);
	i += sprintf(&cmd_out_ptr[i], "%s", STR_buffer);

	sprintf(&cmd_out_ptr[i], ",%u,%u,%u,%u",
		DIG_channel[cmd_channel_index].sub[0].totaliser.units_enumeration,
		DIG_channel[cmd_channel_index].sub[1].totaliser.units_enumeration,
		DIG_channel[cmd_channel_index].sub[0].totaliser.log_interval_enumeration,
		DIG_channel[cmd_channel_index].sub[1].totaliser.log_interval_enumeration);
#endif
}

/******************************************************************************
** Function:	Trigger control output
**
** Notes:
*/
void cmd_tro(void)
{
	COP_config_type *p;
	int j;

	if (cmd_channel_index >= COP_NUM_CHANNELS)
	{
		cmd_error_code = CMD_ERR_INVALID_CHANNEL_NUMBER;
		return;
	}

	p = &COP_config[cmd_channel_index];

	if (cmd_equals)
	{	
		cmd_parse_flag(&p->flags, COP_MASK_IMMEDIATE_TRIGGER);									// parse equals command
		cmd_parse_flag(&p->flags, COP_MASK_TIMED_TRIGGER);
		cmd_set_time_bcd(&p->trigger_time.hr_bcd, &p->trigger_time.min_bcd, NULL);

		if (cmd_error_code == CMD_ERR_NONE)
			COP_act_on_trigger_channel(cmd_channel_index);										// act on new trigger
	}

	if (cmd_error_code == CMD_ERR_NONE)
	{
		j = sprintf(cmd_out_ptr, "dTRO%u=", cmd_channel_index + 1);
		j+= sprintf(&cmd_out_ptr[j], "%d,%d,%02x:%02x",
					  ((p->flags & COP_MASK_IMMEDIATE_TRIGGER) != 0x00) ? 1 : 0,
					  ((p->flags & COP_MASK_TIMED_TRIGGER) != 0x00) ? 1 : 0,
					  p->trigger_time.hr_bcd,
					  p->trigger_time.min_bcd);
	}
}

/******************************************************************************
** Function:	time sync status report
**
** Notes:
*/
void cmd_tstat(void)
{
	// send tsync status data
	TSYNC_format_status(STR_buffer);
	sprintf(cmd_out_ptr, "dTSTAT=%s", STR_buffer);
}

/******************************************************************************
** Function:	send all lines of config\current.hcs as sms messages to given phone number
**
** Notes:		
*/
void cmd_send_current_as_sms(char * phone_number)
{
	int i, j;

	// Send sms all lines of \config\current.hcs to given phone number
	i = 1;
	do
	{
		j = CFS_read_line((char *)CFS_config_path, (char *)CFS_current_name, i++, STR_buffer, 140);
		if (j > 0)
		{
			MSG_send(MSG_TYPE_SMS_TEXT, STR_buffer, phone_number);
			MSG_flush_outbox_buffer(true);
		}
	}
	while (j > 0);
}

/******************************************************************************
** Function:	cmd_tsu_tx - transmission part of cmd_tsu()
**
** Notes:		can be called after the re-creation of current.hcs and nv.hcs by script files
*/
void cmd_tsu_tx(void)
{
	(void)CFS_open();																		// keep file system awake

	if (STR_match(cmd_tsu_destination, "sms"))
	{
		if (cmd_tsu_destination[3] == ':')
			cmd_send_current_as_sms(&cmd_tsu_destination[4]);								// Send sms all lines of \config\current.hcs to supplied phone number
		else
		{
			if (COM_host1[0] != '\0')														// Send sms all lines of \config\current.hcs to host number(s)
				cmd_send_current_as_sms(COM_host1);
			if (COM_host2[0] != '\0')
				cmd_send_current_as_sms(COM_host2);
			if (COM_host3[0] != '\0')
				cmd_send_current_as_sms(COM_host3);
		}
	}
	else if (STR_match(cmd_tsu_destination, "ftp"))
	{
		cmd_error_code = FTP_frd_send((char *)CFS_config_path, (char *)CFS_current_name);	// Send current.hcs and nv.hcs from \CONFIG\ directory to ftp server
		if (cmd_error_code == CMD_ERR_NONE)
			cmd_error_code = FTP_frd_send((char *)CFS_config_path, (char *)CFS_nv_name);
	}
	cmd_dirty_flags &= ~CMD_TSU;															// clear tcu bit
}

/******************************************************************************
** Function:	#TSU - transmit set up
**
** Notes:		#TSU=<Comms type & destination>
**				Comms type: sms:Phone number, sms, ftp
*/
void cmd_tsu(void)
{
	int i;

	if (cmd_equals)
	{
		if (cmd_set_string(cmd_tsu_destination, sizeof(cmd_tsu_destination)) == 0)
			cmd_error_code = CMD_ERR_NO_PARAMETERS;
		else
		{
			if (STR_match(cmd_tsu_destination, "sms:"))
			{
				if (!STR_phone_number_ok(&cmd_tsu_destination[4]))
					cmd_error_code = CMD_ERR_INVALID_VALUE;
			}
			else if ((!STR_match(cmd_tsu_destination, "sms")) && (!STR_match(cmd_tsu_destination, "ftp")))
				cmd_error_code = CMD_ERR_INVALID_VALUE;
		}
	}
	else
		cmd_error_code = CMD_ERR_NO_PARAMETERS;

	if (cmd_error_code == CMD_ERR_NONE)														// if ok check dirty flags
	{
		if (CMD_check_dirty_flags())														// will schedule script files to create new xxx.hcs if necessary
			cmd_dirty_flags |= CMD_TSU;														// if set set tcu bit to schedule cmd_tsu_tx() after creation of new xxx.hcs
		else																				// else clean
			cmd_tsu_tx();																	// schedule tx of xxx.hcs files
	}
	if (cmd_error_code == CMD_ERR_NONE)														// reply immediately
		i = sprintf(cmd_out_ptr, "dTSU=%s", cmd_tsu_destination);
}

/******************************************************************************
** Function:	time sync
**
** Notes:		
*/
void cmd_tsync(void)
{
	bool	on_off, use_mins_secs;
	uint8	adjust_threshold;
	uint16	interval, remaining;

	if (cmd_equals)
	{
		// on off flag
		if (!cmd_field_null())
		{
			cmd_set_bool(&on_off);
			if (cmd_error_code == CMD_ERR_NONE)
				TSYNC_on = on_off;
		}

		// interval
		if (!cmd_field_null())
		{
			cmd_set_uint16(&interval);
			if (cmd_error_code == CMD_ERR_NONE)
				TSYNC_set_interval(interval);
		}

		// remaining
		if (!cmd_field_null())
		{
			cmd_set_uint16(&remaining);
			if (cmd_error_code == CMD_ERR_NONE)
				TSYNC_set_remaining(remaining);
		}

		// use mins secs flag
		if (!cmd_field_null())
		{
			cmd_set_bool(&use_mins_secs);
			if (cmd_error_code == CMD_ERR_NONE)
				TSYNC_use_mins_secs = use_mins_secs;
		}

		// threshold
		if (!cmd_field_null())
		{
			cmd_set_uint8(&adjust_threshold);
			if (adjust_threshold <= 30)
				TSYNC_threshold = adjust_threshold;
			else
				cmd_error_code = CMD_ERR_INVALID_VALUE;
		}
		// go act on parameters
		TSYNC_action();
	}

	if (cmd_error_code == CMD_ERR_NONE)
		sprintf(cmd_out_ptr, "dTSYNC=%d,%u,%u,%d,%u", 
                TSYNC_on, TSYNC_get_interval(), TSYNC_get_remaining(), TSYNC_use_mins_secs, TSYNC_threshold);
}

/******************************************************************************
** Function:	Execute command pointed to by cmd_input_ptr
**
** Notes:
*/
void cmd_execute(void)
{
	int i;

	cmd_command_string = cmd_input_ptr;
	for (i = 0; i < sizeof(cmd_action_table) / sizeof(cmd_action_table[0]); i++)
	{
		if (STR_match(cmd_input_ptr, cmd_action_table[i].cmd))
		{
			cmd_input_ptr += strlen(cmd_action_table[i].cmd);
																										// Allow a 1-digit channel number, except on #ALM and #AP
																										// cmd_channel_index will be the index of the specified channel 
																										// in DIG_config, ANA_config, or ALM_config. 
																										// Default is channel 1, which is index 0.
			cmd_channel_index = 0;
																										// if #EC or #CEC allow sub-channel Dnx only
			if ((cmd_action_table[i].function == cmd_ecd) || 
				(cmd_action_table[i].function == cmd_cec))
			{
				if (cmd_action_table[i].function == cmd_ecd)											// if #ECD decrement input pointer to point at D of Dnx
					 cmd_input_ptr--;
				if ((*cmd_input_ptr | _B00100000) == 'd')
				{
					cmd_input_ptr++;																	// point to channel number
					cmd_channel_index = (*cmd_input_ptr++ - '1') << 1;									// '1'..'2' -> 0 or 2
					if ((*cmd_input_ptr | _B00100000) == 'b')
						cmd_channel_index++;															// 1 or 3
					else if ((*cmd_input_ptr | _B00100000) != 'a')
					{
						cmd_error_code = CMD_ERR_INVALID_CHANNEL_NUMBER;
						*(cmd_input_ptr + 2) = '\0';													// string terminate cmd_command_string
						return;
					}
					cmd_input_ptr++;
				}
				else
				{
					cmd_error_code = CMD_ERR_INVALID_CHANNEL_NUMBER;
					*(cmd_input_ptr + 1) = '\0';														// string terminate cmd_command_string
					return;
				}
			}
			else if ((cmd_action_table[i].function == cmd_alm) || 
				((cmd_action_table[i].cmd[0] == 'a') &&
				 (((cmd_action_table[i].cmd[1] == 'e') || (cmd_action_table[i].cmd[1] == 'p')))))
			{
				// #ALM, #AEH, #AEL or #AP allow sub-channel An or Dnx, and derived channels DDnx and DAn
				switch (*cmd_input_ptr | _B00100000)													// Exit from this switch pointing to the '=' or end of string
				{
				case 'a':																				// #ALMAn, #AEH1-4An, #AEL1-4An or #AP1-4An
																										// A1..7 -> alarm channel number 5..11 -> index 4..10
					cmd_input_ptr++;																	// point to channel number
					cmd_channel_index = LOG_ANALOGUE_1_INDEX - 1 + *cmd_input_ptr++ - '1';
					break;

				case 'd':																				// derived analogue or normal digital
					if ((*(cmd_input_ptr + 1) | _B00100000) == 'a')										// if derived analogue
					{
						cmd_input_ptr++;																// move past "derived 'd'"
						cmd_input_ptr++;																// point to channel number
						cmd_channel_index = LOG_ANALOGUE_1_INDEX - 1 + *cmd_input_ptr++ - '1';
						cmd_channel_index += ALM_ALARM_DERIVED_CHANNEL0;								// add 11 to channel number
					}
#ifdef HDW_RS485																						// doppler serial
					else if ((*(cmd_input_ptr + 1) | _B00100000) == 's')								// if derived serial channel 1
					{
						cmd_input_ptr++;																// move past "derived 'd'"
						cmd_input_ptr++;																// point to channel number
						cmd_channel_index = *cmd_input_ptr++ - '1';
						cmd_channel_index += ALM_ALARM_DERIVED_CHANNEL0;								// add 11 to channel number
						if (cmd_channel_index > 11)														// limit check
						{
							cmd_error_code = CMD_ERR_INVALID_CHANNEL_NUMBER;
							*(cmd_input_ptr + 1) = '\0';												// string terminate cmd_command_string
							return;
						}
					}
					else
					{
						cmd_error_code = CMD_ERR_INVALID_CHANNEL_NUMBER;
						*(cmd_input_ptr + 1) = '\0';													// string terminate cmd_command_string
						return;
					}
#else
					else																				// else defaults to normal digital
					{
																										// #ALMDnx, #AEH1-4Dnx, #AEL1-4Dnx, #AP1-4Dnx
																										// D1A -> index 0, D1B -> index 1, D2A -> index 2, D2B -> index 3
						cmd_input_ptr++;																// point to channel number
						cmd_channel_index = (*cmd_input_ptr++ - '1') << 1;								// '1'..'2' -> 0 or 2
						if ((*cmd_input_ptr | _B00100000) == 'b')
							cmd_channel_index++;														// 1 or 3
						else if ((*cmd_input_ptr | _B00100000) != 'a')
						{
							cmd_error_code = CMD_ERR_INVALID_CHANNEL_NUMBER;
							*(cmd_input_ptr + 2) = '\0';												// string terminate cmd_command_string
							return;
						}
						cmd_input_ptr++;
					}
#endif
					break;

				case 's':																				// #ALMSn, #AEH1-4Sn, #AEL1-4Sn or #AP1-4Sn
					// S1..5 -> alarm channel number 1..5 -> index 0..4
					cmd_input_ptr++;																	// point to channel number
					cmd_channel_index = *cmd_input_ptr++ - '1';
					if (cmd_channel_index > 4)															// limit check
					{
						cmd_error_code = CMD_ERR_INVALID_CHANNEL_NUMBER;
						*(cmd_input_ptr + 1) = '\0';													// string terminate cmd_command_string
						return;
					}
					break;

#ifdef HDW_RS485																						// doppler sensor
/*
				case 's':																				// #ALMSn, #AEH1-4Sn, #AEL1-4Sn or #AP1-4Sn
																										// S1..3 -> alarm channel number 1..3 -> index 0..2
					cmd_input_ptr++;																	// point to channel number
					cmd_channel_index = *cmd_input_ptr++ - '1';
					if (cmd_channel_index > 2)															// limit check
					{
						cmd_error_code = CMD_ERR_INVALID_CHANNEL_NUMBER;
						*(cmd_input_ptr + 1) = '\0';													// string terminate cmd_command_string
						return;
					}
					break;
*/
#else
				case 'r':																				// if derived digital - letter 'r'
																										// #ALMRnx, #AEH1-4Rnx, #AEL1-4Rnx, #AP1-4Rnx, #ECRnx, #CECRnx
																										// D1A->index 11, D1B->index 12, D2A->index 13, D2B->index 14
					cmd_input_ptr++;																// point to channel number
					cmd_channel_index = (*cmd_input_ptr++ - '1') << 1;								// '1'..'2' -> 0 or 2
					if ((*cmd_input_ptr | _B00100000) == 'b')
						cmd_channel_index++;														// 1 or 3
					else if ((*cmd_input_ptr | _B00100000) != 'a')
					{
						cmd_error_code = CMD_ERR_INVALID_CHANNEL_NUMBER;
						*(cmd_input_ptr + 2) = '\0';												// string terminate cmd_command_string
						return;
					}
					cmd_channel_index+= 11;
					cmd_input_ptr++;
					break;
#endif
				case '=':																				// assume D1A unless EC or CEC
					if ((cmd_action_table[i].function == cmd_ecd) || (cmd_action_table[i].function == cmd_cec))
					{
						cmd_error_code = CMD_ERR_INVALID_CHANNEL_NUMBER;
						*cmd_input_ptr = '\0';															// string terminate cmd_command_string
						return;
					}
					break;

				default:
					// #ALM=... defaults to ALMD1A=...
					// #AP1=... defaults to #AP1D1A...
					// #AEH1=... defaults to #AEH1D1A...
				// #AEL1=... defaults to #AEL1D1A...
					cmd_error_code = CMD_ERR_INVALID_CHANNEL_NUMBER;
					*(cmd_input_ptr + 1) = '\0';														// string terminate cmd_command_string
					return;
				}
			}
			else if ((*cmd_input_ptr >= '1') && (*cmd_input_ptr <= '9'))								// just channel number
				cmd_channel_index = *cmd_input_ptr++ - '1';

																										// command followed by something other than '=' is an error
																										// for all commands except #AT
			cmd_equals = (*cmd_input_ptr == '=');
			if (cmd_action_table[i].function != cmd_at)
			{
				*cmd_input_ptr = '\0';																	// string terminate cmd_command_string

				if (cmd_equals)
				{
					cmd_input_ptr++;
					
					// set dirty flags for those that alter configuration (ignore the suppress-acivity-log bit)
					cmd_dirty_flags |= cmd_action_table[i].config_flags & ~CMD_NO_ACTIVITY_LOG;
				}
				else if (cmd_input_ptr != cmd_end_ptr)
				{
					cmd_error_code = CMD_ERR_INVALID_CHARACTER;
					return;
				}
			}

			if (COM_test_in_progress())																	// test for test in progress
			{
																										// if not abort or result enquiry
				if ((cmd_action_table[i].function != cmd_abort) && (cmd_action_table[i].function != cmd_sigres) && (cmd_action_table[i].function != cmd_nwres)) 
				{
					cmd_error_code = CMD_ERR_TEST_IN_PROGRESS;
					return;
				}
			}

			cmd_config_flags = cmd_action_table[i].config_flags;
			cmd_action_table[i].function();
			return;
		}
	}

	cmd_error_code = CMD_ERR_UNRECOGNISED_COMMAND;
}

/******************************************************************************
** Function:	Clear pending action flag for current comms source & go back to idle
**
** Notes:
*/
void cmd_done(void)
{
	int i;

	if (cmd_error_code != CMD_ERR_NONE)
	{
		i = (int)(cmd_input_ptr - cmd_input[cmd_source_index]);
		if (cmd_command_string == NULL)
			cmd_command_string = "";
		sprintf(cmd_out_ptr, "dERROR=%u,%s,%d", cmd_error_code, cmd_command_string, i);
		cmd_equals = true;															// log error message in use log
		cmd_config_flags &= ~CMD_NO_ACTIVITY_LOG;
	}

	// If command set something & needs to be added to file use.txt, add it.
	if (cmd_equals && ((cmd_config_flags & CMD_NO_ACTIVITY_LOG) == 0))
		LOG_entry(cmd_out_ptr);

	cmd_source_mask &= ~(1 << cmd_source_index);									// clear the action flag
	if (cmd_source_index == CMD_SOURCE_USB)											// add string termination to USB output
		strcat(cmd_out_ptr, "\r\n");
	else if (cmd_source_index == CMD_SOURCE_SCRIPT)									// we are processing a script file
	{
		if (*SCF_output_filename == '\0')											// no target filename
			USB_monitor_string(cmd_out_ptr);
		else																		// we are producing a target file from a script file
		{
			strcat(cmd_out_ptr, "\r\n");											// terminate with CRLF

			// change first letter 'd' to '#' or '&' so target file can be used as a script file
			if (*cmd_out_ptr == 'd')
				*cmd_out_ptr = CMD_CHARACTER;

			// append string to target file
			CFS_write_file((char *)CFS_config_path, (char *)SCF_output_filename, "a", cmd_out_ptr, strlen(cmd_out_ptr));
		}

																					// do next line of file
		if (!SCF_execute_next_line())												// end of script
		{
			if (STR_match(SCF_output_filename, "current.hcs"))					// just finished producing current.hcs
				cmd_dirty_flags &= ~CMD_VOLATILE;
			else if (STR_match(SCF_output_filename, "nv.hcs"))					// just finished producing nv.hcs
				cmd_dirty_flags &= ~CMD_NON_VOLATILE;

			if (!CMD_check_dirty_flags() && ((cmd_dirty_flags & CMD_TSU) != 0x00))	// create nv.hcs after current.hcs if necessary														 										// if dirty flags cleared after check  and if was started by #tsu command
				cmd_tsu_tx();														// schedule tx of xxx.hcs files
		}
	}
	cmd_state = CMD_IDLE;
}

//*****************************************************************************
// Public functions
//*****************************************************************************

/******************************************************************************
** Function:	check the dirty flags and if set attempt to create the current or nonvolatile configuration script file
**
** Notes:		requires to be called again if current config was created
**				returns true if either flag is set, else false
*/
bool CMD_check_dirty_flags(void)
{
	bool flags = false;

	if (CFS_open() && (CFS_state == CFS_OPEN))
	{
		if ((cmd_dirty_flags & CMD_VOLATILE) != 0x00)												// if volatile dirty flag set
		{																							// Don't clear the flag until we've finished executing the script
			FSchdir((char *)CFS_config_path);														// remove current.hcs if it exists - out of date.
			FSremove((char *)CFS_current_name);
			strcpy(SCF_output_filename, (char *)CFS_current_name);									// attempt to create new current.hcs, if we have a configuration builder
			if (!SCF_execute((char *)CFS_config_path, (char *)CFS_get_cfg_name, true))
				cmd_dirty_flags &= ~CMD_VOLATILE;													// clear flag if can't run script
			else
				flags = true;
		}
		else if ((cmd_dirty_flags & CMD_NON_VOLATILE) != 0x00)										// else if non-volatile dirty flag set
		{																							// Don't clear the flag until we've finished executing the script
			FSchdir((char *)CFS_config_path);														// remove nv.hcs if it exists - out of date.
			FSremove((char *)CFS_nv_name);
			strcpy(SCF_output_filename, (char *)CFS_nv_name);										// attempt to create new nv.hcs, if we have a configuration builder
			if (!SCF_execute((char *)CFS_config_path, (char *)CFS_get_nv_name, true))
				cmd_dirty_flags &= ~CMD_NON_VOLATILE;												// clear flag if can't run script
			else
				flags = true;
		}
	}
	return flags;
}

/******************************************************************************
** Function:	parse the rsd parameters into the given config structure
**
** Notes:		used by CMD and PDU code - we have to fool the cmd pointer into pointing at the pdu data line
**				Must cope with two time formats: <hh:mm:sec>, and <hh:mm> with sec forced to zero. 
*/
uint8 CMD_parse_rsd(PDU_sms_retrieve_type * p_dest, char * p_start, char * p_end)
{
	cmd_error_code = CMD_ERR_NONE;

	// set up input pointers
	cmd_input_ptr = p_start;
	cmd_end_ptr = p_end;

	// parse channel no
	if (!cmd_field_null())
	{
		if (!cmd_set_channel_no(&p_dest->channel))
			cmd_error_code = CMD_ERR_INVALID_CHANNEL_NUMBER;
		else
			p_dest->channel--;
	}
	if (!cmd_field_null())																		// parse date
		cmd_set_date_bcd(&p_dest->when.day_bcd, &p_dest->when.mth_bcd, &p_dest->when.yr_bcd);
	else
	{
		// use today's date
		p_dest->when.day_bcd = RTC_now.day_bcd;
		p_dest->when.mth_bcd = RTC_now.mth_bcd;
		p_dest->when.yr_bcd = RTC_now.yr_bcd;
		cmd_next_field();
	}

	if (!cmd_field_null())
	{
		// parse hours and minutes of time
		p_dest->when.hr_bcd = cmd_parse_bcd(':', 0x59);
		p_dest->when.min_bcd = cmd_parse_bcd('\0', 0x59);
		// if have second colon
		if (*cmd_input_ptr == ':')
		{
			// parse seconds
			cmd_input_ptr++;
			p_dest->when.sec_bcd = cmd_parse_bcd('\0', 0x59);
		}
	}
	else
	{
		// use default tx window start time
		p_dest->when.hr_bcd = COM_schedule.tx_window.start.hr_bcd;
		p_dest->when.min_bcd = COM_schedule.tx_window.start.min_bcd;
		p_dest->when.sec_bcd = 0;
	}

	return cmd_error_code;
}

#ifndef HDW_RS485
/******************************************************************************
** Function:	parse the dcc parameters into the given config structure
**
** Notes:		used by CMD - we have to fool the cmd pointer into pointing at the pdu data line
*/
uint8 CMD_parse_dcc(DIG_config_type * p_dest, char * p_start, char * p_end)
{
	uint8 i;

	cmd_error_code = CMD_ERR_NONE;
	cmd_input_ptr = p_start;														// set up input pointers
	cmd_end_ptr = p_end;

	cmd_parse_flag(&p_dest->flags, DIG_MASK_CHANNEL_ENABLED);
	cmd_set_uint8(&p_dest->sensor_type);
#if (HDW_NUM_CHANNELS == 3)															// 3-channel logger only supports events on DIG1A & DIG1B
	if (cmd_channel_index > 0)														// DIG2 selected
		p_dest->sensor_type &= 0x0F;												// pulse counting sensor types only
#endif

	cmd_parse_flag(&p_dest->flags, DIG_MASK_CONTINUOUS_POWER);
	cmd_parse_flag(&p_dest->flags, DIG_MASK_VOLTAGE_DOUBLER);
	cmd_parse_flag(&p_dest->flags, DIG_MASK_COMBINE_SUB_CHANNELS);
	cmd_parse_flag(&p_dest->flags, DIG_MASK_MESSAGING_ENABLED);
	cmd_set_uint8(&p_dest->sample_interval);
	cmd_set_uint8(&p_dest->log_interval);
	cmd_set_uint8(&p_dest->min_max_sample_interval);
	cmd_set_uint8(&p_dest->sms_data_interval);
	cmd_set_uint8(&p_dest->event_log_min_period);
	cmd_set_uint8(&p_dest->pit_min_period);
	cmd_set_float(&p_dest->fcal_a);
	cmd_set_float(&p_dest->fcal_b);
	if (!cmd_field_null())															// take care to leave as is if field empty
	{
		cmd_set_uint8(&p_dest->rate_enumeration);
		if ((p_dest->rate_enumeration & 0x04) != 0x00)								// set or clear derived volume enable flag
			p_dest->flags |= DIG_MASK_DERIVED_VOL_ENABLED;
		else
			p_dest->flags &= ~DIG_MASK_DERIVED_VOL_ENABLED;
		p_dest->rate_enumeration &= 0x03;
	}
	cmd_set_uint8(&p_dest->sensor_index);
	cmd_set_hex_byte(&p_dest->sms_message_type);
	cmd_set_uint8(&p_dest->description_index);
	cmd_set_uint8(&p_dest->units_index);
	cmd_set_uint8(&p_dest->ff_pulse_width_x32us);
	for (i = 0; i < 8; i++)
		cmd_set_uint8(&p_dest->ff_period[i]);

	if (p_dest->sms_message_type > 0xFC)											// auto correct sms message type
		p_dest->sms_message_type &= 0xFC;

#ifdef HDW_1FM

	// Check sample interval 10s, 30s or 1 min:
	if ((p_dest->sample_interval != 5) && (p_dest->sample_interval != 8) && (p_dest->sample_interval != 9))
		cmd_error_code = CMD_ERR_VALUE_OUT_OF_RANGE;

	// Check log interval 5, 10, 15, 30 or 60 minutes:
	if ((p_dest->log_interval < 12) || (p_dest->log_interval > 17) || (p_dest->log_interval == 15))
		cmd_error_code = CMD_ERR_VALUE_OUT_OF_RANGE;

	// disallow continuous power:
	p_dest->flags &= ~DIG_MASK_CONTINUOUS_POWER;

#endif

	return cmd_error_code;
}
#endif

#ifndef HDW_GPS
/******************************************************************************
** Function:	parse the acc parameters into the given config structure
**
** Notes:
*/
uint8 CMD_parse_acc(ANA_config_type * p_dest, char * p_start, char * p_end)
{
	bool internal_transducer;
	int i;

	cmd_error_code = CMD_ERR_NONE;
	// set up input pointers
	cmd_input_ptr = p_start;
	cmd_end_ptr = p_end;

	// set flag if reading config for internal transducer:
	internal_transducer = false;
	for (i = 0; i < ANA_NUM_CHANNELS; i++)
	{
		if (p_dest == &ANA_config[i])
		{
			internal_transducer = (CAL_build_info.analogue_channel_types[i] == 'P');
			break;
		}
	}

	cmd_parse_flag(&p_dest->flags, ANA_MASK_CHANNEL_ENABLED);
	cmd_set_uint8(&p_dest->sensor_type);
	cmd_parse_flag(&p_dest->flags, ANA_MASK_POWER_TRANSDUCER);
	cmd_parse_flag(&p_dest->flags, ANA_MASK_MESSAGING_ENABLED);
	cmd_set_uint8(&p_dest->sample_interval);
	cmd_set_uint8(&p_dest->log_interval);
	cmd_set_uint8(&p_dest->min_max_sample_interval);
	cmd_set_uint8(&p_dest->sms_data_interval);
	cmd_set_float(&p_dest->user_offset);
	cmd_set_float(&p_dest->auto_offset);

	if (internal_transducer)		// read-only parameters
	{
		p_dest->sensor_type = ANA_SENSOR_DIFF_MV;
		cmd_next_field();
		cmd_next_field();
		cmd_next_field();
	}
	else							// external transducer
	{
		cmd_set_float(&p_dest->e0);
		cmd_set_float(&p_dest->e1);
		cmd_set_float(&p_dest->p0);
	}

	cmd_set_float(&p_dest->p1);
	cmd_set_uint8(&p_dest->sensor_index);
	cmd_set_hex_byte(&p_dest->sms_message_type);
	cmd_set_uint8(&p_dest->description_index);
	cmd_set_uint8(&p_dest->units_index);

	// auto correct sms message type
	if (p_dest->sms_message_type > 0xFC)
		p_dest->sms_message_type &= 0xFC;

	return cmd_error_code;
}
#endif

/******************************************************************************
** Function:	Schedule a parse operation
**
** Notes:
*/
void CMD_schedule_parse(uint8 index, char * input, char * output)
{
	cmd_source_mask |= 1 << index;
	cmd_input[index] = input;
	cmd_output[index] = output;
}

/******************************************************************************
** Function:	Check if parser busy for a particular source
**
** Notes:
*/
bool CMD_busy(uint8 index)
{
	return ((cmd_source_mask & (1 << index)) != 0);
}

/******************************************************************************
** Function:	Check if CMD task can sleep
**
** Notes:
*/
bool CMD_can_sleep(void)
{
	return (cmd_state == CMD_IDLE);
}

/******************************************************************************
** Function:	Parse command from buffer at *p
**
** Notes:		Input string can be terminated by '\r' or '\n' after the '#', or '\0'.
** Writes output to cmd_out_ptr.
*/
void CMD_task(void)
{
	int i;
	char *p = NULL;

	switch (cmd_state)
	{
	case CMD_IDLE:
		if (cmd_source_mask == 0)		// nothing to do
			return;

		for (cmd_source_index = 0; cmd_source_index < CMD_NUM_SOURCES; cmd_source_index++)
		{
			if ((cmd_source_mask & (1 << cmd_source_index)) != 0)
				break;
		}

		if (cmd_source_index >= CMD_NUM_SOURCES)
		{
			LOG_enqueue_value(LOG_ACTIVITY_INDEX, LOG_CMD_FILE, __LINE__);	// invalid flag set for pending parse operation
			cmd_source_mask = 0;
			return;
		}
		// else setup buffer pointers:

		p = cmd_input[cmd_source_index];
		cmd_out_ptr = cmd_output[cmd_source_index];

		cmd_error_code = NULL;
		cmd_command_string = NULL;

		// skip white space at beginning of command:
		while ((*p == '\r') || (*p == '\n') || (*p == ' '))
			p++;

		cmd_input_ptr = p + 1;
		if (*p != CMD_CHARACTER)
			cmd_error_code = CMD_ERR_NO_HASH;	// or ampersand if 1Fm
		else
		{
			// get pointer to end of command
			cmd_end_ptr = cmd_input_ptr;
			i = 0;
			while ((*cmd_end_ptr != '\0') && (*cmd_end_ptr != '\r') && (*cmd_end_ptr != '\n'))
			{
				cmd_end_ptr++;
				if (++i > CMD_MAX_LENGTH)
				{
					cmd_error_code = CMD_ERR_COMMAND_TOO_LONG;
					break;
				}
			}
		}

		if (cmd_error_code == CMD_ERR_NONE)
		{
			*cmd_end_ptr = '\0';
			cmd_execute();
		}

		if (cmd_state == CMD_IDLE)
			cmd_done();
		break;

	case CMD_EXECUTE_AT_COMMAND:
		if (!MDM_cmd_in_progress())
		{
			cmd_at();
			cmd_done();
		}
		break;

	case CMD_READ_SNS_COUNTERS:
		// if finished, generate reply then return to idle state
#if (HDW_NUM_CHANNELS == 9)
		if (cmd_channel_index == 1)
		{
			if (SNS_read_digital_counters_2 || SNS_command_in_progress)
				break;
		}
		else
#endif
		if (SNS_read_digital_counters || SNS_command_in_progress)
			break;
		// else:

		cmd_dpc();
		cmd_done();
		break;

	case CMD_READ_LI_INFO:
		// if finished, generate reply then return to idle state
#if (HDW_NUM_CHANNELS == 9)
		if (cmd_channel_index == 1)
		{
			if (SNS_read_version_2 || SNS_command_in_progress)
				break;
		}
		else
#endif
		if (SNS_read_version || SNS_command_in_progress)
			break;
		// else:

		cmd_li();
		cmd_done();
		break;

#ifndef HDW_GPS
	case CMD_READ_ADV:
		if (ANA_get_adc_values(cmd_channel_index))
		{
			cmd_adv();
			cmd_done();
		}
		break;
#endif

	case CMD_IDV_POWER:
	case CMD_IMV_POWER:
	case CMD_RDA_POWER:
		if (!PWR_measure_in_progress())
			cmd_state++;					// either CMD_IDV_ANA, CMD_IMV_ANA or CMD_RDA_ANA
		break;

	case CMD_IDV_ANA:
		// Print result if conversion not required (don't want to wait for transducer
		// powering time), or conversion complete
#ifndef HDW_GPS
		if (ANA_power_transducer_required() || !ANA_busy())
#endif
		{
			cmd_idv();
			cmd_done();
		}
		break;

	case CMD_IMV_ANA:
		// Print result if conversion not required (don't want to wait for transducer
		// powering time), or conversion complete
#ifndef HDW_GPS
		if (ANA_power_transducer_required() || !ANA_busy())
#endif
		{
			cmd_imv();
			cmd_done();
		}
		break;

	case CMD_RDA_ANA:
		// Print result if conversion not required (don't want to wait for transducer
		// powering time), or conversion complete
#ifndef HDW_GPS
		if (ANA_power_transducer_required() || !ANA_busy())
#endif
		{
			cmd_rda();
			cmd_done();
		}
		break;

#ifndef HDW_GPS
	case CMD_AUTOCAL:
		if (ANA_get_adc_values(cmd_channel_index))
		{
			cmd_acal();
			cmd_done();
		}
		break;
#endif

#ifdef HDW_RS485_
	case CMD_NIVUS:
		if (TIM_20ms_tick)
		{
			if (--cmd_20ms_timer == 0)
			{
				cmd_nv_finish();
				cmd_done();
			}
		}
		else if (!DOP_busy())
		{
			cmd_nv_finish();
			cmd_done();
		}
		break;
#endif

	default:		// confused state machine
		cmd_error_code = CMD_ERR_INTERNAL;
		cmd_done();
		break;
	}
}

/******************************************************************************
** Function:	undocumented test command
**
** Notes:
*/
void cmd_test(void)
{
	sprintf(cmd_out_ptr, "dTEST=%u", LOG_state);
}

/******************************************************************************
** Function:	undocumented fram write command
**
** Notes:
*/
void cmd_frmw(void)
{
/*
	uint8 bytes;
	uint16 address;

	if (FRM_fram_present)
	{
		if (cmd_equals)
		{
			cmd_set_uint8(&bytes);
			cmd_set_uint16(&address);
			cmd_set_string(STR_buffer, bytes+1);
			FRM_write_data((uint8*)STR_buffer, address, bytes);
			sprintf(cmd_out_ptr, "dFRMW=%u,%u,%s", bytes, address, STR_buffer);
		}
		else
			cmd_error_code = CMD_ERR_NO_PARAMETERS;
	}
	else
*/
		cmd_error_code = CMD_ERR_UNRECOGNISED_COMMAND;
}

/******************************************************************************
** Function:	undocumented fram read command
**
** Notes:
*/
void cmd_frmr(void)
{
/*
	uint8 bytes;
	uint16 address;

	if (FRM_fram_present)
	{
		if (cmd_equals)
		{
			cmd_set_uint8(&bytes);
			cmd_set_uint16(&address);
			FRM_read_data(address, (uint8*)STR_buffer, bytes);
			STR_buffer[bytes]='\0';
			sprintf(cmd_out_ptr, "dFRMR=%u,%u,%s", bytes, address, STR_buffer);
		}
		else
			cmd_error_code = CMD_ERR_NO_PARAMETERS;
	}
	else
*/
		cmd_error_code = CMD_ERR_UNRECOGNISED_COMMAND;
}

#pragma endregion

/******************************************************************************
** Function:	undocumented MODBUS test command
**
** Notes:
*/
void cmd_mod(void)
{
	uint16	x;
	uint16	mask;
	uint8	i;
	uint8	channel = 0;
	char	temp[20];

	if (cmd_equals)
	{
		// interval
		i = MOD_config.interval;
		cmd_set_uint8(&i);
		if (i > 24)
		{
			cmd_error_code = CMD_ERR_VALUE_OUT_OF_RANGE;
			return;
		}
		MOD_config.interval = i;

		// channel enable flags
		for (mask = 0x1; mask < 0x100; mask <<= 1)
		{
			if (cmd_set_uint16(&x))
			{
				if (x > 1)	// bool only
				{
					cmd_error_code = CMD_ERR_INVALID_VALUE;
					return;
				}

				if (x)
				{
					MOD_config.channel_enable_bits |= mask;
					MOD_channel_mins[channel] = FLT_MAX;
					memset(&MOD_channel_mins_time[channel], 0, sizeof(RTC_hhmmss_type));
					MOD_channel_maxes[channel] = -FLT_MAX;
					memset(&MOD_channel_maxes_time[channel], 0, sizeof(RTC_hhmmss_type));
				}
				else
					MOD_config.channel_enable_bits &= ~mask;
			}
			else if (cmd_error_code != CMD_ERR_NONE)
				return;
			
			channel++;
		}

		// load settings
		if (MOD_config.channel_enable_bits != 0)	// at least one channel enabled
		{
			MOD_wakeup_time = RTC_time_sec;
			LOG_set_next_time(&MOD_wakeup_time, MOD_config.interval, false);
		}
		else
			LOG_set_next_time(&MOD_wakeup_time, 0, false);

		// TX enable flag
		if (cmd_set_uint16(&x))
		{
			if (x > 1)	// bool only
			{
				cmd_error_code = CMD_ERR_INVALID_VALUE;
				return;
			}

			if (x)
				MOD_tx_enable = true;
			else
				MOD_tx_enable = false;
		}
		else if (cmd_error_code != CMD_ERR_NONE)
			return;
	}

	i = sprintf(cmd_out_ptr, "dMOD=%u,", MOD_config.interval);
	for (mask = 0x1; mask < 0x100; mask <<= 1)
		i += sprintf(&cmd_out_ptr[i], "%d,", ((MOD_config.channel_enable_bits & mask) != 0) ? 1 : 0);
	i += sprintf(&cmd_out_ptr[i], "%u", MOD_tx_enable);
	//cmd_out_ptr[i - 1] = '\0';	// erase last comma

	USB_monitor_string(temp);	
}
