/**
 * CIO Close call
 */

#include <atari.h>
#include <6502.h>
#include <stddef.h>
#include <stdbool.h>
#include "sio.h"

extern unsigned char err;
extern unsigned char ret;
extern unsigned char aux1_save[8];
extern unsigned char aux2_save[8];

extern void cio_put_flush(void);

void _cio_close(void)
{
  cio_put_flush();
  
  siov(DEVIC_N,OS.ziocb.drive,
	       'c',
	       DSTATS_NONE,
	       NULL,
	       DBYT_NONE,
	       DTIMLO_DEFAULT,
	       aux1_save[OS.ziocb.drive],
	       aux2_save[OS.ziocb.drive]);

  aux1_save[OS.ziocb.drive]=
    aux2_save[OS.ziocb.drive]=0;

  ret=1;
  err=1;
}
