/******************************************************************************
** File:	Version.h
**
** Notes:	Defines firmware version
*/

// Version number defined in this form to allow use by USB driver.
// Reported in #LI response as %x.%x (i.e. BCD), MS byte then LS byte.

// 2.05		First version for Rev 2 3-channel HW: 
// 2.14		First version for Rev 3 3-channel HW: 
// 3.01		First version for Waste Water Product: 
// 3.05		Merging changes up to Xilog+ V2.98 with Waste Water V3.04: 
// 3.08		Merging XilogPlus ECO code V2.99/3rd with Waste Water V3.07:   
// V3.09	Merging XilogPlus power task code V2.99/5th with Waste Water V3.08: 
// V3.10	Correction to command #ACO: 
// V3.11	correction to table lookup for depth to flow calculation. ana.c: 
// V3.12	change to filename for FTP control output messages, addition of event driven volume cal and units to #CEC: 
// V3.13	correction to creation of ftp intermediate headers for digital event value data log.c: 
// V3.14	1. negative values in depth to flow conversion - subtracting higer from lower point in conversion - ana.c:
//			2. read depth to flow conversion data from file system into RAM when receive #ADD command - cmd.c: ana.c: ana.h:
//			3. divide system and pump volumes by tcal to deconvert to flow units * time - dig.c:
// V3.15	Update with fixes to Xilog+ V3.01
// V3.16	separate config space for channels and derived channels - ana.h: ana.c:
//			add function to read file APIPE.CAL into RAM when use #DSF command - cmd.c: dop.h: dop.c:
//			use RAM apipe.cal image when calculating pipe area from depth - dop.c:   
// V3.17	Update with fixes to Xilog+ V3.02 to V3.06
// V3.18	Update with fix to Xilog+ NEW VERSION V3.03 - com.c: sns.c:
// V3.19	Analogue boost time up to 65 seconds
// V3.20	ATEX variant added
// V3.21	Fix for occasional failure to update Sensor PIC digital config, and DEL157 (#TC sometimes causing
//			digital sampling to stop).
//			Modem lock-up if TSYNC attempted with no SIM card or failure to register - fixed.
//			#RESET=9 added
//			Outgoing message numbers wrapping around - fixed (DEL178) Numerous other instances
//			of unsigned variables printed with %d also changed to %u. Also DEL 187.
//			DEL179: all pending SMS now transmitted after FTP Tx (not just one).
//			DEL 182: IMV & RDA only cause analogue read if transducer powering not enabled, otherwise
//			they just report the last sample value.
//			DEL172: escape string not terminated when message truncated to 160 characters - fixed.
// V3.22	DEL174: call DIG_insert_headers() in #CEC command to ensure headers inserted on event config changes - cmd.c:
//			DEL175: only enqueue footers for pulse counting in dig_channel_midnight_task() - dig.c:
//			DEL185: limit minutes to < 60 in #PRT command - cmd.c:
//			DEL188: error in line 673 corrected to read point_value from file - ana.c:
//			DEL180: in DIG_print_immediate_values() add event enabled test to event type > 0 test for event value printing - dig.c:
//			DEL183: allow #ACO=0,00 to disable action without failing on channel number out of range - cmd.c:
//			DEL181: add dig_first_in_system() to find first enabled pump in a system for printing immediate values - dig.c:
// V3.23	add ANA_insert_derived_header() - ana.h: ana.c:
//			call ANA_insert_derived_headers() in #ADD command to ensure headers inserted on derived config changes - cmd.c:
//			call DIG_configure_channel() from DCCx and CECDxx to set headers and start/stop logging - cmd.c:
//			no longer any need for DIG_insert_event_headers() - done in dig_start_stop_logging() - dig.h: dig.c:
//			if #PRT successful in setting new time - set pump time switched on to time now - cmd.c:
//**********************************************************************************************************************************//
// V3.24	Change method of calculating system pump flow rates and volumes - corrects methods defined on 060612 V3.12 - dig.c:
//			PFR is in units per second and defines units for flow rate logging - i.e. DCC units enum must match PFR
//			logged flow rate is modified by #CEC rate_enumeration
//			tcalx10000 is used only in volume logging (rate_enum = 0) and totalising (metering)
//
//			Rewrite event value logging to do this correction - keep everything in PFR units.
//			Use tcalx10000 when a volume is required to be displayed, logged or compared
//**********************************************************************************************************************************//
// V3.25	Test for insertion of derived header independent of ordinary header - ana.c:
// V3.26	Script file work with PIC24FJ256GB210
//			Updated cfs.c and cfs.h from Xilog+ V3.04
//			Remove CFS_FAILED_TO_OPEN state and associated code - cfs.h: cfs.c: mdm.c: com.c: msg.c: log.c: ftp.c: pwr.c: dsk.c:
//			Add and modify code for running script files on alarm events - alm.c: cmd.c: scf.c: scf.h: cfs.h:
// V3.27	DEL190: debug #TOD faults - dop.c:
//					changes to ALM_update_profile() - alm.c
//			DEL194: debug #NVM faults - dop.c: dop.h: 
//					add new error code 27 CMD_ERR_NIVUS_BUSY cmd.c: cmd.h:
//			DEL195: always call DOP_configure_sensor() for any doppler set up command - cmd.c:
//					remove individual DOP_configure_xxx() functions - dop.c: dop.h:
// V3.28    DEL192: do not call MDM_change_time() in RTC_add_time() as it is called in RTC_set_time() - rtc.c:
// 					if modem already off, do not update modem on time in MDM_power_off() - mdm.c:
//			DEL193: In dig_check_event_debounce() if both edge triggering - update expected interrupt edge from present state of input line
//					as we may have had a change of state during debounce time - dig.c:
// V3.29	DEL201: add wait for file system to task and remove from cop_log_control() - cop.c:
//			DEL202:	correction to dig_get_immediate_pump_flow() - move zero flow returned if pump off inside individual test - dig.c:
//			DEL203: in dig_check_event_log() move 'footer at midnight' test inside type tests to prevent spurious footers in pump system logs - dig.c:
//			DEL205: correction to output string in SCF_execute_next_line() to "....%s\\%s" - scf.c:
//			DEL206: correction to DIG_immediate_value() to return units per second in all cases of rate enumeration - dig.c:
//					bring up to date with Xilog+ V3.05
//					in COM_task() state COM_SET_GPRSxx set gsm network id to zero if roaming and fail to get IP - com.c:
//					use new log state LOG_BATT_DEAD instead of LOG_VOLTAGE_TOO_LOW - alm.c: cfs.c: log.h: mdm.c: cop.c: log.c:
//					call PWR_set_pending_batt_test() when ignite modem - mdm.c:
//					correction to usb_send_disconnected_alarm() - add send ftp low battery alarm - usb.c:
//					add send battery alarm if flag set to log_new_day_task() - log.c:
//					cmd_bv uses new PWR variable for battery voltage when failed - cmd.c:
//					cmd_imv and cmd_rda use new function PWR_set_pending_batt_test() - cmd.c:
//					use PWR_measure_in_progress() in CMD_task() - cmd.c:
//					read diode offset from ALMBATT file at initialisation - main.c:
//					new flag for battery alarm sent, new variable and functions for battery failure management and transmission of alarm - pwr.h: pwr.c:
//					use strcpy instead of memcpy for path and filename - ensures null termination - scf.c:
// V3.30			in PWR_tx_internal_batt_alarm() also clear subchannel message enable flags when battery low - pwr.c:
//					new code in PDU_time_for_batch() to deal with subchannel event value logging transmission enable flag and SMS types - pdu.c:
//					correction to code in ftp_channel_messaging() to preserve ordinary event messaging - ftp.c:
// V3.31			in PWR_tx_internal_batt_alarm() also clear doppler and derived analogue message enable flags when battery low - pwr.c:
//					correction to use lower case string in STR_string_match() in cmd_done() - cmd.c:
//					use alm_day_bcd to force recalc of wakeup times at midnight - alm.c:
//					reposition com_day_bcd = RTC_now.day_bcd at end of COM_task and in COM_init - com.c:
//					reposition cop_day_bcd = RTC_now.day_bcd at end of COP_task and in COP_init - cop.c:
//					initialise tsync_day_bcd & tsync_correction_day_bcd in TSYNC_init() - tsync.c:
//					initialise dsk_day_bcd in DSK_init() - dsk.c:
//					revised/added check on CFS_open() == CFS_OPEN in cmd_frd() and cmd_frl() - cmd.c:
//					in MDM_task state MDM_CONFIG shut down if file system not open - mdm.c:
//					add double check of file system open in log_write_to_file() before calling FSfopen() - log.c:
//					use CFS_open() != CFS_OPEN test to keep file system awake in USB_task() states READ_FILE and WRITE_FILE - usb.c:
// V3.32			revise use of CFS_open() in READ_FILE and WRITE_FILE states - usb.c:
//					reposition alm_day_bcd = RTC_now.day_bcd after midnight test - alm.c:
//					reposition dsk_day_bcd = RTC_now.day_bcd after midnight test - dsk.c:
//					return cop_day_bcd = RTC_now.day_bcd to cop_new_day_task() - cop.c:
//					return com_day_bcd = RTC_now.day_bcd to com_new_day_task() - com.c:
//					DEL201: fix in V3.29 keeps SD card on permanently - do it properly by enqueueing a log task - cop.c:
//					add function COP_create_file_string() for log_write_to_file() to call - cop.h: cop.c:
//					add indices for control output logging - move SMS and DERIVED MASKS to make room - log.h:
//					add to log_write_to_file() and LOG_enqueue_value() for control output logging - log.c:
// V3.33			remove existing control output logging changes and implement a new scheme, separate from existing logging channels
//					it still requires queueing of coded information, but separately identifiable as control output logs 
//					new code in LOG_enqueue_value() and log_write_to_file() for control outputs - log.c:
//					new log_control_active_mask and log_control_write_mask - log.c:
//					change control output logging indices and masks - log.h
//					change in enqueueing control output logs - cop.c:
//					wait for logging and pdu to complete and file system open in states ALM_MESSAGE_PENDING, ALM_GET_NEXT_LEVELS and ALM_SCRIPT_PENDING - alm.c:
// V3.34			change SLP_wakeup_times[] to include shadow channels for 3 channel loggers - slp.c:
//					change SLP_NUM_WAKEUP_SOURCES to include shadow channels for 3 channel loggers - slp.h:
// V3.35			add FTP_reset_active_retrieval_info() to cmd_mps - cmd.c
//					in FTP_activate_retrieval_info(), set seek pos to zero if not active - ftp.c
//					don't clear ftp flags and seek pos when start a channel that is already active and has logged data - ftp.c
// V3.36			don't call FTP_deactivate_retrieval_info() when stop a channel logging - ana.c: dig.c: dop:
//					call FTP_reset_active_retrieval_info(), then synchronise logging channels, in cmd_dt, cmd_tc and cmd_mps - cmd.c:
//					FTP_reset_active_retrieval_info() to clear ftp flags, makes FTP_deactivate_retrieval_info() redundant - ftp.c:
//					remove FTP_deactivate_retrieval_info() - ftp.h:
//					V3.35 and V3.36 changes allow ftp of window logging after channel has been turned off
// V4.00			Add product type for XILOG_GPS
//					Add new files - gps.h: gps.c:
//					add build option HDW_GPS for XILOG_GPS product - HardwareProfile.h:
//					disable if HDW_GPS defined - ana.h: ana.c:
//					if HDW_GPS disable all analogue calls and functions - cmd.h: main.c: cmd.c: log.c: cal.c: slp.c: slp.h: ftp.c: pdu.c: alm.c: pwr.c:
//					add gps data to all ftp headers, and trigger GPS automatic task at midnight - log.c:
// V4.01			change to task manager to call US_Device_Tasks() in between other tasks to cope with 
//					fast response required for USB3 by some laptops
// V4.02			rewrite #NVB to send 38400 baud rate command to NIVUS at 9600 baud - used when NIVUS is set to wrong baudrate - ser.c:
//					add COP_EVENT case to COP_value_to_string() - cop.c:
//					reorganise cop_state into an array - cop.c:
//					do not count pulse width until out of COP_IDLE_WAIT state - cop.c:
//					ensure file system is woken up when task has detected it is time to do something - cop.c:
//					rewrite interrupt event handling in COP_task() - cop.c:
//					correction to dig_check_event_alarm() - need to zero normal_alarm_amount, not normal_alarm_count - dig.c:
//					need high speed clock if UART is on in RS485 version - slp.c:
//					ensure clock is switched to PLL when UART is on - ser.c:
// V4.03			use baud rate calculation for UART3, not fixed value (this was wrong for disconnected operation) - gps.c:
// V4.04			add field to cmd_gps for time of day to get fix - cmd.c:
//					call GPS_recalc_wakeup() when time of day changes - cmd.c:
//					add config for time of day for gps fix - gps.c: gps.h:
//                  add GPS_wakeup_timer and set timer if day changed for time of day for gps fix - gps.c: gps.h:
//					check GPS_wakeup_timer for GPS fix request - slp.c:
//					add a wakeup source for GPS - slp.h:
//					remove gps fix request from midnight task - log.c:
//					hold TURN_AN_ON low if gps product - pwr.c: 
//					call GPS_recalc_wakeup() when time of day changes - tsync.c:
// V4.05			disable GPS in freight mode - gps.c: cmd.c:
// V4.06			File system modifications for faster creation of new files and avoidance of file system corruption
// V4.07			Fix problem with broken #DIR command over USB
// V4.08			Fix problem with non-GPS build removing analogue power from transducer before reading complete
// V4.09 290514     GPS version - set TURN_AN_ON = false after power measurements to ensure low current - pwr.c:
// V4.10 040614 	in channel task only set up event times if event sensor enabled and valid - dig.c:
// V4.11 260814	PB	Correction of faults with RS485 type - using the NIVUS doppler sensor:
//					line 234 - requires '~' to invert sum of flags for clearing - pwr.c:
//					rewrite DOP_task to include send velocity and send height in measurement loop - dop.c:
//					add DOP_sensor_present flag - dop.h: dop.c:
//					set DOP_sensor_present if doppler initialisation is successful
//					if changes from false to true at this point, insert all log headers, as sensor has been reconnected
//					clear it if any comms fault with RS485 - dop.c:
//					monitor it when #RDA5 or #ISV acted on, and do not insert data if false - cmd.c:
//					add rounding of very small negative numbers to calculation of flow, so does not log as -0.0 - dop.c:
//					Add CFS_gps_name "GPS.TXT" - cfs.h:
//					add GPS.TXT to headers if file exists if not a GPS product - log.c:
//		 040914 PB  rework calculation of flow rate from doppler sensor data, look up table and area calc - dop.c: dop.h:
// V4.12 221014 PB  RS485 #alms1\2\3 and #almsd1 to be recognised by cmd_report_alm_channel_ok() - cmd.c:
// V5.00 221014 PB  New version for issue 4 PCBs - route GPS comms via RS232RX port on RE8 - main.c:
//					relabel RA0-HDW_HW_REV_BIT_0, RA1-HDW_HW_REV_BIT_1, RA7-HDW_GPS_POWER_CTRL and RG14-HDW_RS485_BOOST_ON - HardwareProfile.h:
//					add HDW_revision global variable - set on power up - HardwareProfile.h:
//					GPS on/off control via RA7 - gps.c:
//		 231014 PB	RS485 boost control via RG14 - dop.c:
//                  new undocumented command #rhr - read hardware revision bits - return value of bits set with pullups - cmd.c:
//					new CMD_ERR_INCOMPATIBLE_HARDWARE - cmd.h:
//					add hardware/software mismatch tests to cmd_li(), returning new CMD_ERR_INCOMPATIBLE_HARDWARE - cmd.c:
//					field 12 of cmd_li() reply - 'S' for RS485 and 'G' for GPS versions - cmd.c:
//
// V5.01 111114 PB  set hardware revision bits to inputs from start - HardwareProfile.h:
//
// V5.02 161214 MA	time sync debugged
//
// V5.03 070115 MA	RS485 build accepts #ALMAx command for analogue channels
//
// V5.04 DEL222 MA  log_c_file updated to reflect addition of LOG_GPS_FILE
//		 DEL220 MA	FTP_activate_retrieval_info needed to be called for sub-channel B when configured for event logging
//		 DEL224 MA	ALM routines now pick up units index from #CECDxx command values if appropriate
//		 270115 MA	SLP_NUM_WAKEUP_SOURCES was broken for some builds. Replaced by const SLP_num_wakeup_sources
//
// V5.05 050215 MA	ALM.c corrected to allow RS485 build, & DOP.h modified to avoid Visual Studio warning
//		 DEL221 MA	DOP.c modified to allow transducer to be disconnected without spinning, & will log NO_VALUE entries while
//					in this state.
// V5.06 100215 MA	Quiescent state of HDW_RS485_RE_N flipped to fix high current problem
//
// V5.07 DEL214 MA	Internal battery volts, external battery volts and GSM signal strength (followed by % sign) added at end of all footers.
//		 DEL223 MA	Typo fixed in dig_check_derived_min_max
//
// V5.08 DEL228 MA	Integer constants changed to float for calculation of RDA5 value (DOP_channel.velocity_value).
//		 DEL227 MA	dig_events_task was jumping out of loop if DxA not configured for event logging, so DxB would not be checked unless
//					DxA enabled for event logging.
//
// V5.09 230415 MA	dig_channel_midnight_task was enqueueing two footers for derived channels.
//		 230415 MA	Only append signal strength & batt volts to digital footers (PrimeWorks couldn't cope on analogue)
//
// V5.11 090715 MA	5s damping programmed to Nivus transducer. Max abs value of the samples taken as the value.
//
// V5.12 100715 MA	#DDAC command added
//
// V5.13 150715 MA	Encoding of NO_VALUE fixed. Last doppler sample used as logged value, rather than max or average.
//					Nivus sensor only powered if external power available. Bug fix - headers now added to SMS DS1 file.
//
// V5.16 210715 MA	DEL225: #RSD to parse channel S3, & fix wrong channel nos in SMS msgs
//
// V5.17 010915 MA  DEL231: fixed #ACO problem if channel ID field null
//
// V5.18 290915 MA	Changed handling ALMBATT config file to ensure that if the file exists, but there is no string for
//					an alarm, the default string is used.
//
// V5.19 051115 MA	Fixed problem where control output 2 would only work if output 1 enabled.
//
// V5.20 DEL235 MA	Stop #TOD script lines appending to current.hcs.
//
// V6.00 260116 PQ	XiLog+++ support. Reset line floats, baud rate set to 115200.
//					MODBUS version
//
// V6.01 280617 PQ	Mark's Xliog updates, GL865 support
//
// V6.02 280617 PQ	4GB SD card support

#include "HardwareProfile.h"

// Keep old modem build 1 below new, e.g. GE864 version 5.31 corresponds to GL865 version 6.31
// Update BOTH values when up-versioning
#ifdef HDW_MODEM_GE864
#define VER_FIRMWARE_BCD	0x0506
#else
#define VER_FIRMWARE_BCD	0x0606
#endif

