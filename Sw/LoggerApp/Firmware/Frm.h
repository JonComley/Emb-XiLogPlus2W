/******************************************************************************
** File:	Frm.h
**
** Notes:
*

#include "HardwareProfile.h"	// for references to fram hardware lines

extern bool FRM_fram_present;

void FRM_write_data(uint8 * source_ptr, uint16 fram_ptr, uint16 bytes);
void FRM_read_data(uint16 fram_ptr, uint8 * dest_ptr, uint16 bytes);
void FRM_init(void);

*/
