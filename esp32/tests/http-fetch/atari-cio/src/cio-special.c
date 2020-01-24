/**
 * CIO Special (XIO) call
 */

#include <atari.h>
#include <6502.h>
#include <string.h>
#include "sio.h"

extern unsigned char err;
extern unsigned char ret;
extern unsigned char packet[256];

void _cio_special(void)
{
  err=1;
}
