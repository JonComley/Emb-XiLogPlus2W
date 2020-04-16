/********************************************************************
 FileName:     	HardwareProfile.h
 ********************************************************************/
/*
** CHANGES
**
** v2.83 270711 pb 9 channel digital channels are NOT inverted - CH1 on RD3, CH2 on RD2 - power on state is OFF
**
** V3.04 221211 PB Waste Water RS485 logger is a build option - (replaces digital channels D1A and D1B)
**
** V3.17 091012 PB RS485 must use hardware revision 4 with second voltage boost circuit
**
** V4.00 220114 PB add build option HDW_GPS for XILOG_GPS product 
**
** V4.05 070514 PB removed ATEX option
**
** V5.00 221014 PB relabel RA0-HDW_HW_REV_BIT_0, RA1-HDW_HW_REV_BIT_1, RA7-HDW_GPS_POWER_CTRL and RG14-HDW_RS485_BOOST_ON
**				   add HDW_revision global variable - set on power up
**
** V5.01 111114 PB set hardware revision bits to inputs from start
*/

#ifndef HARDWARE_PROFILE_H
#define HARDWARE_PROFILE_H

#include "custom.h"
#include "usb_config.h"		// Code compiles without this, but does not work!

#include <p24fxxxx.h>
#ifdef WIN32
#define Sleep()
#define Nop()
#endif

// FOR DEBUG ONLY:
//#define HDW_DBG		1

// Master define for 3 or 9-ch hardware.
#define HDW_NUM_CHANNELS		3

// Modem type definition: Original = GE864, new = GL865 / UL865 (default)
#define HDW_MODEM_GE864		1

// Master define for XILOG_GPS hardware, with option for truck mode on by default
//#define HDW_GPS				1
//#define HDW_GPS_TRUCK_MODE	1

// Master define for RS485 serial port product - comment this out for XilogPlus
// this definition is required for RS485 MODBUS
#define HDW_RS485				1

// Master define for product type - comment this out for XilogPlus
//#define HDW_PRIMELOG_PLUS		1

// Master define for hardware revision. Supported values: XiLog+ 3, PrimeLog+ 2, RS485 4
#ifdef HDW_PRIMELOG_PLUS
  #define HDW_REV					2

// check for channel definition mismatch
  #if (HDW_NUM_CHANNELS != 3)
    #error can only define 3 channel hardware for Primelog+
  #endif
#else
  #ifdef HDW_RS485
    #define HDW_REV					4
  #else
    #ifdef HDW_GPS
      #define HDW_REV				6
    #else	// XiLog+
      #define HDW_REV				3
    #endif
  #endif
#endif 
// check for RS485 definition mismatch
#ifdef HDW_RS485
  #if ((HDW_NUM_CHANNELS != 3) || (HDW_REV != 4))
    #error can only define 3 channel revision 4 hardware with RS485
  #endif
#endif
// check for GPS definition mismatch
#ifdef HDW_GPS
  #if ((HDW_NUM_CHANNELS != 3) || (HDW_REV != 6))
    #error can only define 3 channel revision 6 hardware with GPS V5.00
  #endif
#endif

extern uint16 HDW_revision;

uint32 SLP_get_system_clock(void);

#define GetSystemClock()        SLP_get_system_clock()

//#define GetSystemClock()        32000000
#define GetPeripheralClock()    GetSystemClock()
#define GetInstructionClock()   (GetSystemClock() / 2)

// Debug output: replaced by #DBG command
//#define TEST_LED_ENABLED		1		// comment this out if not required
//#ifdef TEST_LED_ENABLED
//#define HDW_SET_TEST_LED(X)		HDW_CONTROL1_ON = (X)
//#ifndef WIN32
//#warning: Control 1 output assigned to test LED for debug purposes
//#endif
//#else
//#define HDW_SET_TEST_LED(X)
//#endif

/** SD Card *********************************************************/

#define USE_SD_INTERFACE_WITH_SPI

#define SD_CD				false				// assume card always present
#define SD_WE				false				// assume card writeable
#define SPICLOCK			_TRISF4
#define SD_CS				HDW_SPI1_SS_N
#define SD_CS_TRIS			_TRISD15

