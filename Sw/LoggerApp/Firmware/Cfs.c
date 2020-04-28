/******************************************************************************
** File:	Cfs.c	
**
** Notes:	Custom File System
*/

/* CHANGES
** v2.36 271010 PB  add CFS_find_youngest_file() fn
**
** v3.05 260712 PB  in CFS_write_file do not open file if empty string, just return true
**
** V3.26 180613 PB  Remove CFS_FAILED_TO_OPEN state 
**
** V3.29 171013 PB  bring up to date with Xilog+ V3.05
**					use new log state LOG_BATT_DEAD instead of LOG_VOLTAGE_TOO_LOW
**
** V4.02 140414 PB  new state CFS_FAILED with a timeout of 2sec, then power CFS down
** V4.06 130515 MA	File system modifications for faster creation of new files and avoidance of file system corruption
*/

#include <string.h>
#include "Custom.h"
#include "Compiler.h"
#include "Tim.h"
#include "Str.h"
#include "Msg.h"
#include "rtc.h"
#include "Com.h"
#include "Pdu.h"
#include "Ana.h"
#include "Dig.h"
#include "Cmd.h"
#include "Slp.h"
#include "Log.h"

#include "MDD File System/SD-SPI.h"
#include "MDD File System/FSIO.h"
#include "usb_config.h"
#include "Usb/usb_device.h"

#define extern
#include "Cfs.h"
#undef extern

// Low-level function in FSIO.c:
int FS_read_line(FSFILE *stream, int line_number, char *ptr, size_t max_bytes);

// Low-level function in SD-SPI.c:
BYTE MDD_SDSPI_ReadMedia(void);

#define CFS_POWERING_TIMEOUT_X20MS	2
#define CFS_INIT_TIMEOUT_X20MS		50
#define CFS_OPENING_TIMEOUT_X20MS	100		// 2s
#define CFS_STAY_ON_TIMEOUT_X20MS	20		// 400ms
#define CFS_FAILED_TIMEOUT_X20MS	100		// 2s

char cfs_filename[CFS_FILE_NAME_SIZE];

SearchRec cfs_search_result;

/******************************************************************************
** Prototypes for functions in SD-SPI.c:
*/
void OpenSPIM(unsigned int sync_mode);
unsigned char WriteSPIM(unsigned char data_out);
MMC_RESPONSE SendMMCCmd(BYTE cmd, DWORD address);

/******************************************************************************
** Function:	Initialise SD card
**
** Notes:		Returns true if it replies to GO_IDLE_STATE command
*/
bool cfs_init_sd_card(void)
{
	uint16 i;
    MMC_RESPONSE response; 
    WORD spiconvalue = 0x0003;
        
    mediaInformation.errorCode = MEDIA_NO_ERROR;
    mediaInformation.validityFlags.value = 0;
    MDD_SDSPI_finalLBA = 0x00000000;	//Will compute a valid value later, from the CSD register values we get from the card

    HDW_SPI1_SS_N = 1;					//Initialize Chip Select line
    
    //Media powers up in the open-drain mode and cannot handle a clock faster
    //than 400kHz. Initialize SPI port to slower than 400kHz
    // Calculate the prescaler needed for the clock. Set up for clock speed 32000000
    i = (uint16)(SLP_get_system_clock() / 400000L);

    while (i != 0)
    {
        if (i > 8)
        {
            spiconvalue--;
            // round up
            if ((i % 4) != 0)
                i += 4;
            i /= 4;
        }
        else
            i = 0;
    }
    
    OpenSPIM(MASTER_ENABLE_ON | spiconvalue | ((~(i << 2)) & 0x1C));

    // let the card power on and initialize
    TIM_delay_ms(1);
    
	//Media requires 80 clock cycles to startup [8 clocks/BYTE * 10 us]
    for(i = 0; i < 10; i++)
        mSend8ClkCycles();

	HDW_SPI1_SS_N = 0;   
    TIM_delay_ms(1);
    
    // Send CMD0 to reset the media
    response = SendMMCCmd(GO_IDLE_STATE, 0x0);
    
    if((response.r1._byte == MMC_BAD_RESPONSE) || ((response.r1._byte & 0xF7) != 0x01))
    {
        HDW_SPI1_SS_N = 1;                               // deselect the devices
        mediaInformation.errorCode = MEDIA_CANNOT_INITIALIZE;
        return false;
    }

	gSDMode = SD_MODE_NORMAL;							// by default...
    response = SendMMCCmd(SEND_IF_COND, 0x1AA);
	// If the following is true, when we enter CFS_sd_card_ready, assume it's an HC.
	// Commands we execute then may reveal that it is in fact non-HC.
    if (((response.r7.bytewise._returnVal & 0xFFF) == 0x1AA) && (!response.r7.bitwise.bits.ILLEGAL_CMD))
    {
       	gSDMode = SD_MODE_HC;
    }

	return true;
}

