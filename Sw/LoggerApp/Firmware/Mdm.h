/******************************************************************************
** File:	Mdm.h
**
** Notes:	Modem comms
*/

/* CHANGES
** v2.36 281010 PB 	Remove parameter in MDM_log_use
**
** V2.50 030211 PB  DEL119 - adjustments to MDM_ready() to give it a parameter to indicate test or not
**
** V2.65 070411 PB  added conditional compilation ifndef HDW_PRIMELOG_PLUS
**
** V2.90 090811 PB  DEL152 - new modem ready state MDM_TEST_WAIT for a delay before ready for sigtst and nwtst
**
** V3.01 140612 PB  removed MDM_retry()
*/

#ifndef HDW_PRIMELOG_PLUS
// Modem states:
#define MDM_OFF					0		// modem unpowered
#define MDM_IGNITE				1		// initialise uart and ignite modem
#define MDM_IGNITION			2		// power on, pulse asserted
#define MDM_CHECK_POWER			3		// check modem happy
#define MDM_INITIALISE			4		// send initialisation string
#define MDM_INITIALISING		5		// wait for reply from initialisation strings
#define MDM_SELECT_OPERATOR		6		// select GSM network
#define MDM_CHECK_PIN			7		// See if SIM card requires PIN
#define MDM_SEND_PIN			8		// Send PIN
#define MDM_CONFIG				9		// Send the 1-off config commands
#define MDM_PRE_REG				10		// Execute commands done every power-up before registration
#define MDM_SIGN_ON_WAIT		11		// wait - we can go to sleep before looking for CREG: x,1 or x,5
#define MDM_SIGNING_ON			12		// look for CREG: x,1 or x,5
#define MDM_POST_REG			13		// Execute commands done every power-up after registration
#define MDM_GET_OPERATOR		14		// Read operator (AT+COPS?)
#define MDM_ON					15		// modem signed up to N/W (standby or fully on)
#define MDM_TEST_WAIT			16		// wait for timeout for modem registration for sigtst and nwtst
#define MDM_POWER_CYCLE			17		// power down then back up
#define MDM_FAIL_TO_REGISTER	18		// modem has failed to register and is waiting to retry - restart controlled by retry strategy
#define MDM_SHUTTING_DOWN		19		// signing off

// Command status:
#define MDM_CMD_BUSY	0
#define MDM_CMD_SUCCESS	1
#define MDM_CMD_FAILED	2

extern uint8 MDM_cmd_status;

extern uint8 MDM_state;
extern uint16 MDM_tx_delay_timer_x20ms;
extern uint16 MDM_cmd_timer_x20ms;
extern uint32 MDM_wakeup_time;

extern char *MDM_retry_ptr;
extern char *MDM_tx_ptr;

extern char MDM_pin[8];

extern FAR char MDM_tx_buffer[544];		// PB increased buffer sizes 9.7.09 and 9.12.09
extern FAR char MDM_rx_buffer[512];		// PB make both FAR 9.12.09
#endif

void MDM_preset_state_flag(void);		// PB added 20.04.10
bool MDM_log_use(void);					// PB added 25.03.10 modified 20.04.10, 28.10.10
bool MDM_cmd_in_progress(void);
bool MDM_ready(bool ignore_registration);
void MDM_shutdown(void);
void MDM_power_off(void);
void MDM_change_time(uint32 old_time_sec, uint32 new_time_sec);  // PB added 25.03.10
void MDM_recalc_wakeup(void);
void MDM_task(void);
void MDM_clear_rx_buffer(void);
void MDM_send_cmd(char *p);
bool MDM_clear_to_send(void);
bool MDM_do_command_file(char *filename);
bool MDM_can_sleep(void);
void MDM_init();