#define SPICON1				SPI1CON1
#define SPISTAT				SPI1STAT
#define SPIBUF				SPI1BUF
#define SPISTAT_RBF			SPI1STATbits.SPIRBF
#define SPICON1bits			SPI1CON1bits
#define SPISTATbits			SPI1STATbits
#define SPIENABLE           SPISTATbits.SPIEN

// Will generate an error if the clock speed is too low to interface to the card
//#if (GetSystemClock() < 100000)
//    #error Clock speed must exceed 100 kHz
//#endif


/*********************************************************************/
/******************* Pin and Register Definitions ********************/
/*********************************************************************/

/** Port A ***********************************************************/
#define HDW_HW_REV_BIT_0		_RA0
#define HDW_HW_REV_BIT_1		_RA1
#define HDW_HW_REV_BIT_0_OUT	_LATA0
#define HDW_HW_REV_BIT_1_OUT	_LATA1
#define HDW_EXT_SUPPLY_N		_RA2
#define HDW_SPARE_A3			_LATA3
#define HDW_CONTROL2_ON			_LATA4
#define HDW_CONTROL1_ON			_LATA5
#define HDW_TURN_AN_ON			_LATA6
#define HDW_GPS_POWER_CTRL		_LATA7		// active low
#define HDW_AN_0V_REF_2			_RA9
#define HDW_AN_VREF_2			_RA10		// 3V0
#ifdef  HDW_PRIMELOG_PLUS
  #define HDW_SPARE_RA14		_RA14		// Radio interrupt
  #define HDW_FRAM_WP_N			_LATA15
#else
  #define HDW_MODEM_2V8_MON		_RA14
  #define HDW_MODEM_CTS_N		_RA15
#endif

#if (HDW_NUM_CHANNELS == 9)
  #define HDW_TRISA				_16BIT(_B11111101, _B00000111)
  #define HDW_LATA				_16BIT(_B11111101, _B00000000)
#else	// 3-ch HW
  #ifdef  HDW_PRIMELOG_PLUS
    #define HDW_TRISA			_16BIT(_B00111111, _B00000111)
    #define HDW_LATA			_16BIT(_B00111111, _B00000000)
  #else
    #define HDW_TRISA			_16BIT(_B11111111, _B00000111)
    #define HDW_LATA			_16BIT(_B11111111, _B10000000)
  #endif
#endif

/** Port B ***********************************************************/
#if (HDW_NUM_CHANNELS == 9)

#define HDW_AN_VREF_2V5_2B	_RB0	// Not populated - idle o/p low
#define HDW_CH1_FLOW_2		_RB1
#define HDW_BAT_7V2_VMON	_RB2
#define HDW_EXT_SUPPLY_VMON	_RB3
#define HDW_CH1_FLOW_IMON	_RB4
#define HDW_CH2_FLOW_IMON	_RB5
#define HDW_PGC				_RB6
#define HDW_PGD				_RB7
#define HDW_SNS2_BUSY		_RB8
#define HDW_CH2_FLOW_1		_RB9
#define HDW_BATTEST_INT_ON	_LATB10
#define HDW_BATTEST_EXT_ON	_LATB11
#define HDW_MODEM_RESET		_LATB12		// Floating for GL865 / UL865 modem
#define HDW_SPARE_B13		_RB13
#define HDW_CH2_FLOW_2		_RB14
#define HDW_RS485_DE		_LATB15

#ifdef HDW_MODEM_GE864
#define HDW_TRISB			_16BIT(_B01100011, _B00111110)
#else
#define HDW_TRISB			_16BIT(_B01110011, _B00111110)
#endif
#define HDW_LATB			_16BIT(_B01100011, _B00111110)

// Analog pins:
#define HDW_ADC_CHANNEL_2V5_REF			0
#define HDW_ADC_CHANNEL_INT_BAT			2
#define HDW_ADC_CHANNEL_EXT_SUPPLY		3
#define HDW_ADC_CHANNEL_CH1_FLOW_IMON	4
#define HDW_ADC_CHANNEL_CH2_FLOW_IMON	5

