/**
 * CIO Get call
 */

#include <atari.h>
#include <6502.h>
#include <stdbool.h>
#include "sio.h"

extern unsigned char err;
extern unsigned char ret;
extern unsigned char packet[256];
extern long filesize;

unsigned char packetlen;
unsigned char* p;
unsigned char done=false;

void _cio_get_chr(void)
{
}