/******************************************************************************
** Function:	Poll SD card for initialisation complete
**
** Notes:		Sets mediaInformation.errorCode = MEDIA_NO_ERROR when ready
*/
MEDIA_INFORMATION * CFS_sd_card_ready(void)
{
	uint16 i;
    MMC_RESPONSE response;
	int index;
	int count;
	BYTE CSDResponse[20];
	DWORD c_size;
	BYTE c_size_mult;
	BYTE block_len;

	mediaInformation.errorCode = MEDIA_CANNOT_INITIALIZE;	// by default
	if (gSDMode == SD_MODE_HC)
	{
       	response = SendMMCCmd(SEND_OP_COND, 0x40000000);
	    if (response.r1._byte != 0x00)
			return &mediaInformation;

		response = SendMMCCmd(READ_OCR, 0x0);
        if (((response.r7.bytewise._returnVal & 0xC0000000) != 0xC0000000) || (response.r7.bytewise._byte != 0))
			gSDMode = SD_MODE_NORMAL;
	}
    else								// gSDMode = SD_MODE_NORMAL;
	{
        response = SendMMCCmd(SEND_OP_COND, 0x0);
	    if (response.r1._byte != 0x00)
			return &mediaInformation;
	}

	TIM_delay_ms(2);
	OpenSPIM(SYNC_MODE_FAST);

	/* Send the CMD9 to read the CSD register */
    i = 0xFFF;
    do
    {
		response = SendMMCCmd(SEND_CSD, 0x00);
        i--;
    } while((response.r1._byte != 0x00) && (i != 0));

	/* According to the simplified spec, section 7.2.6, the card will respond
	with a standard response token, followed by a data block of 16 bytes
	suffixed with a 16-bit CRC.*/
	index = 0;
	for (count = 0; count < 20; count ++)
	{
		CSDResponse[index] = MDD_SDSPI_ReadMedia();
		index ++;			
		/* Hopefully the first byte is the datatoken, however, some cards do
		not send the response token before the CSD register.*/
		if((count == 0) && (CSDResponse[0] == DATA_START_TOKEN))
		{
			/* As the first byte was the datatoken, we can drop it. */
			index = 0;
		}
	}

	//Extract some fields from the response for computing the card capacity.
	//Note: The structure format depends on if it is a CSD V1 or V2 device.
	//Therefore, need to first determine version of the specs that the card 
	//is designed for, before interpreting the individual fields.

	//-------------------------------------------------------------
	//READ_BL_LEN: CSD Structure v1 cards always support 512 byte
	//read and write block lengths.  Some v1 cards may optionally report
	//READ_BL_LEN = 1024 or 2048 bytes (and therefore WRITE_BL_LEN also 
	//1024 or 2048).  However, even on these cards, 512 byte partial reads
	//and 512 byte write are required to be supported.
	//On CSD structure v2 cards, it is always required that READ_BL_LEN 
	//(and therefore WRITE_BL_LEN) be 512 bytes, and partial reads and
	//writes are not allowed.
	//Therefore, all cards support 512 byte reads/writes, but only a subset
	//of cards support other sizes.  For best compatibility with all cards,
	//and the simplest firmware design, it is therefore preferrable to 
	//simply ignore the READ_BL_LEN and WRITE_BL_LEN values altogether,
	//and simply hardcode the read/write block size as 512 bytes.
	//-------------------------------------------------------------
	gMediaSectorSize = 512u;
	mediaInformation.validityFlags.bits.sectorSize = TRUE;
	mediaInformation.sectorSize = gMediaSectorSize;
	//-------------------------------------------------------------

	//Calculate the MDD_SDSPI_finalLBA (see SD card physical layer simplified spec 2.0, section 5.3.2).
	//In USB mass storage applications, we will need this information to 
	//correctly respond to SCSI get capacity requests.  Note: method of computing 
	//MDD_SDSPI_finalLBA depends on CSD structure spec version (either v1 or v2).
	if(CSDResponse[0] & 0xC0)	//Check CSD_STRUCTURE field for v2+ struct device
	{
		//Must be a v2 device (or a reserved higher version, that doesn't currently exist)

		//Extract the C_SIZE field from the response.  It is a 22-bit number in bit position 69:48.  This is different from v1.  
		//It spans bytes 7, 8, and 9 of the response.
		c_size = (((DWORD)CSDResponse[7] & 0x3F) << 16) | ((WORD)CSDResponse[8] << 8) | CSDResponse[9];
		
		MDD_SDSPI_finalLBA = ((DWORD)(c_size + 1) * (WORD)(1024u)) - 1; //-1 on end is correction factor, since LBA = 0 is valid.
	}
	else //if(CSDResponse[0] & 0xC0)	//Check CSD_STRUCTURE field for v1 struct device
	{
		//Must be a v1 device.
		//Extract the C_SIZE field from the response.  It is a 12-bit number in bit position 73:62.  
		//Although it is only a 12-bit number, it spans bytes 6, 7, and 8, since it isn't byte aligned.
		c_size = ((DWORD)CSDResponse[6] << 16) | ((WORD)CSDResponse[7] << 8) | CSDResponse[8];	//Get the bytes in the correct positions
		c_size &= 0x0003FFC0;	//Clear all bits that aren't part of the C_SIZE
		c_size = c_size >> 6;	//Shift value down, so the 12-bit C_SIZE is properly right justified in the DWORD.
		
		//Extract the C_SIZE_MULT field from the response.  It is a 3-bit number in bit position 49:47.
		c_size_mult = ((WORD)((CSDResponse[9] & 0x03) << 1)) | ((WORD)((CSDResponse[10] & 0x80) >> 7));

        //Extract the BLOCK_LEN field from the response. It is a 4-bit number in bit position 83:80.
        block_len = CSDResponse[5] & 0x0F;

        block_len = 1 << (block_len - 9); //-9 because we report the size in sectors of 512 bytes each
		
		//Calculate the MDD_SDSPI_finalLBA (see SD card physical layer simplified spec 2.0, section 5.3.2).
		//In USB mass storage applications, we will need this information to 
		//correctly respond to SCSI get capacity requests (which will cause MDD_SDSPI_ReadCapacity() to get called).
		MDD_SDSPI_finalLBA = ((DWORD)(c_size + 1) * (WORD)((WORD)1 << (c_size_mult + 2)) * block_len) - 1;	//-1 on end is correction factor, since LBA = 0 is valid.		
	}	

    // Turn off CRC7 if we can, might be an invalid cmd on some cards (CMD59)
    response = SendMMCCmd(CRC_ON_OFF,0x0);

    // Now set the block length to media sector size. It should be already
    response = SendMMCCmd(SET_BLOCKLEN,gMediaSectorSize);

	mediaInformation.errorCode = MEDIA_NO_ERROR;
	return &mediaInformation;

	// old code:
	/*
	// Turn off CRC7 if we can, might be an invalid cmd on some cards (CMD59)
	response = SendMMCCmd(CRC_ON_OFF, 0x0);

	// Now set the block length to media sector size. It should be already
	response = SendMMCCmd(SET_BLOCKLEN, MEDIA_SECTOR_SIZE);

	for(i = 255; i > 0; i--)
	{
		if (MDD_SDSPI_SectorRead(0x0,NULL))
			break;
	}

	// see if we had an issue
	if (i == 0)
	{
		SD_CS = 1;                               // deselect the devices
		return false;
	}

	return true;
	*/
}