#define HDW_PCFG			_16BIT(_B11111111, _B11000010)

#else	// 3-channel hardware

#define HDW_AN_DIFF1_2		_RB0
#define HDW_AN_DIFF2_2		_RB1
#define HDW_AN_CURRENT_2	_RB2
#define HDW_AN_VREF			_RB3	// NC on XiLog+, 2V5 on PrimeLog+
#define HDW_BAT_7V2_VMON	_RB4
#define HDW_L_FLOW_IMON		_RB5
#define HDW_PGC				_LATB6
#define HDW_PGD				_LATB7
#define HDW_EXT_SUPPLY_VMON	_RB8
#define HDW_FRAM_CLK		_LATB9
#define HDW_BATTEST_INT_ON	_LATB10
#define HDW_BATTEST_EXT_ON	_LATB11
#ifdef HDW_PRIMELOG_PLUS
  #define HDW_SPARE_B12		_LATB12
#else
  #define HDW_MODEM_RESET	_LATB12		// Floating for GL865 / UL865 modem
#endif
#define HDW_AN_VOLTS_2		_RB13
#define HDW_SPARE_B14		_LATB14
#define HDW_RS485_DE		_LATB15

#ifdef  HDW_PRIMELOG_PLUS	// Vref connected
  #define HDW_TRISB			_16BIT(_B00000001, _B00111000)
  #define HDW_LATB			_16BIT(_B00000001, _B00111000)
  #define HDW_PCFG			_16BIT(_B11111110, _B11100111)
#else														// XiLog+
	#ifdef HDW_RS485
	  #ifdef HDW_MODEM_GE864
	    #define HDW_TRISB	_16BIT(_B00000001, _B00110000)
	  #else													// GL865 / UL865
	    #define HDW_TRISB	_16BIT(_B00010001, _B00110000)
	  #endif
	#else													// Non-RS485 build
	  #ifdef HDW_MODEM_GE864
	    #define HDW_TRISB	_16BIT(_B10000001, _B00110000)	// this for gps prototype
	  #else													// GL865 / UL865
	    #define HDW_TRISB	_16BIT(_B10010001, _B00110000)
	  #endif
	#endif
  #define HDW_LATB			_16BIT(_B00000001, _B00110000)
  #define HDW_PCFG			_16BIT(_B11111110, _B11101111)
#endif

// Analog pins:
#define HDW_ADC_CHANNEL_2V5_REF			3
#define HDW_ADC_CHANNEL_INT_BAT			4
#define HDW_ADC_CHANNEL_EXT_SUPPLY		8
#endif

/** Port C ***********************************************************/
#if (HDW_NUM_CHANNELS == 9)

#define HDW_CH1_FLOW_CTRL	_LATC1
#define HDW_CH2_FLOW_CTRL	_LATC2
#define HDW_CH1_FLOW_1		_RC3
#define HDW_MODEM_RING		_RC4
#define HDW_OSCI			_RC12		// Main CPU clock
#define HDW_SPARE_C13		_LATC13
#define HDW_CLK_32KHZ		_RC14
#define HDW_OSCO			_RC15		// Main CPU clock

#define HDW_TRISC			_16BIT(_B11010000, _B00011000)
#define HDW_LATC			_16BIT(_B00000011, _B00011000)

#else	// 3-channel hardware

#define HDW_FLOW_1			_RC1
#define HDW_FLOW_2			_RC2
#define HDW_CH1_FLOW_CTRL	_LATC3
#ifdef HDW_PRIMELOG_PLUS
  #define HDW_SPARE_C4		_RC4		// Radio RSSI
#else
  #define HDW_MODEM_RING	_RC4
#endif
#define HDW_OSCI			_RC12		// Main CPU clock
#define HDW_DIFF2_SW2_ON	_LATC13		// NC on PrimeLog+ - should be disabled by config
#define HDW_CLK_32KHZ		_RC14
#define HDW_OSCO			_RC15		// Main CPU clock

