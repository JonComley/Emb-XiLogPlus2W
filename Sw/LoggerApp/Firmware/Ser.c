/******************************************************************************
** File:	Ser.c	
**
** Notes:	Serial Port to NIVUS Sensor functions
**
** v3.04 071211 PB First version for Waste Water
**				   compiler switch on if RS485
**
** V4.02 080414 PB rewrite #NVB to send 38400 baud rate command to NIVUS at 9600 baud - used when NIVUS is set to wrong baudrate
**		 140414	   ensure clock is switched to PLL when UART is on
**
** V6.00 300316 PQ MODBUS version
*/

#include <string.h>
#include <stdio.h>
#include <float.h>
//#include <stdint.h>

#include "custom.h"
#include "Compiler.h"
#include "str.h"
#include "HardwareProfile.h"
#include "MDD File System/FSIO.h"
#include "Tim.h"
#include "Rtc.h"
#include "Slp.h"
#include "Log.h"
#include "Ftp.h"
#include "Alm.h"
#include "Pdu.h"
#include "Usb.h"

#define extern
#include "ser.h"
#undef extern

#ifdef HDW_RS485

/* MODBUS definitions */

#define	bswap16(x)		((x >> 8) | (x << 8))


typedef struct
{
	uint8_t		slave_address;
	uint8_t		function;
	uint16_t	start_address;
	uint16_t	words;
	uint16_t	crc;
} MOD_QUERY_t;

typedef struct
{
	uint8_t		slave_address;
	uint8_t		function;
	uint16_t	address;
	uint16_t	value;
	uint16_t	crc;
} MOD_PRESET_t;


#define	SER_STATE_IDLE						0
#define	SER_STATE_WAIT_TX_END				1
#define	SER_STATE_WAIT_RESPONSE_START		2
#define	SER_STATE_WAIT_RESPONSE_END			3

#define	SER_BAUD_RATE		9600
#define BRG_DIV1			4
#define BRGH1				1

#define	RX_TIMEOUT_MS		2000				// time to wait for a response from device
#define RX_BUFFER_SIZE		160
#define	TX_BUFFER_SIZE		16

bool	mod_byte_recieved_AT = false;			// set to true when RX in interrupt triggered
uint8	mod_rx_buffer_AT[RX_BUFFER_SIZE] __attribute__((aligned(4)));;		// filled by RX interrupt
uint8_t	mod_rx_index_AT = 0;					// index of next free byte in RX buffer
uint8_t	mod_tx_buffer_AT[TX_BUFFER_SIZE] __attribute__((aligned(4)));;		// read by TX interrupt
uint8_t	mod_tx_index_AT = 0;					// index of next byte to read from TX buffer
uint8_t mod_tx_bytes_AT = 0;					// number of bytes to send

bool	mod_pending_tx = false;					// signals the task to start transmitting when possible
uint8_t	state = SER_STATE_IDLE;

uint8_t	MOD_slave_address = 1;

bool	SER_timeout = false;
bool	SER_transaction_finished = false;
float	SER_last_read_value = 0;


/******************************************************************************
** Debug buffer dump
*/
void ser_debug_dump_buffer(void *buffer, uint8 bytes)
{
	uint8 *p = (uint8 *)buffer;
	char temp[4] = {0};
	while(bytes--)
	{
		sprintf(temp, "%02X ", *p++);
		USB_monitor_prompt(temp);
	}
	USB_monitor_prompt("\r\n");
}

/******************************************************************************
** Function:	ser_RS485_off
**
** Notes:		
*/
void SER_RS485_off(void)
{
	HDW_RS485_ON_N = 1;																	// RS485 off
	HDW_RS485_RE_N = 0;
	HDW_RS485_DE = 0;
	U1MODEbits.UARTEN = false;
	_U1TXIE = false;
	_U1RXIE = false;																	// disable receive interrupt
	SLP_set_required_clock_speed();														// switch clock back to default
}

/******************************************************************************
** Function:	ser_RS485_on
**
** Notes:		
*/
void SER_RS485_on(void)
{
	mod_byte_recieved_AT = false;
	mod_rx_buffer_AT[0] = '\0';
	mod_rx_index_AT = 0;
	mod_tx_buffer_AT[0] = '\0';
	mod_tx_index_AT = 0;
	mod_tx_bytes_AT = 0;

	// Configure UART
	HDW_RS485_ON_N = 0;										// switch 485 interface on to transmit
	SLP_set_required_clock_speed();							// this will switch clock to high speed when USB unconnected

	U1BRG = (((GetSystemClock() / 2) + (BRG_DIV1 / 2 * SER_BAUD_RATE)) / BRG_DIV1 / SER_BAUD_RATE - 1);
    U1MODE = 0;
    U1MODEbits.BRGH = BRGH1;
	U1MODEbits.UEN = 0;										// no flow control
    U1STA = 0;
	U1MODEbits.PDSEL = 1;	// 8 bit, even parity
    U1MODEbits.UARTEN = true;
    U1STAbits.UTXEN = true;
    _U1RXIF = false;

	U1STAbits.URXISEL = 0;	// interrupt when RX buffer receives any character
	_U1TXIE = true;
	_U1RXIE = true;

	HDW_RS485_RE_N = 1;		// disable receiver
	HDW_RS485_DE = 1;		// enable transmitter
	//HDW_RS485_BOOST_ON = 0;
}

