/**
 * CIO Get call
 */

#include <atari.h>
#include <6502.h>

extern unsigned char err;
extern unsigned char ret;

void _cio_get(void)
{
  err=1;
}