/******************************************************************************
** Function:	Initialize custom file system
**
** Notes:		Must do TIM_init first. Can take ~1.7 sec
*/
void CFS_init(void)
{
	// Ensure SD card is powered down for about a second, in case we crashed & reset
	HDW_SD_CARD_ON_N = true;
	CFS_timer_x20ms = 50;
	CFS_state = CFS_OFF;
	do
	{
		TIM_task();
		CFS_task();
	} while (CFS_timer_x20ms != 0);

	// Get file system open:
	while (!CFS_open())
	{
		TIM_task();
		CFS_task();
	}

	CFS_timer_x20ms = CFS_STAY_ON_TIMEOUT_X20MS;
}

/******************************************************************************
** Function:	Power up & initialise file system if it has not already been done
**
** Notes:	Returns CFS_state - can be CFS_OFF, CFS_POWERING, CFS_INITIALISING,
**			CFS_OPENING, CFS_OPEN 
**			or CFS_FAILED_TO_OPEN for 30 seconds timeout, then reverts to CFS_OFF
*/
bool CFS_open(void)
{
	if (HDW_SD_CARD_ON_N)								// currently off
		CFS_state = CFS_OFF;

	switch (CFS_state)
	{
	case CFS_OFF:
		HDW_SPI1_SS_N = true;							// deselect
		HDW_SD_CARD_ON_N = false;						// switch on
		SLP_set_required_clock_speed();
		CFS_state = CFS_POWERING;
		CFS_timer_x20ms = CFS_POWERING_TIMEOUT_X20MS;
		break;

	case CFS_OPEN:
		CFS_timer_x20ms = CFS_STAY_ON_TIMEOUT_X20MS;
		return true;

	case CFS_FAILED:									// pretend it's OK
		return true;

	//case CFS_POWERING:
	//case CFS_INITIALISING:
	//case CFS_OPENING:
	}
	
	return (false);
}

