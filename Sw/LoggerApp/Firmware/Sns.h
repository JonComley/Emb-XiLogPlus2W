/******************************************************************************
** File:	Sns.h
**
** Notes:	Interface to local Sensor PIC.
** Master indicates to slave that a byte written is first byte of a command by
** taking CS high before WR is taken high. For all other bytes written, keep CS
** low between writes, and just use the WR strobe.
**
** changes
**
** v2.69 270411 PB DEL134 - add SNS_write_ff_width and SNS_write_ff_width_2 flags
*/

#include "HardwareProfile.h"

// Bits for pending commands:
// NB must agree with sns_cmd_table
#define SNS_read_version			SNS_command_flags.b0
#define SNS_read_digital_config		SNS_command_flags.b1
#define SNS_write_digital_config	SNS_command_flags.b2
#define SNS_read_digital_counters	SNS_command_flags.b3
#define SNS_write_ff_table			SNS_command_flags.b4	// clear SNS_ff_table_index when this is set
#define SNS_read_adc				SNS_command_flags.b5
#define SNS_write_ff_width			SNS_command_flags.b6

#if (HDW_NUM_CHANNELS == 9)
#define SNS_read_version_2			SNS_command_flags.b8
#define SNS_read_digital_config_2	SNS_command_flags.b9
#define SNS_write_digital_config_2	SNS_command_flags.b10
#define SNS_read_digital_counters_2	SNS_command_flags.b11
#define SNS_write_ff_table_2		SNS_command_flags.b12	// clear SNS_ff_table_index when this is set
#define SNS_read_adc_2				SNS_command_flags.b13
#define SNS_write_ff_width_2		SNS_command_flags.b14
#endif

#define SNS_command_in_progress		SNS_command_flags.b15

#define SNS_NUM_COMMAND_FLAGS		7						// 7 for sensor PIC 1, 7 for sensor PIC 2
#define SNS_MAX_COMMAND_MASK		_B01111111

// Status bits
#define SNS_pulse_count_a_active	SNS_digital_config.b0
#define SNS_pulse_count_b_active	SNS_digital_config.b1
#define SNS_flash_firing_active		SNS_digital_config.b2

// Sensor PIC ADC channels
#if (HDW_NUM_CHANNELS == 9)

// Sensor PIC 1:
#define SNS_ADC_ADDRESS_VOLTAGE_1	0
#define SNS_ADC_ADDRESS_VOLTAGE_2	1
#define SNS_ADC_ADDRESS_VOLTAGE_3	4
#define SNS_ADC_ADDRESS_VOLTAGE_4	9

// Sensor PIC 2:
#define	SNS_ADC_ADDRESS_CURRENT_1	0
#define SNS_ADC_ADDRESS_CURRENT_2	1
#define SNS_ADC_ADDRESS_CURRENT_3	4

// Common to both:
#define SNS_ADC_ADDRESS_VREF		8

#else	// 3-channel hardware (ch 3 is Vref+):

#define SNS_ADC_ADDRESS_DIFF1		0
#define SNS_ADC_ADDRESS_DIFF2		1
#define SNS_ADC_ADDRESS_CURRENT		4
#define SNS_ADC_ADDRESS_VREF		8
#define SNS_ADC_ADDRESS_VOLTAGE		9

#endif

typedef struct
{
	uint16 channel_a;
	uint16 channel_b;
} SNS_counters_type;

extern uint8 SNS_version;
extern uint8 SNS_ff_table_index;
extern uint8 SNS_adc_address;		// only write this when SNS_read_adc clear
extern bool SNS_pic1_pulse_on_a;
extern bool SNS_pic2_pulse_on_a;

extern uint16 SNS_adc_value;

extern BITFIELD SNS_digital_config;
extern BITFIELD SNS_command_flags;

extern SNS_counters_type SNS_counters;

void SNS_task(void);