/* interrupt function */

/******************************************************************************
** Serial RX interrupt handler
*/
void __attribute__((__interrupt__, no_auto_psv)) _U1RXInterrupt(void)
{
    mod_byte_recieved_AT = true;
	
	while (U1STAbits.URXDA)	// keep going until FIFO empty
	{
		uint8 temp = U1RXREG;
		if (mod_rx_index_AT < RX_BUFFER_SIZE)
			mod_rx_buffer_AT[mod_rx_index_AT++] = temp;
	}
    _U1RXIF = false;
}

/******************************************************************************
** Serial TX interrupt handler
 * Sends out bytes. When complete, tx_bytes_AT will be zero.
*/
void __attribute__((__interrupt__, no_auto_psv)) _U1TXInterrupt(void)
{
	_U1TXIF = false;

	//while (U1STAbits.UTXBF == 0)	// fill TX FIFO
	//{
		if (mod_tx_bytes_AT > 0)
		{
			mod_tx_bytes_AT--;
			U1TXREG = mod_tx_buffer_AT[mod_tx_index_AT++];
		}
		//else	// end of transmission
		//	break;
	//}
}

/******************************************************************************
** Calculate MODBUS CRC
*/
uint16_t ser_calc_crc(const void *buffer, uint8_t bytes)
{
	uint8_t *b = (uint8_t *)buffer;

	uint8	i;
	uint16	crc = 0xFFFF;
	while(bytes--)
	{
		crc ^= *b++;
		for (i = 8; i !=0; i--)
		{
			if (crc & 1)
				crc = (crc >> 1) ^ 0xA001;
			else
				crc >>= 1;
		}
	}

	return crc;
}

/******************************************************************************
** Check response data looks valid. Size includes the CRC.
*/
bool ser_check_response(uint8_t func, uint16_t size)
{
	uint16	crc = 0;
	if (mod_rx_buffer_AT[1] != func)
		return false;
	memcpy(&crc, &mod_rx_buffer_AT[size-2], 2);
	if (ser_calc_crc(mod_rx_buffer_AT, size-2) != crc)
	{
	/*
		char temp[12];
		//sprintf(temp, "%04X", ser_calc_crc(mod_rx_buffer_AT, size-2));
		sprintf(temp, "%04X %04X", crc, ser_calc_crc(mod_rx_buffer_AT, size-2));
		USB_monitor_string(temp);
	*/
		USB_monitor_string("Bad CRC");
		return false;
	}
	return true;
}

/******************************************************************************
** Check if transmission has finished
*/
bool ser_tx_has_finished(void)
{
	return U1STAbits.TRMT;
}

/******************************************************************************
** Function:	SER_busy
**
** Notes:		return true if so
*/
bool SER_busy(void)
{
	return (state != SER_STATE_IDLE);
}

/******************************************************************************
** Transmit contents of tx_buffer_AT
*/
void ser_tx(uint8_t bytes)
{
	HDW_RS485_RE_N = 1;			// disable receiver
	HDW_RS485_DE = 1;			// enable transmitter

	__builtin_disi(0x3FFF);		// interrupts off
	mod_tx_bytes_AT = bytes;
	mod_tx_index_AT = 0;
	__builtin_disi(0);			// interrupts on
	mod_pending_tx = true;
}

/******************************************************************************
** Read coils/inputs/registers. Each register is a 16 bit word.
*/
bool SER_read(uint8 func, uint16 address, uint16 words)
{
	MOD_QUERY_t *q = (MOD_QUERY_t *)mod_tx_buffer_AT;

	if (!ser_tx_has_finished())
		return false;

	mod_byte_recieved_AT = false;
	mod_rx_buffer_AT[0] = '\0';
	mod_rx_index_AT = 0;

	q->slave_address = MOD_slave_address;
	q->function = func;
	q->start_address = bswap16(address);
	q->words = bswap16(words);
	q->crc = ser_calc_crc(q, sizeof(MOD_QUERY_t) - 2);

	//ser_debug_dump_buffer(q, sizeof(MOD_QUERY_t));
	ser_tx(sizeof(MOD_QUERY_t));

	// make sure RX FIFO is empty
	while (U1STAbits.URXDA)
		U1RXREG;

	SER_transaction_finished = false;
	SER_timeout = false;

	return true;
}