/******************************************************************************
** Function:	CFS_task
**
** Notes:	
*/
void CFS_task(void)
{
	if (TIM_20ms_tick && (CFS_timer_x20ms != 0))
		CFS_timer_x20ms--;

	switch (CFS_state)
	{
	case CFS_OFF:																	// nothing to do
	case CFS_OPEN:
		break;

	case CFS_FAILED:
		if (CFS_timer_x20ms == 0)
			CFS_power_down();														// sets CFS_state to CFS_OFF
		break;

	case CFS_POWERING:
		if (CFS_timer_x20ms == 0)
		{
			CFS_init_counter = 0;
			CFS_state = CFS_INITIALISING;
			CFS_timer_x20ms = CFS_INIT_TIMEOUT_X20MS;
		}
		break;

	case CFS_INITIALISING:
		if (TIM_20ms_tick)
		{
			if (cfs_init_sd_card())
			{
				CFS_state = CFS_OPENING;
				CFS_timer_x20ms = CFS_OPENING_TIMEOUT_X20MS;
				CFS_open_counter = 0;
			}
			else if (CFS_timer_x20ms == 0)
			{
				if (CFS_first_assert == 0)
					CFS_first_assert = 1;
				CFS_last_assert = 1;
				CFS_state = CFS_FAILED;
				CFS_timer_x20ms = CFS_FAILED_TIMEOUT_X20MS;
			}
			else
				CFS_init_counter++;
		}
		break;

	case CFS_OPENING:
		if (TIM_20ms_tick)
		{
			if (FSInit())
			{
				CFS_state = CFS_OPEN;
				CFS_timer_x20ms = CFS_STAY_ON_TIMEOUT_X20MS;
			}
			else if (CFS_timer_x20ms == 0)
			{
				if (CFS_first_assert == 0)
					CFS_first_assert = 2;
				CFS_last_assert = 2;
				CFS_state = CFS_FAILED;
				CFS_timer_x20ms = CFS_FAILED_TIMEOUT_X20MS;
			}
			else
				CFS_open_counter++;
		}
		break;

	default:
		if (CFS_first_assert == 0)
			CFS_first_assert = 3;
		CFS_last_assert = 3;
		CFS_state = CFS_FAILED;
		CFS_timer_x20ms = CFS_FAILED_TIMEOUT_X20MS;
		break;
	}
}

