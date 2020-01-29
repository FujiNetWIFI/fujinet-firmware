/**
 * CIO Get call
 */

#include <atari.h>
#include <6502.h>
#include <stdbool.h>
#include "sio.h"

extern unsigned char err;
extern unsigned char ret;
extern unsigned char len;
extern unsigned char aux1_save[8];
extern unsigned char aux2_save[8];

unsigned char* p;

void _cio_get(void)
{
  
  if (len==0)
    {
      ret=err=siov(DEVIC_N,
		   OS.ziocb.drive,
		   'r',
		   DSTATS_READ,
		   OS.ziocb.buffer,
		   OS.ziocb.buflen,
		   DTIMLO_DEFAULT,
		   aux1_save[OS.ziocb.drive],
		   aux2_save[OS.ziocb.drive]);
      p=OS.ziocb.buffer;
      len=OS.ziocb.buflen;
    }
  
  if (OS.ziocb.command&0x02)
    {
      // GETREC requested, do EOL/CRLF translation
      if (((*p==0x0a) && (*p+1==0x0d)) || ((*p==0x0d) && (*p+1==0x0a)))
	p++; // scoot ahead

      if ((*p==0x0a) || (*p==0x0d))
	*p=0x9b; // change to EOL
    }
  
  ret=*p;
}
