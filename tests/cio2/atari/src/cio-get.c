/**
 * CIO Get call
 */

#include <atari.h>
#include <6502.h>

extern unsigned char err;
extern unsigned char ret;
unsigned char c=0;

const char testmsg[]="Testing Input\x9b";

void _cio_get(void)
{
  ret=testmsg[c];
  if (c>14)
    c=0;
  err=1;
}
