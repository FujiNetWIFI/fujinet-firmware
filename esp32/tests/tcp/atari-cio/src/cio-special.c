/**
 * CIO Special (XIO) call
 */

#include <atari.h>
#include <6502.h>
#include <string.h>
#include "sio.h"
#include "misc.h"

extern unsigned char err;
extern unsigned char ret;
extern unsigned char aux1_save[8];
extern unsigned char aux2_save[8];

void _cio_special(void)
{
  unsigned char dcmd, dstats;
  unsigned short dbyt, aux1, aux2;
  void *buf;
  
  err=1;
  dstats=dbyt=0x00;
  aux1=aux1_save[OS.ziocb.drive];
  aux2=aux2_save[OS.ziocb.drive];
  buf=NULL;
  
  switch (OS.ziocb.command)
    {
    case 16: // Accept Connection
      dcmd='a';
      break;
    case 17: // unlisten
      dcmd='u';
      break;
    default:
      err=146; // Not implemented
    }

  ret=siov(DEVIC_N, OS.ziocb.drive,
	   dcmd,
	   dstats,
	   buf,	   
	   dbyt,
	   DTIMLO_DEFAULT,
	   aux1,
	   aux2);
}
