/******************************************************************************
** File:	Scf.c - script file processor	
**
** Notes:	
**
** V3.26 170613 PB	replace SCF_execute() with SCF_config_execute() and SCF_scripts_execute() for scripts in different directories
*/

extern char SCF_filename[CFS_FILE_NAME_SIZE];
extern char SCF_path[CFS_PATH_NAME_SIZE];
extern FAR char SCF_output_filename[CFS_FILE_NAME_SIZE];

bool SCF_execute(char *path, char *filename, bool output);
bool SCF_execute_next_line(void);
uint8 SCF_progress(void);


