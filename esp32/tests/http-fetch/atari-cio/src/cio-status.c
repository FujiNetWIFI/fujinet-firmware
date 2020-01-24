/**
 * CIO Status call
 */

#include <atari.h>
#include <6502.h>
#include "sio.h"

extern unsigned char err;
extern unsigned char ret;

void _cio_status(void)
{
  err=1;
  ret=0;
}
