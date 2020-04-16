/******************************************************************************
** File:	Sns.c	
**
** Notes:	Interface to local Sensor PIC.
** Master indicates to slave that a byte written is first byte of a command by
** taking CS high before WR is taken high. For all other bytes written, keep CS
** low between writes, and just use the WR strobe.
**
** changes
**
** V2.69 270411 PB DEL134 - add new entry to sns_cmd_table[] for writing flash fire pulse width
**                          add SNS_CMD_WRITE_PULSE_WIDTH case to switch on command type in SNS_task SNS_IDLE state 
**                          to set second byte to DIG_config[x].ff_pulse_width_x32us
**
** V3.18 090113 PB log sensor pick command retry failure in activity log
**				   for monitoring a problem seen when setting channel B to reverse counting
**				   it COULD be that command does not get through to sensor pic
*/

#include "Custom.h"
#include "compiler.h"
#include "HardwareProfile.h"
#include "tim.h"
#include "Rtc.h"
#include "Dig.h"
#include "Ana.h"
#include "log.h"

#define extern
#include "sns.h"
#undef extern

// Commands: (upper nibble = command, lower = parameter)
#define SNS_CMD_READ_VERSION			0x00
#define SNS_CMD_READ_DIG_CONFIG			0x10
#define SNS_CMD_WRITE_DIG_CONFIG		0x20	// LSN = data
#define SNS_CMD_READ_DIG_COUNTERS		0x30
#define SNS_CMD_READ_MIN_FREQ			0x40	// LSN = index
#define SNS_CMD_WRITE_MIN_FREQ			0x50	// LSN = index, + 2-byte value (LSB first)
#define SNS_CMD_READ_MAX_FREQ			0x60	// LSN = index
#define SNS_CMD_WRITE_MAX_FREQ			0x70	// LSN = index, + 2-byte value (LSB first)
#define SNS_CMD_READ_OUTPUT_PERIOD		0x80	// LSN = index
#define SNS_CMD_WRITE_OUTPUT_PERIOD		0x90	// LSN = index, + 1-byte value
#define SNS_CMD_READ_PULSE_WIDTH		0xA0
#define SNS_CMD_WRITE_PULSE_WIDTH		0xB0	// + 1-byte value
#define SNS_CMD_READ_ADC_CHANNEL		0xC0	// LSN = channel
#define SNS_CMD_READ_RAM				0xD0	// + 2-byte address (LSB first)
#define SNS_CMD_WRITE_RAM				0xE0	// + 2-byte address (LSB first), 1-byte data

// Sensor PIC interface states:
#define SNS_IDLE	0
#define SNS_TX		1
#define SNS_RX		2

// command setup table:
typedef struct
{
	uint8 command;
	uint8 tx_count;
	uint8 rx_count;
	void *p_result;
} sns_cmd_setup_type;

bool sns_pic2;		// set = talk to sensor PIC 2

int sns_index;
int sns_state;
int sns_timer_x20ms;
int sns_cmd_index;
int sns_retry_count;

uint8 sns_tx_buffer[4]; 
uint8 sns_rx_buffer[4];

// Indices of command setup table must agree with bit positions in sns.h:
const sns_cmd_setup_type sns_cmd_table[SNS_NUM_COMMAND_FLAGS] =
{
	{ SNS_CMD_READ_VERSION,			1,	1, &SNS_version			},
	{ SNS_CMD_READ_DIG_CONFIG,		1,	1, &SNS_digital_config	},
	{ SNS_CMD_WRITE_DIG_CONFIG,		1,	0, NULL					},	// NB add value to command byte
	{ SNS_CMD_READ_DIG_COUNTERS,	1,	4, &SNS_counters		},
	{ SNS_CMD_WRITE_OUTPUT_PERIOD,	2,	0, NULL					},
	{ SNS_CMD_READ_ADC_CHANNEL,		1,	2, &SNS_adc_value		},	// add channel no. to command byte
	{ SNS_CMD_WRITE_PULSE_WIDTH,	2,	0, NULL					}
};

