/**
 * CIO Get call
 */

#include <atari.h>
#include <6502.h>

extern unsigned char err;
extern unsigned char ret;

char testmsg[]="Testing Input\x9b";
unsigned char c;

void _cio_get(void)
{
  ret=testmsg[c++];
  err=1;
}