#ifdef  HDW_PRIMELOG_PLUS
  #define HDW_TRISC			_16BIT(_B11010000, _B00000110)
  #define HDW_LATC			_16BIT(_B00000011, _B00000110)
#else
  #define HDW_TRISC			_16BIT(_B11010000, _B00010110)
  #define HDW_LATC			_16BIT(_B00000011, _B00010110)
#endif
#endif

/** Port D ***********************************************************/
#if (HDW_NUM_CHANNELS == 9)

#define HDW_SNS1_BUSY			_RD0
#define HDW_RS485_TX_DATA		_LATD1		// NC on HW Rev 2
#define HDW_CH2_POWER_OUT_ON	_LATD2
#define HDW_CH1_POWER_OUT_ON	_LATD3
#define HDW_SNS_WR_N			_LATD4
#define HDW_SNS_RD_N			_LATD5
#define HDW_SPARE_D6			_LATD6
#define HDW_SPARE_D7			_LATD7
#define HDW_MODEM_RTS_N			_LATD8
#define HDW_RS485_RX_DATA		_RD9		// NC on HW Rev 2
#define HDW_SNS2_CS_N			_LATD10
#define HDW_SNS1_CS_N			_LATD11
#define HDW_SPARE_D12			_LATD12
#define HDW_SD_CARD_ON_N		_LATD13
#define HDW_SPI1_DI				_RD14
#define HDW_SPI1_SS_N			_LATD15

#define HDW_TRISD			_16BIT(_B01000000, _B00000001)
#define HDW_LATD			_16BIT(_B00101100, _B00110000)		// SD card off, CS idles low

#else	// 3-channel hardware

#define HDW_SNS1_BUSY			_RD0
#ifdef HDW_PRIMELOG_PLUS
  #define HDW_V_DOUBLE			_LATD1
#else
  #define HDW_DIFF2_SW1_ON		_LATD1
#endif
#ifdef HDW_RS485
  #define HDW_POWER_OUT_ON		_LATD2
#else 
  #define HDW_CH1_POWER_OUT_ON	_LATD2
#endif
#define HDW_MAIN_FLASH_FIRE		_LATD3
#define HDW_SNS_WR_N			_LATD4
#define HDW_SNS_RD_N			_LATD5
#define HDW_DIFF1_SW2_ON		_LATD6
#define HDW_DIFF1_SW1_ON		_LATD7
#ifdef HDW_PRIMELOG_PLUS
  #define HDW_FRAM_SS_N			_LATD8
#else
  #define HDW_MODEM_RTS_N		_LATD8
#endif
#define HDW_RS485_RX_DATA		_RD9
#define HDW_RS485_TX_DATA		_LATD10
#define HDW_SNS1_CS_N			_LATD11
#define HDW_SPI2_DI				_LATD12		// NC
#define HDW_SD_CARD_ON_N		_LATD13
#define HDW_SPI1_DI				_RD14
#define HDW_SPI1_SS_N			_LATD15

#define HDW_TRISD			_16BIT(_B01000010, _B00000001)
#ifdef HDW_PRIMELOG_PLUS
#define HDW_LATD			_16BIT(_B00101001, _B00110000)		// SD card off, CS idles low
#else
  #define HDW_LATD			_16BIT(_B00101000, _B00110000)		// SD card off, CS idles low
#endif
#endif

/** Port E ************************************************************/

#define HDW_SNS_DATA_OUT		LOWBYTE(LATE)
#define HDW_SNS_DATA_IN			LOWBYTE(PORTE)
#define HDW_SNS_DATA_DIRECTION	LOWBYTE(TRISE)
#define HDW_DIRECTION_OUTPUT	0x00
#define HDW_DIRECTION_INPUT		0xFF

#ifdef HDW_PRIMELOG_PLUS
  #define HDW_SPARE_E8			_RE8	// SPI3_SO
  #define HDW_SPARE_E9			_RE9	// SPI3_SI
#else
  #define HDW_RS232_RX_DATA		_RE8
  #define HDW_MODEM_DTR_N		_LATE9
#endif

