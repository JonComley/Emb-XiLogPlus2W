/******************************************************************************
** File:	Ser.h	
**
** Notes:	Header file for RS485 serial port to Nivus Sensor
**
** v3.04 071211 PB First version for Waste Water
**				   compiler switch on if RS485
*/

#ifndef SER_H_
#define SER_H_

#ifdef HDW_RS485


extern bool	SER_timeout;
extern bool SER_transaction_finished;
extern float SER_last_read_value;
extern uint8 mod_rx_buffer_AT[];


extern void SER_RS485_off(void);
extern void SER_RS485_on(void);
extern bool SER_busy(void);
extern bool SER_read(uint8 func, uint16 address, uint16 words);
extern bool SER_write(uint8 func, uint16 address, uint16 value);
extern void SER_task(void);
extern void SER_init(void);


#endif

#endif	/* SER_H_ */
