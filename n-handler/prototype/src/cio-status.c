/**
 * CIO Status call
 */

#include <atari.h>
#include <6502.h>
#include "sio.h"

extern unsigned char err;
extern unsigned char ret;
extern unsigned char aux1_save[MAX_DEVICES];
extern unsigned char aux2_save[MAX_DEVICES];

void _cio_status(void)
{

  err=siov(DEVIC_N,
	   OS.ziocb.drive,
	   'S',
	   DSTATS_READ,
	   OS.dvstat,
	   4,
	   DTIMLO_DEFAULT,
	   OS.ziocb.aux1,
	   OS.ziocb.aux2);
  
  ret=OS.dvstat[2];
}
