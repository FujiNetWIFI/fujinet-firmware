/**
 * CIO Status call
 */

#include <atari.h>
#include <6502.h>
#include "sio.h"

extern unsigned char err;
extern unsigned char ret;
extern unsigned char aux1_save[8];
extern unsigned char aux2_save[8];

void _cio_status(void)
{

  err=siov(DEVIC_N,
	   OS.ziocb.drive,
	   's',
	   DSTATS_READ,
	   OS.dvstat,
	   4,
	   DTIMLO_DEFAULT,
	   aux1_save[OS.ziocb.drive],
	   aux2_save[OS.ziocb.drive]);
  
  ret=OS.dvstat[0];
}