/******************************************************************************
** Function:	Finished command
**
** Notes:		Call after command ends successfully, or all retries have failed
*/
void sns_command_finished(void)
{
#if (HDW_NUM_CHANNELS == 9)
	HDW_SNS2_CS_N = true;		// deselect chip
#endif

	HDW_SNS1_CS_N = true;		// deselect chip

	sns_state = SNS_IDLE;

	sns_timer_x20ms = 0;	// prevent subsequent timeout
	sns_retry_count = 0;	// clear counter for next command

	if (sns_cmd_table[sns_cmd_index].p_result != NULL)
	{
		memcpy(sns_cmd_table[sns_cmd_index].p_result, sns_rx_buffer, sns_cmd_table[sns_cmd_index].rx_count);

		// correct for sensor PIC missing the first pulse on A after being reconfigured
		if (sns_cmd_table[sns_cmd_index].command == SNS_CMD_READ_DIG_COUNTERS)
		{
			if (sns_pic2)
			{
				if (_INT3IF || SNS_pic2_pulse_on_a)		// sensor PIC 2, interrupt line has seen a pulse
				{
					SNS_pic2_pulse_on_a = true;
					SNS_counters.channel_a++;
				}
			}
			else if (_INT1IF || SNS_pic1_pulse_on_a)	// sensor PIC 1, interrupt line has seen a pulse
			{
				SNS_pic1_pulse_on_a = true;
				SNS_counters.channel_a++;
			}
		}
	}
	else if (sns_cmd_table[sns_cmd_index].command == SNS_CMD_WRITE_OUTPUT_PERIOD)
	{
#ifndef HDW_RS485
		// suppress clearing the command bit if more of ff table to do:
		if (++SNS_ff_table_index < sizeof(DIG_config[0].ff_period))
		{
#if (HDW_NUM_CHANNELS == 9)
			if (sns_pic2)
				SNS_write_ff_table_2 = true;
			else
#endif
				SNS_write_ff_table = true;

			return;
		}
		// else reset table pointer to beginning of table:
#endif
		SNS_ff_table_index = 0;
	}

	SNS_command_in_progress = false;
}

