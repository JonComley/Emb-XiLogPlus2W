/******************************************************************************
** Function:	Main.c
**
** Notes:	
*/

/* CHANGES
** V2.40 061210 MA	CONFIG3 settings added, to ensure code can be updated by bootloader
**
** V2.52 150211 PB	call to FRM_init() (fram init) after reading build info from cal
**
** V3.04 071211 PB  Waste Water - add pin allocations for RS485 on UART1 
**
** V3.08 280212 PB  ECO product - call PWR_init() before reading build cal
**								  add call to PWR_task() before LOG_task()
**
** V3.26 170613 PB	remove call to FRM_init()
**
** V3.29 171013 PB  bring up to date with Xilog+ V3.05
**					read diode offset from ALMBATT file at initialisation
**
** V4.00 220114 PB  if HDW_GPS disable all analogue calls and functions
**					program RPINR17 = 33 to put UART3 RX on pin RPI33/RE8 
**					FOR PROTOTYPE program RPINR17 = 4 to put UART3 RX on pin RP4/RD9 
**					call GPS_init() and GPS_task() 
**
** V5.00 231014 PB  route RS232 RX (RE8) to UART3 for GPS
**					read and condition hardware revision inputs at power up
**					use hardware revision for choice of route for GPS RX serial data
*/

/** I N C L U D E S **********************************************************/

#include "Custom.h"

#ifdef WIN32
#define extern
double ceilf(const double f) {return 0.0;}
double fabsf(const double f) {return 0.0;}
float powf(float x, float y) {return 0.0;}
#endif
#include "Compiler.h"
#undef extern

#include "MDD File System/SD-SPI.h"
#include "MDD File System/FSIO.h"

#include "HardwareProfile.h"

#include "Rtc.h"
#include "Cfs.h"
#include "Mdm.h"
#include "Tim.h"
#include "Msg.h"
#include "tsync.h"
#include "Com.h"
#include "Cal.h"
#include "Usb.h"
#include "Log.h"
#include "Sns.h"
#include "Dig.h"
#include "Ana.h"
#include "Slp.h"
#include "pdu.h"
#include "Cmd.h"
#include "ftp.h"
#include "alm.h"
#include "Frm.h"
#include "Cop.h"
#include "Ser.h"
#include "Dop.h"
#include "Pwr.h"
#include "Scf.h"
#include "gps.h"
#include "modbus.h"

#ifdef HDW_DBG
#ifndef WIN32
#warning HDW_DBG feature enabled.
#endif
#endif

/******************************************************************************/
																					// Configuration Bits: Primary XT OSC with PLL
_CONFIG1(JTAGEN_OFF & FWDTEN_OFF & ICS_PGx2 & GWRP_OFF & WINDIS_OFF & GCP_ON)

																					// Clock switching enabled
_CONFIG2(FNOSC_FRCDIV & POSCMOD_XT & PLL_96MHZ_ON & PLLDIV_NODIV & IESO_OFF &
		 FCKSM_CSECMD & OSCIOFNC_OFF & DISUVREG_OFF & IOL1WAY_OFF) 

																					// Disable erase/write protect of all memory regions.
_CONFIG3(WPCFG_WPCFGDIS & WPDIS_WPDIS)		

/** VARIABLES ******************************************************/

int rcon_at_reset;
uint16 HDW_revision;
//int dbg __attribute__ ((persistent));

