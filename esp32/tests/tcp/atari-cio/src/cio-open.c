/**
 * CIO Open call
 */

#include <atari.h>
#include <6502.h>
#include <string.h>
#include <stdbool.h>
#include "sio.h"
#include "filename.h"

extern unsigned char err;
extern unsigned char ret;
extern unsigned char packet[256];
extern long filesize;
extern unsigned char aux1_save[8];
extern unsigned char aux2_save[8];

void _cio_open(void)
{
  // Save AUX1/AUX2 values
  aux_save(OS.ziocb.drive);
  
  ret=err=siov(DEVIC_N,OS.ziocb.drive,
	       'o',
	       DSTATS_READ,
	       &packet,
	       DBYT_OPEN,
	       DTIMLO_DEFAULT,
	       aux1_save[OS.ziocb.drive],
	       aux2_save[OS.ziocb.drive]);
  
}