/******************************************************************************
** Function:	Sensor PIC comms task
**
** Notes:	
*/
void SNS_task(void)
{
	uint16 i;

	if (TIM_20ms_tick && (sns_timer_x20ms != 0))
	{
		if (--sns_timer_x20ms == 0)			// timeout
		{
			if (++sns_retry_count >= 3)		// last retry
			{
				memset(sns_rx_buffer, 0x00, sizeof(sns_rx_buffer));
				LOG_enqueue_value(LOG_ACTIVITY_INDEX, LOG_SNS_FILE, __LINE__);	// assert
				sns_command_finished();
			}
			else							// retry same command
				sns_state = SNS_IDLE;
		}
	}

	if (SNS_command_flags.mask == 0)		// nothing to do
		return;

	switch (sns_state)
	{
	case SNS_IDLE:							// start new command
		// Set sns_cmd_index & sns_pic2:
		i = 0x0001;
#if (HDW_NUM_CHANNELS == 9)
		for (sns_cmd_index = 0; sns_cmd_index < 2 * SNS_NUM_COMMAND_FLAGS; sns_cmd_index++)
		{
			// Do bits 0..5 then 8..13
			if (i == (SNS_MAX_COMMAND_MASK + 1))
				i = 0x0100;

			if ((SNS_command_flags.mask & i) != 0)
			{
				sns_cmd_index %= SNS_NUM_COMMAND_FLAGS;
				sns_pic2 = ((i & 0xFF00) != 0);
				break;
			}

			i <<= 1;
			if (i > (SNS_MAX_COMMAND_MASK << 8))
			{
				LOG_enqueue_value(LOG_ACTIVITY_INDEX, LOG_SNS_FILE, __LINE__);	// assert
				SNS_command_flags.mask = 0;
				return;
			}
		}
#else
		sns_pic2 = false;
		for (sns_cmd_index = 0; sns_cmd_index < SNS_NUM_COMMAND_FLAGS; sns_cmd_index++)
		{
			if ((SNS_command_flags.mask & i) != 0)
				break;

			i <<= 1;
			if (i > (SNS_MAX_COMMAND_MASK << 8))
			{
				LOG_enqueue_value(LOG_ACTIVITY_INDEX, LOG_SNS_FILE, __LINE__);	// assert
				SNS_command_flags.mask = 0;
				return;
			}
		}
#endif

		SNS_command_flags.mask &= ~i;		// clear command flag
		SNS_command_in_progress = true;
		sns_tx_buffer[0] = sns_cmd_table[sns_cmd_index].command;
		switch (sns_tx_buffer[0])
		{
#ifndef HDW_RS485
		case SNS_CMD_WRITE_DIG_CONFIG:
			sns_tx_buffer[0] |= LOWBYTE(SNS_digital_config.mask);
			break;

		case SNS_CMD_WRITE_OUTPUT_PERIOD:
			sns_tx_buffer[0] |= SNS_ff_table_index & 0x0F;
			sns_tx_buffer[1] = sns_pic2 ?
				DIG_config[1].ff_period[SNS_ff_table_index] : DIG_config[0].ff_period[SNS_ff_table_index];
			break;

		case SNS_CMD_WRITE_PULSE_WIDTH:
			sns_tx_buffer[1] = sns_pic2 ?
				DIG_config[1].ff_pulse_width_x32us : DIG_config[0].ff_pulse_width_x32us;
			break;
#endif
		case SNS_CMD_READ_ADC_CHANNEL:
			sns_tx_buffer[0] |= SNS_adc_address & 0x0F;
			break;
		}

		// Set up interrupt to detect falling edge on busy line:
		// Sensor PIC 1 busy: INT0 on RD0
		_INT0EP = true;
		_INT0IF = false;

#if (HDW_NUM_CHANNELS == 9)
		// Sensor PIC 2 busy: IC2 interrupt on RB8 / RP8
		IC2CON1 = 0x0000;
		IC2CON1 = _16BIT(_B00111100, _B00000010);	// falling edge interrupt on IC2
		_IC2IF = false;
#endif

		sns_index = 0;							// start tx
		sns_state = SNS_TX;
		sns_timer_x20ms = 10;					// 200ms timeout
		break;

	case SNS_TX:
#if (HDW_NUM_CHANNELS == 9)
		if (!sns_pic2)
#endif
		{
			if (HDW_SNS1_BUSY)
				break;
			// else:

			if (_INT0IF || (sns_index == 0))		// first byte, or previous write acknowledged
			{
				HDW_SNS_DATA_DIRECTION	= HDW_DIRECTION_OUTPUT;
				HDW_SNS1_CS_N = false;
				HDW_SNS_DATA_OUT = sns_tx_buffer[sns_index];
				HDW_SNS_WR_N = false;				// strobe data out
				_INT0IF = false;					// look for acknowledge
				if (++sns_index == 1)				// writing command byte
					HDW_SNS1_CS_N = true;			// terminate command byte strobe with CS
				// else leave CS low

				HDW_SNS_WR_N = true;				// finish write
				HDW_SNS_DATA_OUT = _B00000000;		// idle the lines low
			}
		}
#if (HDW_NUM_CHANNELS == 9)
		else	// sns_pic2
		{
			if (HDW_SNS2_BUSY)
				break;
			// else:

			if (_IC2IF || (sns_index == 0))		// first byte, or previous write acknowledged
			{
				HDW_SNS_DATA_DIRECTION	= HDW_DIRECTION_OUTPUT;
				HDW_SNS2_CS_N = false;
				HDW_SNS_DATA_OUT = sns_tx_buffer[sns_index];
				HDW_SNS_WR_N = false;				// strobe data out
				_IC2IF = false;						// look for acknowledge
				if (++sns_index == 1)				// writing command byte
					HDW_SNS2_CS_N = true;			// terminate command byte strobe with CS
				// else leave CS low

				HDW_SNS_WR_N = true;				// finish write
				HDW_SNS_DATA_OUT = _B00000000;		// idle the lines low
			}
		}
#endif

		if (sns_index >= sns_cmd_table[sns_cmd_index].tx_count)		// finished Tx - go into Rx mode
		{
			sns_index = 0;
			sns_state = SNS_RX;
			sns_timer_x20ms = 10;				// 200ms timeout
		}
		break;

	case SNS_RX:
#if (HDW_NUM_CHANNELS == 9)
		if (!sns_pic2)
#endif
		{
			if (HDW_SNS1_BUSY)
				break;
			// else:

			if (_INT0IF)							// receive: previous read or write acknowledged
			{
				if (sns_index >= sns_cmd_table[sns_cmd_index].rx_count)		// finished successfully
				{
					sns_command_finished();
					break;
				}
				// else read next byte:

				HDW_SNS_DATA_DIRECTION	= HDW_DIRECTION_INPUT;
				HDW_SNS1_CS_N = false;
				HDW_SNS_RD_N = false;
				_INT0IF = false;
				Nop();
				sns_rx_buffer[sns_index++] = HDW_SNS_DATA_IN;
				HDW_SNS_RD_N = true;
				HDW_SNS1_CS_N = true;				// deselect chip
				HDW_SNS_DATA_OUT = _B00000000;		// idle the lines low
				HDW_SNS_DATA_DIRECTION	= HDW_DIRECTION_OUTPUT;
			}
		}
#if (HDW_NUM_CHANNELS == 9)
		else	// sns_pic2
		{
			if (HDW_SNS2_BUSY)
				break;
			// else:

			if (_IC2IF)								// receive: previous read or write acknowledged
			{
				if (sns_index >= sns_cmd_table[sns_cmd_index].rx_count)		// finished successfully
				{
					sns_command_finished();
					break;
				}
				// else read next byte:

				HDW_SNS_DATA_DIRECTION	= HDW_DIRECTION_INPUT;
				HDW_SNS2_CS_N = false;
				HDW_SNS_RD_N = false;
				_IC2IF = false;
				Nop();
				sns_rx_buffer[sns_index++] = HDW_SNS_DATA_IN;
				HDW_SNS_RD_N = true;
				HDW_SNS2_CS_N = true;				// deselect chip
				HDW_SNS_DATA_OUT = _B00000000;		// idle the lines low
				HDW_SNS_DATA_DIRECTION	= HDW_DIRECTION_OUTPUT;
			}
		}
#endif
		break;

	default:
		LOG_enqueue_value(LOG_ACTIVITY_INDEX, LOG_SNS_FILE, __LINE__);	// assert
		SNS_command_flags.mask = 0;
		sns_state = SNS_IDLE;
		break;
	}
}