/******************************************************************************
** Function:	Initialise hardware
**
** Notes:	
*/
void init_hardware(void)
{
	LATA = HDW_LATA;
	TRISA = HDW_TRISA;
	LATB = HDW_LATB;
	TRISB = HDW_TRISB;
	LATC = HDW_LATC;
	TRISC = HDW_TRISC;
	LATD = HDW_LATD;
	TRISD = HDW_TRISD;
	LATE = HDW_LATE;
	TRISE = HDW_TRISE;
	LATF = HDW_LATF;
	TRISF = HDW_TRISF;
	LATG = HDW_LATG;
	TRISG = HDW_TRISG;

	// read hardware version bits 0 and 1 on RA0 and RA1 - HW_TRISA sets them as inputs, HW_LATA sets them both low
	HDW_revision = PORTA & 0x0003;						// read state
	TRISA = (HDW_TRISA & 0xFFFC) | HDW_revision;		// set any floating (zero) lines to outputs for minimum current

	// Analogue input pins:
	AD1PCFGL = HDW_PCFG;

	// Disable USB module & detach from bus. Allows app code to connect to USB
	// after being loaded by bootloader.
	U1CON = 0;

	// Do all the peripheral pin set-ups here, so registers can be locked.
	// Set up peripheral pin select for SPI 1 (SD card interface):
    _SDI1R = 43;	// SPI1_DI on RPI43
    _RP17R = 7;		// SPI1_DO on RP17
    _RP10R = 8;		// SPI1_CLK on RP10

	// Set up peripheral pin select for UART 2 (GSM modem):
	_U2RXR = 19;	// Rx: RP19/RG8
	_RP26R = 5;		// Tx: RP26/RG7
	_U2CTSR = 35;	// CTS input: RPI35/RA15
	_RP2R = 6;		// RTS output: RP2/RD8

#ifdef HDW_GPS
	// Set up peripheral pin select for UART 3 (GPS RX) depending on board issue:
	if (HDW_revision == 0)
		_U3RXR = 4;		// RX: RP4/RD9 FOR PROTOTYPE
	else
		_U3RXR = 33;	// RX: RPI33/RE8	
#endif

#ifdef HDW_RS485
																					// Set up peripheral pin select for UART 1 (RS485 interface):
	_U1RXR = 4;																		// Rx: RP4/RD9
#if (HDW_NUM_CHANNELS == 3)															// for 3 channel
	_RP3R = 3;																		// Tx: RP3/RD10
#else																				// for 9 channel
	_RP24R = 3;																		// Tx: RP24/RD1
#endif
#endif

	// Debug timer: T5, select 32768Hz clock input
	_T5CKR = 37;	// T5 input on RPI37

	// enable USB monitor interrupt  on CN74 / RF8 / RP15
	CNEN5bits.CN74IE = 1;
	
	// modem ring indicator on CN48 / RC4 / RPI41
	// is set up before going to sleep
	// depending on state of modem power control

	// set CN priority level 1
	_CNIP0 = 1;
	_CNIP1 = 0;
	_CNIP2 = 0;

#if (HDW_NUM_CHANNELS == 9)
	// IC2: Sensor PIC 2 busy line on RB8 / RP8
	RPINR7bits.IC2R = 8;

	// INT1: D1A event interrupt on RPI40
	RPINR0bits.INT1R = 40;
	// INT2: D1B event interrupt on RP1
	RPINR1bits.INT2R = 1;
	// INT3: D2A event interrupt on RP9
	RPINR1bits.INT3R = 9;
	// INT1: D1A event interrupt on RP14
	RPINR2bits.INT4R = 14;
	// set interrupt priorities to 1 (don't interrupt CPU)
	_INT4IP = 1;					
	_INT3IP = 1;
#else
	// INT1: D1A event interrupt on RPI38
	RPINR0bits.INT1R = 38;
	// INT2: D1B event interrupt on RPI39
	RPINR1bits.INT2R = 39;
#endif
	// set interrupt priorities to 1 (don't interrupt CPU)
	_INT2IP = 1;					
	_INT1IP = 1;					

	// lock peripheral pin select registers:
	asm("MOV	#OSCCON, w1");
	asm("MOV	#0x46, w2");
	asm("MOV	#0x57, w3");
	asm("MOV.b	w2, [w1]");
	asm("MOV.b	w3, [w1]");
	asm("BSET	OSCCON,#6");
}

/******************************************************************************
** Function:	Main
**
** Notes:	
*/
int main(void)
{
	rcon_at_reset = RCON;
	RCON = 0;
	//dbg_at_reset = dbg;

	init_hardware();

	RTC_init();
	TIM_init();
	LOG_init();																		// must go here to set LOG_state before file system accessed
	CFS_init();
	MSG_init();
	MDM_init();
	PWR_eco_init();
	CAL_read_build_info();
//	FRM_init();
#ifdef HDW_RS485
	MOD_init();
	SER_init();
	//DOP_init();
#else
	DIG_init();
#endif
	PDU_init();
	TSYNC_init();
	COM_init();
	ALM_init();
	FTP_init();
	COP_init();
#ifdef HDW_GPS
	GPS_init();
#endif

	if (CFS_state == CFS_OPEN)
	{
		if (CFS_file_exists("\\", "app.hex"))
			FSremove("app.hex");
	}

	LOG_enqueue_value(LOG_ACTIVITY_INDEX, LOG_MAIN_FILE, __LINE__);					// reset

#ifndef HDW_GPS
	ANA_boost_time_ms = ANA_DEFAULT_BOOST_TIME_MS;
#endif

	SCF_execute((char *)CFS_config_path, (char *)CFS_current_name, false);

	PWR_drive_debug_led(true);
	PWR_read_diode_offset();														// read diode offset from ALMBATT file

	//_RTCIF = true;																// DEBUG ONLY
	do
    {
		TIM_task();
		RTC_get_time_now();
		USB_SUB_TASK();

		SNS_task();
#ifndef HDW_GPS
		ANA_task();
#endif
#ifdef HDW_RS485
		//DOP_task();
		SER_task();
		MOD_task();
#else
		DIG_task();
#endif
		ALM_task();																	// must go after DIG_task or CPU may go back to sleep
		PWR_task();
		USB_SUB_TASK();

		LOG_task();
		COP_task();
		PDU_task();
		FTP_task();
		USB_SUB_TASK();

		CFS_task();
		TSYNC_task();
		USB_SUB_TASK();

		COM_task();
		MDM_task();
		MSG_task();
		CMD_task();
		USB_task();
#ifdef HDW_GPS
		GPS_task();
#endif
		SLP_task();

	} while (true);
    
    return 0;
}

/** EOF main.c ***************************************************************/

