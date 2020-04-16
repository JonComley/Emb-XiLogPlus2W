/******************************************************************************
** File:	Frm.c
**
** Notes:	SPI mode 0 is used, with clock resting at 0 and active rising edge
**
** V2.53 PB added for PrimeLog product - functions for use with serial fram 
**
** V2.65 070411 PB added conditional compilation ifdef HDW_PRIMELOG_PLUS
*/

#include "Custom.h"
#include "Compiler.h"
#include "HardwareProfile.h"
#include "Cal.h"

#define extern
#include "Frm.h"
#undef extern

#ifdef NOT_USED

// fram op-codes
#define FRM_WREN	0x06
#define FRM_WRDI	0x04
#define FRM_RDSR	0x05
#define FRM_WRSR	0x01
#define FRM_READ	0x03
#define FRM_FSTRD	0x0B
#define FRM_WRITE	0x02
#define FRM_SLEEP	0xB9
#define FRM_RDID	0x9F
#define FRM_SNR		0xC3

#define FRM_STATUS_FIXED_BITS	0x71
#define FRM_STATUS_FIXED_CODE	0x40

// local functions

/******************************************************************************
** Function:	fram select
**
** Notes:		select fram chip and wait for safe time
*/
void frm_select(void)
{
	HDW_FRAM_SS_N = 0;
}

/******************************************************************************
** Function:	fram deselect
**
** Notes:		deselect fram chip
*/
void frm_deselect(void)
{
	HDW_FRAM_SS_N = 1;
}

/******************************************************************************
** Function:	fram write bit
**
** Notes:		writes one bit to fram - clocked in on rising edge
*/
void frm_write_bit(bool the_bit)
{
	// set data
	HDW_FRAM_DI = the_bit;
	// clock to 1
	HDW_FRAM_CLK = 1;
	// clock to 0
	HDW_FRAM_CLK = 0;
}

/******************************************************************************
** Function:	fram read bit
**
** Notes:		reads one bit from fram - clocked in on rising edge
*/
bool frm_read_bit(void)
{
	bool result;

	// clock to 1
	HDW_FRAM_CLK = 1;
	// get data
	result = HDW_FRAM_DO;
	// clock to 0
	HDW_FRAM_CLK = 0;
	return result;
}

/******************************************************************************
** Function:	fram write byte
**
** Notes:		writes one byte to fram - MSB FIRST (according to data sheet)
*/
void frm_write_byte(uint8 the_byte)
{
	uint8 mask = 0x80;

	do
	{
		frm_write_bit((the_byte & mask) == mask);
		mask >>= 1;
	}
	while (mask != 0);
}

/******************************************************************************
** Function:	fram read byte
**
** Notes:		reads one byte from fram - MSB FIRST (according to data sheet)
*/
uint8 frm_read_byte(void)
{
	uint8 result = 0x00;
	uint8 count = 0x00;

	do
	{
		result <<= 1;
		if (frm_read_bit())
			result |= 0x01;
		count++;
	}
	while (count < 8);
	return result;
}

/******************************************************************************
** Function:	fram read status register
**
** Notes:		
*/
uint8 frm_read_status(void)
{
	uint8 status;

	frm_select();
	frm_write_byte(FRM_RDSR);
	status = frm_read_byte();
	frm_deselect();
	return status;
}

/******************************************************************************
** Function:	fram sleep
**
** Notes:		
*/
void frm_sleep(void)
{
	frm_select();
	frm_write_byte(FRM_SLEEP);
	frm_deselect();
}

#endif

// global functions

/******************************************************************************
** Function:	write data to fram
**
** Notes:		parameters from pointer, to address, number of bytes
*/
void FRM_write_data(uint8 * source_ptr, uint16 fram_ptr, uint16 bytes)
{
#ifdef HDW_PRIMELOG_PLUS
	// set RF12 to input to read fram data output
	TRISF |= 0x1000;
	frm_select();

	frm_write_byte(FRM_WREN);
	frm_deselect();
	frm_select();
	frm_write_byte(FRM_WRITE);
	frm_write_byte((uint8)(fram_ptr >> 8));
	frm_write_byte((uint8)fram_ptr);
	do
	{
		frm_write_byte(*source_ptr++);
		bytes--;
	}
	while (bytes != 0);

	frm_deselect();
	// set RF12 to output until needed again, as fram data output is tristated off
	TRISF &= 0xefff;
#endif
}

