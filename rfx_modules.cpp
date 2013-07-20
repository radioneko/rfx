#include "rfx_modules.h"
#include "cconsole.h"
#include <stdio.h>
#include <string.h>

uint8_t
hex2i(char digit)
{
	if (digit >= '0' && digit <= '9')
		return digit - '0';
	if (digit >= 'a' && digit <= 'f')
		return digit - 'a' + 10;
	if (digit >= 'A' && digit <= 'F')
		return digit - 'A' + 10;
	return 0;
}
