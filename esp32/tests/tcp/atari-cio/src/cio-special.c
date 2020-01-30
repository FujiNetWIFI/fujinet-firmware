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
  unsigned short dbyt;
  
  err=1;
  dstats=dbyt=0x00;
  
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
	   OS.ziocb.buffer,	   
	   dbyt,
	   DTIMLO_DEFAULT,
	   aux1_save[OS.ziocb.drive],
	   aux2_save[OS.ziocb.drive]);
}