// Parallel slave port idles with lines output
#ifdef HDW_PRIMELOG_PLUS
  #define HDW_TRISE				_16BIT(_B00000000, _B00000000)
#else
  #define HDW_TRISE				_16BIT(_B00000001, _B00000000)
#endif
#define HDW_LATE				_16BIT(_B00000000, _B00000000)

/** Port F ************************************************************/
#if (HDW_NUM_CHANNELS == 9)

#define HDW_SPARE_F0		_LATF0
#define HDW_RS485_RE_N		_LATF1	// NC on HW Rev 2
#define HDW_SPARE_F2		_LATF2
#define HDW_SPARE_F3		_LATF3
#define HDW_SPI1_CLK		_LATF4
#define HDW_SPI1_DO			_LATF5
#define HDW_USB_MON			_RF8
#define HDW_MODEM_IGNITION	_LATF12
#define HDW_MODEM_PWR_ON_N	_LATF13

#define HDW_TRISF			_16BIT(_B00000001, _B11000000)
#define HDW_LATF			_16BIT(_B00100000, _B00000000)

#else	// 3-channel hardware

#define HDW_SPARE_F0		_LATF0
#define HDW_RS485_RE_N		_LATF1
#define HDW_RELAY_SENSE		_RF2
#define HDW_LED				_LATF3
#define HDW_SPI1_CLK		_LATF4
#define HDW_SPI1_DO			_LATF5
#define HDW_USB_MON			_RF8
#ifdef HDW_PRIMELOG_PLUS
  #define HDW_FRAM_DO		_LATF12
  #define HDW_FRAM_DI		_LATF13
#else
  #define HDW_MODEM_IGNITION	_LATF12
  #define HDW_MODEM_PWR_ON_N	_LATF13
#endif

#ifdef HDW_PRIMELOG_PLUS
  #define HDW_TRISF			_16BIT(_B00010001, _B11110000)
  #define HDW_LATF			_16BIT(_B00000000, _B00000000)
#else
  #ifdef HDW_RS485
    #define HDW_TRISF			_16BIT(_B00000001, _B11110000)
    #define HDW_LATF			_16BIT(_B00100000, _B00000000)
  #else
  #ifdef HDW_1FM
    #define HDW_TRISF			_16BIT(_B00000001, _B11110110)
    #define HDW_LATF			_16BIT(_B00100000, _B00000000)
  #else
    #define HDW_TRISF			_16BIT(_B00000001, _B11110010)
    #define HDW_LATF			_16BIT(_B00100000, _B00000000)
  #endif
  #endif
#endif
#endif

/** Port G ************************************************************/
#define HDW_RS232_TX_ON		_LATG0	// NC on 9-ch & PrimeLog+
#define HDW_RS485_ON_N		_LATG1
#define HDW_USB_D_PLUS		_RG2
#define HDW_USB_D_MINUS		_RG3
#define HDW_AN_BOOST_ON		_LATG6
#ifdef HDW_PRIMELOG_PLUS
#define HDW_SPARE_G7		_RG7	// SPI3_CS_N
#define HDW_SPARE_G8		_RG8	// SPI3_CLK
#define HDW_SPARE_G9		_RG9	// RADIO_EN
#else
#define HDW_MODEM_TX_DATA	_LATG7
#define HDW_MODEM_RX_DATA	_RG8
#define HDW_RS232_TX_DATA	_LATG9
#endif
#define HDW_SNS2_MCLR		_LATG12	// NC on 3-ch HW
#define HDW_SNS1_MCLR		_LATG13
#define HDW_RS485_BOOST_ON	_LATG14
#define HDW_USB_BOOST_ON	_LATG15

#define HDW_MODEM_TX_TRIS	_TRISG7
#define HDW_MODEM_RX_TRIS	_TRISG8

#ifdef HDW_PRIMELOG_PLUS
  #define HDW_TRISG			_16BIT(_B00000000, _B00001100)
#else
  #define HDW_TRISG			_16BIT(_B00000001, _B10001100)
#endif
#define HDW_LATG			_16BIT(_B00000000, _B00000010)
#endif  //HARDWARE_PROFILE_H
