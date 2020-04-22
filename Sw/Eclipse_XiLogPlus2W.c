/*
 * Eclipse_XiLogPlus2W.c
 *
 *  Created on: 22 Apr 2020
 *      Author: temp
 */


/*
 ============================================================================
 Name        : MyTest.c
 Author      : Jon C
 Version     :
 Copyright   : Your copyright notice
 Description : Hello World in C, Ansi-style
 ============================================================================
 */
#ifdef ECLIPSE
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "Version.h"
#include "Custom.h"

#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
typedef unsigned long DWORD;

#ifdef WIN32
#define extern
#define extern
#include <p24Fxxxx.h>
#undef extern
#endif

//#include "Sch.h"
//#include "Dbg.h"
//#define __attribute__(x, y, z)
//#define far	unsigned long
//#define sfr unsigned long
//#include "Compiler.h"

int app_main(void);
#define app_main(x) main(x)

struct
{
	int line_index;

	char line_buffer[1024];
	char output_buffer[1024];
} app;
//extern unsigned short TMR1;
DWORD last_tick_count;

void DBG_parse(char *p_cmd, char *p_output);
#ifdef NULL
#undef NULL
#define NULL 0
#endif
/* reads from keypress, doesn't echo */
int getch(void)
{
	struct termios oldattr, newattr;
	int ch;
	tcgetattr( STDIN_FILENO, &oldattr );
	newattr = oldattr;
	newattr.c_lflag &= ~( ICANON | ECHO );
	tcsetattr( STDIN_FILENO, TCSANOW, &newattr );
	ch = getchar();
	tcsetattr( STDIN_FILENO, TCSANOW, &oldattr );
	//printf("getch %d", ch);
	return ch;
}
bool kbhit()
{
	struct termios term;
	tcgetattr(0, &term);

	struct termios term2 = term;
	term2.c_lflag &= ~ICANON;
	tcsetattr(0, TCSANOW, &term2);

	int byteswaiting;
	ioctl(0, FIONREAD, &byteswaiting);

	tcsetattr(0, TCSANOW, &term);

	return byteswaiting > 0;
}
//*****************************************************************************
// Function:	Process keypress
//
// Notes:		Returns false if quit required
//
int process_keypress(void)
{
	char c = getch();//_getch_nolock();
	putchar(c);//_putch(c);

	//printf("char %d", (uint8_t) c);
	if (c == '\b')					// backspace
	{
		if (app.line_index > 0)
			app.line_index--;

//		_putch(' ');
//		_putch(c);
		putchar(' ');
		putchar(c);
		return true;
	}
	// else:

	if ((c == '\n') || (app.line_index >= sizeof(app.line_buffer)))
	{
		c = '\0';
	}
	app.line_buffer[app.line_index++] = c;
	if (c == '\0')								// parse line buffer
	{
		if (app.line_index == 1)
			printf("\r\n> ");
		else
		{
			putchar('\n');
			//_putch('\n');
			DBG_parse(app.line_buffer, app.output_buffer);
			//printf("%s> ", app.output_buffer);
		}

		app.line_index = 0;
	}

	return true;
}
typedef unsigned long uint64_t;
/// Returns the number of ticks since an undefined time (usually system startup).
static uint64_t GetTickCountMs()
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);

	return (uint64_t)(ts.tv_nsec / 1000000) + ((uint64_t)ts.tv_sec * 1000ull);
}
//*****************************************************************************
// Function:	PC sleep function
//
// Notes:
//
void PC_sleep(int x)
{
	usleep(x*1000);
	DWORD t_ms = GetTickCountMs();
	TMR1 += (unsigned short)((t_ms - last_tick_count) * 4);
	//printf("%d\n", TMR1);
	last_tick_count = t_ms;

	if (kbhit())
	{
		if (!process_keypress())
			exit(0);											// 'Q'
	}

}
//*****************************************************************************
// Function:	Dummy functions to allow PC version to link
//
// Notes:
//
//void __builtin_nop(void)
//{
//}
//void __builtin_pwrsav(int i)
//{
//}
void nop(void)
{
}
void pwrsav(int i)
{
}
int main(void)
{
	printf("\r\n*** %s", VER_FIRMWARE_BCD);
	printf(" Eclipse build\r\n");

	return app_main();

}
#else
//*****************************************************************************
// File:	XiLogNG_PC.cpp: PC Simulation of XiLogNG
//
// Notes:	Defines the entry point for the console application.
//

// Modify the following defines if you have to target a platform prior to the ones specified below.
// Refer to MSDN for the latest info on corresponding values for different platforms.
//#ifndef _WIN32_WINNT			// Specifies that the minimum required platform is Windows Vista.
//#define _WIN32_WINNT 0x0600	 // Change this to the appropriate value to target other versions of Windows.
//#endif
//
#include <windows.h>
#include <stdio.h>
#include <tchar.h>
#include <conio.h>

#undef VER_H
#include "..\Src\Ver.h"

#define false 0
#define true !false

extern unsigned short TMR1;
DWORD last_tick_count;
int app_main(void);

void DBG_parse(char *p_cmd, char *p_output);


//*****************************************************************************
struct
{
	int line_index;

	char line_buffer[1024];
	char output_buffer[1024];
} app;

//*****************************************************************************
// Function:	Process keypress
//
// Notes:		Returns false if quit required
//
int process_keypress(void)
{
	char c = _getch_nolock();
	_putch(c);
	if (c == '\b')					// backspace
	{
		if (app.line_index > 0)
			app.line_index--;

		_putch(' ');
		_putch(c);
		return true;
	}
	// else:

	if ((c == '\r') || (app.line_index >= sizeof(app.line_buffer)))
		c = '\0';

	app.line_buffer[app.line_index++] = c;
	if (c == '\0')								// parse line buffer
	{
		if (app.line_index == 1)
			printf("\r\n> ");
		else
		{
			_putch('\n');
			DBG_parse(app.line_buffer, app.output_buffer);
			printf("%s> ", app.output_buffer);
		}

		app.line_index = 0;
	}

	return true;
}

//*****************************************************************************
// Function:	PC Simulation of XiLog_IoT: main entry point
//
// Notes:
//
int _tmain(int argc, _TCHAR* argv[])
{
	//printf("\r\n*** XiLogNG V%d.%02d\r\n\r\n> ", VER_FIRMWARE_BCD >> 8, VER_FIRMWARE_BCD & 0xFF);
	printf("\r\n*** " VER_APP_ID_STRING " V" VER_FIRMWARE_STRING "\r\n\r\n> ");

	last_tick_count = GetTickCount();

	return app_main();
}

//*****************************************************************************
// Function:	PC sleep function
//
// Notes:
//
void PC_sleep(int x)
{
	Sleep(x);
	DWORD t_ms = GetTickCount();
	TMR1 += (unsigned short)((t_ms - last_tick_count) * 4);
	last_tick_count = t_ms;

	if (_kbhit())
	{
		if (!process_keypress())
			exit(0);											// 'Q'
	}

}

//*****************************************************************************
// Function:	Dummy functions to allow PC version to link
//
// Notes:
//
void __builtin_nop(void)
{
}
void __builtin_pwrsav(int i)
{
}




#endif /*LINUX*/