/******************************************************************************
** Function:	Power down down file system
**
** Notes:	
*/
void CFS_power_down(void)
{
	HDW_SD_CARD_ON_N = true;						// switch off
	HDW_SPI1_SS_N = false;							// CS idles low when powered down
	SPIENABLE = false;
	SLP_set_required_clock_speed();

	CFS_state = CFS_OFF;							// re-initialise next time
}

/******************************************************************************
** Function:	Close file
**
** Notes:		Leave file system powered up for a bit afterwards
*/
void CFS_close_file(FSFILE *f)
{
	if (CFS_state != CFS_OPEN)
		return;

	FSfclose(f);
	CFS_timer_x20ms = CFS_STAY_ON_TIMEOUT_X20MS;
}

/******************************************************************************
** Function:	Open file in specified mode
**
** Notes:		Returns NULL if can't. Don't forget to close it.
*/
FSFILE *cfs_open_file(char * path, char * filename, char *mode)
{
	if (!CFS_open())
		return NULL;

	if (CFS_state != CFS_OPEN)
		return NULL;

	if (FSchdir((*path == '\0') ? "\\" : path) != 0)	// can't set working directory
	{
		// if we are trying to write the file, create the directory:
		if ((*mode == 'w') || (*mode == 'a'))
		{
			FSmkdir(path);

			if (FSchdir((*path == '\0') ? "\\" : path) != 0)	// still can't set working directory
				return NULL;
		}
		else				// read mode, directory doesn't exist
			return NULL;
	}

	return FSfopen(filename, mode);
}

/******************************************************************************
** Function:	Check if file exists
**
** Notes:		CFS must be open first
*/
bool CFS_file_exists(char * path, char * filename)
{
	if (CFS_state != CFS_OPEN)
		return false;

	if (FSchdir((*path == '\0') ? "\\" : path) != 0)	// can't set working directory
		return false;

	if (FindFirst(filename, ATTR_MASK & ~ATTR_DIRECTORY, &cfs_search_result) == 0)	// success
		return true;

	return false;
}

/******************************************************************************
** Function:	Get contents of a file into buffer
**
** Notes:		Returns false if can't. Adds '\0' after end-of-file.
**				Empty path is root.
*/
bool CFS_read_file(char * path, char * filename, char * buffer, int max_bytes)
{
	FSFILE *f;

	f = cfs_open_file(path, filename, "r");		// returns NULL if FS not open
	if (f == NULL)
		return false;

	FSfread(buffer, max_bytes - 1, 1, f);		// ignore return code - probably EOF.
	if (f->size < (unsigned int)max_bytes)
		buffer[f->size] = '\0';
	else
		buffer[max_bytes - 1] = '\0';
	
	CFS_close_file(f);
	return true;
}

/******************************************************************************
** Function:	Get a block of bytes from given seek position in a file into buffer
**
** Notes:		Returns false if can't. Adds '\0' after end-of-file.
**				Empty path is root.
**				Must end properly by closing file system in all cases - PB
*/
bool CFS_read_block(char * path, char * filename, char * buffer, long seek_pos, int bytes)
{
	FSFILE *f;
	int		result;

	if (bytes == 0)
		return false;

	f = cfs_open_file(path, filename, "r");
	if (f == NULL)
		return false;

	result = FSfseek(f, seek_pos, SEEK_SET);
	if (result == -1)
	{
		CFS_close_file(f);
		return false;
	}
	FSfread(buffer, bytes, 1, f);
	if ((f->size - (DWORD)seek_pos) <= (DWORD)bytes)
	{
		buffer[(int)(f->size - (DWORD)seek_pos)] = '\0';
	}

	CFS_close_file(f);
	return true;
}