/******************************************************************************
** Function:	read data from fram
**
** Notes:		parameters from address, to pointer, number of bytes
*/
void FRM_read_data(uint16 fram_ptr, uint8 * dest_ptr, uint16 bytes)
{
#ifdef HDW_PRIMELOG_PLUS
	// set RF12 to input to read fram data output
	TRISF |= 0x1000;
	frm_select();
	
	frm_write_byte(FRM_READ);
	frm_write_byte((uint8)(fram_ptr >> 8));
	frm_write_byte((uint8)fram_ptr);
	do
	{
		*dest_ptr++ = frm_read_byte();
		bytes--;
	}
	while (bytes != 0);
	
	frm_deselect();
	// set RF12 to output until needed again, as fram data output is tristated off
	TRISF &= 0xefff;
#endif
}

/******************************************************************************
** Function:	fram initialisation
**
** Notes:		CAL_build_info.modem flag indicates whether modem or fram installed
**				lines are set to modem defaults by main() calling init_hardware()
**				initial line states have also to cope with no fit of fram in PrimeLog
**
**				settings for fram or no fram, with modem flag = 0 (modem settings):
**				RA14	nc			o/p 0	(MODEM_2V8_MON	i/p  )
**				RA15	FRAM_WP_n	o/p 0	(MODEM_CTS_n	i/p  ) (matches external pull down)
**				RB9		FRAM_CK		o/p 0	(nc				o/p 0)
**				RB12	nc			o/p 0	(MODEM_RESET	o/p 0)
**				RC4		nc			o/p 0	(MODEM RING		i/p  )
**				RD8		FRAM_SS_n	o/p 1	(MODEM_RTS_n	o/p 0) (matches external pull up)
**				RE9		nc			o/p 0	(MODEM_DTR_n	o/p 0)
**				RF12	FRAM_DO		i/p 	(MODEM_IGNITION o/p 0) (tristated by FRAM_SS_n high)
**				RF13	FRAM_DI		o/p 0	(MODEM_PWR_ON_n o/p 1)
**				RG7		nc			o/p 0	(MODEM_TX_DATA	i/p  )
**				RG8		nc			o/p 0	(MODEM_RX_DATA	i/p  )
**
**
**				Then need to detect presence of fram
**				and switch it to SLEEP mode with a serial command
**
**				If there is no fram, set RF12 to output at 0 to save power
**
	if (!CAL_build_info.modem)
	{
		// change RA14 and RA15 to outputs logic 0
		LATA &= 0x3fff;
		TRISA &= 0x3fff;
		// no change needed to RB9 and RB12
		// change RC4 to output logic 0
		LATC &= 0xffef;
		TRISC &= 0xffef;
		// change RD8 to output logic 1
		LATD |= 0x0100;
		// no change needed to RE9
		// set RF12 to input, set RF13 to logic 0
		LATF &= 0xcfff;
		TRISF |= 0x1000;
		// change RG7 and RG8 to outputs logic 0
		LATG &= 0xfe7f;
		TRISG &= 0xfe7f;
	}
	else	// we have a modem, so ensure port directions above are undone:
	{
		TRISA |= (HDW_TRISA & ~0x3fff);		// modem 2V8 mon & CTS back to input
		LATA |= (HDW_LATA & ~0x3fff);

		TRISC |= (HDW_TRISC & ~0xffef);		// modem ring restored to input
		LATC |= (HDW_LATC & ~0xffef);

		// TRISD OK
		LATD &= (HDW_LATD | ~0x0100);		// HDW_MODEM_RTS_N = 0

		TRISF &= (HDW_TRISF | ~0x1000);		// modem ignition returned to output	
		LATF |= (HDW_LATF & ~0xcfff);		// sets ignition high

		TRISG |= (HDW_TRISG & ~0xfe7f);
		LATG |= (HDW_LATG & ~0xfe7f);
	}
**
*/
void FRM_init(void)
{
#ifdef HDW_PRIMELOG_PLUS
	// detect presence of fram by attempting read of status
	FRM_fram_present = false;
	// if correct fixed bits received
	if ((frm_read_status() & FRM_STATUS_FIXED_BITS) == FRM_STATUS_FIXED_CODE)
	{
		FRM_fram_present = true;
		// send fram to sleep
		frm_sleep();
	}
	// set RF12 to output until needed again, as fram data output is tristated off
	TRISF &= 0xefff;
#endif
}


