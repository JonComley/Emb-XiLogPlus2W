/******************************************************************************
** File:	Str.h	
**
** Notes:	String functions
**
** v2.45 130111 PB Change STR_parse_quoted_string to more general STR_parse_delimited_string
*/

// Buffer used for a multitude...
extern char STR_buffer[512];

extern const char STR_hex_char[17]
#ifdef extern
	= "0123456789ABCDEF"
#endif
;

char * STR_parse_delimited_string(char *source, char *destination, int max_length, char start_del, char end_del);
bool STR_match(char * p, const char * q);
bool STR_phone_number_ok(char *p);
void STR_print_file_timestamp(uint32 ts);
int STR_print_float(char *p, float f);
uint32 STR_float_32_to_21(uint32 value);
float STR_float_21_to_32(uint32 w);
uint8 STR_parse_hex_digit(char c);