/******************************************************************************
** Function:	Get a line of a file into a string
**
** Notes:		Returns string length, or -1 if can't. Adds '\0' at end of string.
**				Empty path is root.
**				If the file exists but the line doesn't, returns 0.
**				LOOK FOR RETURN VALUE < 1 TO INDICATE NO FILE OR NO STRING.
*/
int CFS_read_line(char * path, char * filename, int n, char * buffer, int max_bytes)
{
	FSFILE *f;
	int length;

	f = cfs_open_file(path, filename, "r");
	if (f == NULL)
	{
		*buffer = '\0';
		return -1;
	}

	length = FS_read_line(f, n, buffer, max_bytes);
	if ((length > 0) && (*buffer == '\0'))	// workaround if FS_read_line comes back blank
		length = 0;							// preserves return of -1 for EOF

	CFS_close_file(f);
	return length;
}

/******************************************************************************
** Function:	Write contents of a buffer to a file
**
** Notes:		Returns false if can't. Empty path is root. Mode is "w" or "a". 
**				v3.05: do not open file if empty string, just return true
*/
bool CFS_write_file(char * path, char * filename, char * mode, char * buffer, int n_bytes)
{
	FSFILE *f;
	bool success;

	// All writes to SD disabled if battery flat
	if (LOG_state == LOG_BATT_DEAD)
		return false;

	if (n_bytes == 0)
		return true;

	f = cfs_open_file(path, filename, mode);
	if (f == NULL)
		return false;

	success = (FSfwrite(buffer, n_bytes, 1, f) == 1);

	CFS_close_file(f);
	return success;
}

/******************************************************************************
** Function:	Find youngest file in folder
**
** Notes:		Returns number of files in folder, -1 if file system unavailable.
*/
int CFS_find_youngest_file(char * path, char * result)
{
	unsigned long youngest;
	int i;

	if (!CFS_open())
		return -1;

	if (CFS_state != CFS_OPEN)
		return -1;

	if (FSchdir(path) != 0)			// can't set working directory
		return -1;

	i = 0;
	result[0] = '\0';
	if (FindFirst("*.*", ATTR_MASK & ~ATTR_DIRECTORY, &cfs_search_result) == 0)	// success
	{
		youngest = cfs_search_result.timestamp;
		strcpy(result, cfs_search_result.filename);
		i = 1;

		while (FindNext(&cfs_search_result) == 0)
		{
			i++;

			if (cfs_search_result.timestamp > youngest)
			{
				youngest = cfs_search_result.timestamp;
				strcpy(result, cfs_search_result.filename);
			}
		}
	}

	return i;
}

/******************************************************************************
** Function:	Find oldest file in folder
**
** Notes:		Returns number of files in folder, -1 if file system unavailable.
*/
int CFS_find_oldest_file(char * path, char * result)
{
	unsigned long oldest;
	int i;

	if (!CFS_open())
		return -1;

	if (CFS_state != CFS_OPEN)
		return -1;

	if (FSchdir(path) != 0)			// can't set working directory
		return -1;

	i = 0;
	result[0] = '\0';
	if (FindFirst("*.*", ATTR_MASK & ~ATTR_DIRECTORY, &cfs_search_result) == 0)	// success
	{
		oldest = cfs_search_result.timestamp;
		strcpy(result, cfs_search_result.filename);
		i = 1;

		while (FindNext(&cfs_search_result) == 0)
		{
			i++;

			if (cfs_search_result.timestamp < oldest)
			{
				oldest = cfs_search_result.timestamp;
				strcpy(result, cfs_search_result.filename);
			}
		}
	}

	return i;
}

/******************************************************************************
** Function:	Purge oldest file in folder
**
** Notes:	
*/
bool CFS_purge_oldest_file(char * path)
{
	if (CFS_find_oldest_file(path, cfs_filename) < 0)
		return false;

	if (FSremove(cfs_filename) == 0)			// Remove while working directory still selected
	{
		LOG_enqueue_value(LOG_ACTIVITY_INDEX, LOG_CFS_FILE, __LINE__);		// Oldest file purged from folder
		return true;
	}
	else
		return false;
}

/******************************************************************************
** Function:	Get filesize from last search result
**
** Notes:	
*/
int CFS_search_filesize(void)
{
	return cfs_search_result.filesize;
}


