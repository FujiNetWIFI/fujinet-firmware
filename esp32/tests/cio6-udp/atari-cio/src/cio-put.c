/**
 * CIO Put call
 */

#include <atari.h>
#include <6502.h>
#include <string.h>
#include "sio.h"

extern unsigned char err;
extern unsigned char ret;
extern unsigned char packet[512];

unsigned char putlen;

void _cio_put(void)
{
}
