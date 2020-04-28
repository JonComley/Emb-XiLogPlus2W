/******************************************************************************
** File:	Scf.c - script file processor	
**
** Notes:
**
** V3.26 170613 PB	replace SCF_execute() with SCF_config_execute() and SCF_scripts_execute() for scripts in different directories
**
** V3.29 161013 PB  correction to output string in SCF_execute_next_line() to "....%s\\%s"
**					use strcpy instead of memcpy for path and filename - ensures null termination
*/

#include <string.h>
#include <stdio.h>
#include "Custom.h"
#include "Compiler.h"
#include "MDD File System/SD-SPI.h"
#include "MDD File System/FSIO.h"

#include "Cfs.h"
#include "Str.h"
#include "Msg.h"
#include "rtc.h"
#include "Com.h"
#include "Pdu.h"
#include "Ana.h"
#include "Dig.h"
#include "Cmd.h"
#include "Usb.h"

#define extern
#include "Scf.h"
#undef extern

int scf_size;
int scf_processed;
int scf_line_number;

FAR char scf_line_buffer[CMD_MAX_LENGTH];

/******************************************************************************
** Function:	Set a config script file to be processed
**
** Notes:		Script file should exist in \CONFIG folder. Returns false if it doesn't.
**				NB File system must be open.
*/
bool SCF_execute(char *path, char *filename, bool output)
{
	if (!CFS_file_exists(path, filename))
		return false;

	// Assume SCF_output_filename hs already been set up if output required
	if (!output)
		SCF_output_filename[0] = '\0';

	strcpy(SCF_path, path);
	strcpy(SCF_filename, filename);
	scf_size = CFS_search_filesize();
	scf_processed = 0;
	scf_line_number = 1;

	return SCF_execute_next_line();
}

/******************************************************************************
** Function:	Process script file one line at a time
**
** Notes:		Returns false only when finished. Ignores comment lines (no # at start).
*/
bool SCF_execute_next_line(void)
{
	int i;

	if (scf_line_number == 1)
	{
		sprintf(STR_buffer, "Executing script file %s\\%s", SCF_path, SCF_filename);
		USB_monitor_string(STR_buffer);
	}

	// read next line until find command or end of file
	do
	{
		i = CFS_read_line(SCF_path, SCF_filename, scf_line_number++,
						  scf_line_buffer, sizeof(scf_line_buffer));

		if (i < 0)
		{
			// end of file
			scf_processed = scf_size;
			SCF_filename[0] = '\0';				// execution complete
			USB_monitor_string("Script execution terminated.");
			return false;
		}
	} while (scf_line_buffer[0] != CMD_CHARACTER);

	// got a command
	scf_processed += i;
	USB_monitor_string(scf_line_buffer);
	CMD_schedule_parse(CMD_SOURCE_SCRIPT, scf_line_buffer, COM_output_buffer);
	return true;
}

/******************************************************************************
** Function:	Return progress of script file
**
** Notes:		
*/
uint8 SCF_progress(void)
{
	if (scf_size == 0) return 100;
	if (scf_processed >= scf_size) return 100;
	return (uint8)(((unsigned long)scf_processed * 100)/(unsigned long)scf_size);
}

