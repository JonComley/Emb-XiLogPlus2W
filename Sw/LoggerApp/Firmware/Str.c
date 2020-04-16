/******************************************************************************
** File:	Str.c	
**
** Notes:	String functions
**
** v2.45 130111 PB Change STR_parse_quoted_string to more general STR_parse_delimited_string
*/

#include <string.h>
#include <stdio.h>
#include <float.h>

#include "custom.h"
#include "Msg.h"
#include "MDD File System/FSIO.h"

#define extern
#include "Str.h"
#undef extern

/******************************************************************************
** Function:	Parse delimited string - string within two given delimiter characters
**
** Notes:		If source string does not contain start delimiter character, destination is empty string.
**				extracts characters up to max_length - max_length must include space for '\0'
**				Returns address of last delimiter or end of input string
*/
char * STR_parse_delimited_string(char *source, char *destination, int max_length, char start_del, char end_del)
{
	int i;

	i = 0;
	// find first delimiter
	while ((*source != start_del) && (*source != '\0'))
	{
		source++;
	}
	if (*source == start_del)			// found before end of input string
	{
		source++;
		max_length--;
		while (*source != '\0')
		{
			if (*source == end_del)
				break;

			if (i < max_length)
				*destination++ = *source++;
			i++;
		}
	}

	*destination = '\0';				// terminate
	return source;						// return position of end delimiter
}

/******************************************************************************
** Function:	Perform case-insensitive string comparison
**
** Notes:		Goes up to length of 2nd parameter. 2nd string must be LOWER case.
*/
bool STR_match(char * p, const char * q)
{
	while (*q != '\0')
	{
		if ((*p | _B00100000) != *q)
			return false;

		p++;
		q++;
	}

	return true;
}

/******************************************************************************
** Function:	Syntax check phone number referenced by pointer
**
** Notes:		No quotes, \0-terminated
*/
bool STR_phone_number_ok(char *p)
{
	if (strlen(p) >= MSG_PHONE_NUMBER_LENGTH)
		return false;
	// else:

	if (*p == '+')	// OK to begin with '+'
		p++;

	do
	{
		if ((*p < '0') || (*p > '9'))
			return false;
		p++;
	} while (*p != '\0');

	return true;
}

/******************************************************************************
** Function:	Print file timestamp into STR_buffer
**
** Notes:	
*/
void STR_print_file_timestamp(uint32 ts)
{
	int yr, mth, day;
	int hrs, min, sec;

	yr = 1980 + (BITS24TO31(ts) >> 1);
	mth = (HIGH16(ts) >> 5) & 0x0F;
	day = HIGH16(ts) & 0x1F;

	hrs = BITS8TO15(ts) >> 3;
	min = (LOW16(ts) >> 5) & 0x3F;
	sec = 2 * (BITS0TO7(ts) & 0x1F);

	sprintf(STR_buffer, "%02d/%02d/%02d,%02d:%02d:%02d", day, mth, yr %100, hrs, min, sec);
}

/******************************************************************************
** Function:	Format floating point values into a print string
**
** Notes:		Returns length. Output string will never exceed 9 characters plus '\0'
*/
int STR_print_float(char *p, float f)
{
	char * p_init;
	int i,r;
	bool all_zeroes;

	r = 0;
	p_init = p;
	if (f < 0.0)
	{
		f = -f;
		*p++ = '-';
		r = 1;
	}

	r += sprintf(p, "%G", (double)f);
	// check for string length
	if (r > 9)
	{
		sprintf(p, "%1.4G", (double)f);
		r = strlen(p_init);
		if (r > 9)
		{
			sprintf(p, "%1.3G", (double)f);
			r = strlen(p_init);
			if (r > 9)
			{
				sprintf(p, "%1.2G", (double)f);
				r = strlen(p_init);
				if (r > 9)
				{
					sprintf(p, "%1.1G", (double)f);
					r = strlen(p_init);
				}
			}
		}
	}
	// check for negative zero condition and remove sign
	if (*p_init == '-')
	{
		all_zeroes = true;
		for (i = 1; i < r; i++)
		{
			if ((p_init[i] != '0') && (p_init[i] != '.'))
				all_zeroes = false;
		}
		if (all_zeroes)
		{
			for (i = 1; i < r; i++)
			{
				p_init[i - 1] = p_init[i];
			}
			r--;
		}
	}

	return r;
}

/******************************************************************************
** Function:	Convert byte pattern of a 32 bit float (passed in as uint32) into a 21-bit one
**
** Notes:		returns value in the LS 21 bits of a uint32
*/
uint32 STR_float_32_to_21(uint32 value)
{
	uint32 mask21;
	int e;
	float f = *(float *)&value;

	// Compiler bug: don't amalgamate the following 2 lines!
	e = (HIGH16(value) >> 7) & 0xFF;
	e -= 127;												// e = -127 to + 128
	if (e == -127)											// f < 2^-126
		mask21 = 0;
	else if (e < -30)										// too small for normalized value
	{
		if (f < 0.0)										// f = abs(f)
			f = -f;

		f *= 1.759218604E13f;								// f *= 2^30 * 2^14
		mask21 = (unsigned long)f;							// non-normalized value
	}
	else if (e > 31)										// too large
		mask21 = (f > 4.29E9f) ? 0x000FFFFF : 0x000FBFFF;	// <NO VALUE> or maximum value
	else													// normalized value
	{
		mask21 = e + 31;
		mask21 <<= 14;
		mask21 |= (value >> 9) & 0x3FFF;
	}

	if ((value & 0x80000000) != 0)							// negative value
		mask21 |= 0x100000;

	return mask21;
}

/******************************************************************************
** Function:	Convert bit pattern of a 21 bit float (passed in as uint32) into a 32-bit float
**
** Notes:		Interpret 0x003FFFFF as FLT_MAX (it's actually -FLT_MAX), for use as NO_VALUE.
*/
float STR_float_21_to_32(uint32 w)
{
	unsigned int e;
	unsigned int sign;
	float value;

	sign = HIGH16(w) & 0x10;
	e = (w >> 14) & 0x3F;
 	w &= 0x3FFF;
	value = (float)w;
	
	if (e == 0)
		value *= 5.6843E-14f;	// pow(2.0, -30.0) / 16384.0f;
	else if (e == 63)
	{
		value = FLT_MAX;		// infinity, or NO VALUE
		sign = 0;				// interpret 0x003FFFFF as +FLT_MAX
	}
	else
	{
		value = 1.0f + (value / 16384.0f);
		if (e > 30)
		{
			w = 1 << (e - 31);
			value *= (float)w;
		}
		else
		{
			w = 1 << (31 - e);
			value /= (float)w;
		}
	}

	return (sign == 0) ? value : -value;
}

/******************************************************************************
** Function:	Parse individual hex digit
**
** Notes:		Returns value, or 0xFF if invalid.
*/
uint8 STR_parse_hex_digit(char c)
{
	if ((c >= '0') && (c <= '9'))
		return c - '0';

	c |= 32;	// convert to lower case
	if ((c >= 'a') && (c <= 'f'))
		return 10 + c - 'a';

	// else error:
	return 0xFF;
}






