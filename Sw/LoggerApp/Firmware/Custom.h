/********************************************************************************
* File:		custom.h
* Author:	Dr Mark Agate
*
* NOTE:		assumes sizeof(short) = 2, sizeof(long) = 4,
*			little-endian storage, LSB first in bitfields.
*/

#ifndef custom_h
#define custom_h

#include "GenericTypeDefs.h"
#include "Binary.h"

// 16-bit constant expressed MSB, LSB:
#define _16BIT(MSB, LSB)	((MSB << 8) | LSB)

#define UINT16_MAX	0xFFFF

/********************************************************************************
* Typedefs
*/

typedef unsigned char	bool;
typedef unsigned char	uint8;
typedef unsigned short  uint16;
typedef unsigned long	uint32;

typedef signed char int8;
typedef signed short int16;
typedef signed long int32;
typedef long long int64;

#define uint8_t uint8
#define uint16_t uint16
#define uint32_t uint32

#define int8_t int8
#define int16_t int16
#define int32_t int32

typedef union
{
	struct
	{
		unsigned b0 : 1;
		unsigned b1 : 1;
		unsigned b2 : 1;
		unsigned b3 : 1;
		unsigned b4 : 1;
		unsigned b5 : 1;
		unsigned b6 : 1;
		unsigned b7 : 1;
		unsigned b8 : 1;
		unsigned b9 : 1;
		unsigned b10 : 1;
		unsigned b11 : 1;
		unsigned b12 : 1;
		unsigned b13 : 1;
		unsigned b14 : 1;
		unsigned b15 : 1;
	};

	uint16 mask;
} BITFIELD;

typedef struct
{
	uint8 c_low;
	uint8 c_high;
} U16;

typedef union
{
	struct
	{
		uint16 i_low;
		uint16 i_high;
	};

	struct
	{
		uint8 bits0to7;
		uint8 bits8to15;
		uint8 bits16to23;
		uint8 bits24to31;
	};
} U32;

typedef union
{
	long long value;

	struct
	{
		uint32 i_low;
		uint32 i_high;
	};
} I64;

// Pointers to functions not advisable for overlayed auto variables.
// typedef void (*ptr_to_function)(void); /* pointer to function with no return value */

/********************************************************************************
* Defines
*/

#ifndef false
#define false	0
#define true	(!false)
#endif

#ifdef WIN32

#pragma warning (disable : 4103)
#pragma warning (disable : 4311)
#pragma warning (disable : 4312)
#pragma warning (disable : 4005)	// macro redefinition
#pragma pack (1)

#define near
#define far

typedef unsigned char bit;

#define __attribute__(X)
#define __extension__
#define Nop()
#define ClrWdt()
#define Sleep()
#define Idle()
#define asm(X)
#define asm_volatile(X)
#define __builtin_write_OSCCONH(X)
#define __builtin_write_OSCCONL(X)
#define __builtin_tblrdl(X)		X
#define __builtin_tblrdh(X)		X
#define	__builtin_disi(X)

#define __PIC24F__
#define __PIC24FJ256GB110__
#define __C30__

#define __FLT_MAX__		3.402823e+38f

#else	// PIC

#define asm_volatile(X)		asm volatile(X)

#endif	// WIN32

#define LOWBYTE(X)	(((U16 *)&(X))->c_low)
#define HIGHBYTE(X)	(((U16 *)&(X))->c_high)

#define LOW16(X)	(((U32 *)&(X))->i_low)
#define HIGH16(X)	(((U32 *)&(X))->i_high)

#define LOW32(X)	(((I64 *)&(X))->i_low)
#define HIGH32(X)	(((I64 *)&(X))->i_high)

#define BITS0TO7(X)		(((U32 *)&(X))->bits0to7)
#define BITS8TO15(X)	(((U32 *)&(X))->bits8to15)
#define BITS16TO23(X)	(((U32 *)&(X))->bits16to23)
#define BITS24TO31(X)	(((U32 *)&(X))->bits24to31)

#endif		// custom_h

/********************************************************************************
* EOF
*/


