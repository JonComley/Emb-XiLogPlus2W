/******************************************************************************
** File:	Cal.h
**
** Notes:	Calibration	& build info
**
** V3.04 050112 PB WasteWater - add number of control outputs to build info
*/

// Logger type & bar rating handled in STR_buffer
typedef struct
{
	bool modem;
	char digital_wiring_option;
	char serial_number[16];
	int num_digital_channels;
	int num_control_outputs;
	char analogue_channel_types[8];		// up to 7 + '\0'
} CAL_build_info_type;

extern CAL_build_info_type CAL_build_info;

void CAL_read_build_info(void);
char * CAL_read_analogue_coefficients(int index);