/******************************************************************************
** Write a 16 bit register
*/
bool SER_write(uint8 func, uint16 address, uint16 value)
{
	MOD_PRESET_t *q = (MOD_PRESET_t *)mod_tx_buffer_AT;

	if (!ser_tx_has_finished())
		return false;

	mod_byte_recieved_AT = false;
	mod_rx_buffer_AT[0] = '\0';
	mod_rx_index_AT = 0;
	//mod_tx_buffer_AT[0] = '\0';
	//mod_tx_index_AT = 0;
	//mod_tx_bytes_AT = 0;

	q->slave_address = MOD_slave_address;
	q->function = func;
	q->address = bswap16(address);
	q->value = bswap16(value);
	q->crc = ser_calc_crc(q, sizeof(MOD_PRESET_t) - 2);

	//ser_debug_dump_buffer(q, sizeof(MOD_PRESET_t));
	ser_tx(sizeof(MOD_PRESET_t));

	SER_transaction_finished = false;
	SER_timeout = false;

	return true;
}

/******************************************************************************
** Function:	SER_task
**
** Notes:		called all the time processor is awake
**				processor may have been awakened by timed wakeup relevant to SER
*/
void SER_task(void)
{
	static uint16_t	timer_ms = 0;
	static uint16_t	last_byte_time_ms = 0;

	if (TIM_20ms_tick)																		// run overall timeout to fall back to idle																		
		timer_ms += 20;


	// start new TX if requested
	if (mod_pending_tx && ser_tx_has_finished())	// pending and last TX finished
	{
		//USB_monitor_string("Starting TX...");
		mod_pending_tx = false;
		__builtin_disi(0x3FFF);		// interrupts off
		if (mod_tx_bytes_AT > 0)
		{
			mod_tx_bytes_AT--;
			U1TXREG = mod_tx_buffer_AT[mod_tx_index_AT++];	// start transmission
		}
		__builtin_disi(0);			// interrupts on
		state = SER_STATE_WAIT_TX_END;
		SER_timeout = false;
		timer_ms = 0;
	}



	switch(state)																			// state machine
	{
	case SER_STATE_IDLE:
		break;																				// can sleep in this state

	case SER_STATE_WAIT_TX_END:
		if (ser_tx_has_finished())
		{
			HDW_RS485_RE_N = 0;		// enable receiver
			HDW_RS485_DE = 0;		// disable transmitter

			//USB_monitor_string("->SER_STATE_WAIT_RESPONSE_START");
			state = SER_STATE_WAIT_RESPONSE_START;
			timer_ms = 0;
		}
		break;

	case SER_STATE_WAIT_RESPONSE_START:
		if (!mod_byte_recieved_AT)			// check for start of RX
		{
			if (timer_ms > RX_TIMEOUT_MS)
			{
				USB_monitor_string("Timeout 1");
				SER_timeout = true;
				state = SER_STATE_IDLE;
			}
		}
		else
		{
			mod_byte_recieved_AT = false;
			state = SER_STATE_WAIT_RESPONSE_END;
			last_byte_time_ms = timer_ms;
		}
		break;

	case SER_STATE_WAIT_RESPONSE_END:
		if (mod_byte_recieved_AT)			// check for end of RX (20ms gap))
		{
			mod_byte_recieved_AT = false;
			last_byte_time_ms = timer_ms;
		}

		if (timer_ms > RX_TIMEOUT_MS)
		{
			USB_monitor_string("Timeout 2");
			SER_timeout = true;
			state = SER_STATE_IDLE;
		}

		if (timer_ms - last_byte_time_ms > 40)
		{
			// handle response
			uint8 size = mod_rx_index_AT;
			state = SER_STATE_IDLE;

			//ser_debug_dump_buffer(mod_rx_buffer_AT, size);

			if ((size >= 9) && ser_check_response(mod_rx_buffer_AT[1], size))
			{
				//char temp[16];
				uint8 *p = (uint8 *)&SER_last_read_value;
				p[3] = mod_rx_buffer_AT[3];		// byte order needs reversing
				p[2] = mod_rx_buffer_AT[4];
				p[1] = mod_rx_buffer_AT[5];
				p[0] = mod_rx_buffer_AT[6];
				//sprintf(temp, "%6.2f", SER_last_read_value);
				//USB_monitor_string(temp);

				SER_transaction_finished = true;
			}
			else
				SER_timeout = true;
		}
		break;
	}
}

/******************************************************************************
** Function:	SER_init
**
** Notes:		initialize RS485 port and set defaults
*/
void SER_init(void)
{
	SER_RS485_off();
}

#endif
