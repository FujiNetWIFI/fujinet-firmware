/**
 * CIO Status call
 */

#include <atari.h>
#include <6502.h>

extern unsigned char err;
extern unsigned char ret;

void _cio_status(void)
{
  err=1;
}
