/**
 * CIO Put call
 */

#include <atari.h>
#include <6502.h>
#include <string.h>
#include "sio.h"

extern unsigned char err;
extern unsigned char ret;
extern unsigned char aux1_save[8];
extern unsigned char aux2_save[8];

unsigned char* putp;
unsigned short putlen;

void _cio_put(void)
{
  unsigned short i;

  if (putlen==0)
    {
      putp=OS.ziocb.buffer;
      putlen=OS.ziocb.buflen;
      if (!(OS.ziocb.command&0x02))
	{
	  // PUTREC requested, do EOL to CR translation
	  for (i=0;i<OS.ziocb.buflen;i++)
	    if (putp[i]==0x9B)
	      putp[i]=0x0D;
	}
    }
  else if (putlen==OS.ziocb.buflen)
    {
      err=siov(DEVIC_N,
  	       OS.ziocb.drive,
  	       'w',
  	       DSTATS_WRITE,
  	       OS.ziocb.buffer,
  	       OS.ziocb.buflen,
  	       DTIMLO_DEFAULT,
  	       aux1_save[OS.ziocb.drive],
  	       aux2_save[OS.ziocb.drive]);
      putlen=0;
    }
  else
    putlen++;

  ret=putp[putlen];
  err=1;
}



