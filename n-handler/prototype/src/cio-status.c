/**
 * CIO Status call
 */

#include <atari.h>
#include <6502.h>
#include <string.h>
#include "sio.h"
#include "config.h"

extern unsigned char err;
extern unsigned char ret;
extern unsigned char aux1_save[MAX_DEVICES];
extern unsigned char aux2_save[MAX_DEVICES];
extern unsigned char buffer_rx_len[MAX_DEVICES];

static unsigned char status_save[4];
extern unsigned char trip;

void _cio_status_poll(void)
{
  unsigned short l;
  err=siov(DEVIC_N,
	   OS.ziocb.drive,
	   'S',
	   DSTATS_READ,
	   OS.dvstat,
	   4,
	   DTIMLO_DEFAULT,
	   OS.ziocb.aux1,
	   OS.ziocb.aux2);

  l=OS.dvstat[1]*256+OS.dvstat[0];
  if (l>255)
    {
      OS.dvstat[0]=255;
      OS.dvstat[1]=0;
    }

  if (l==0)
    {
      trip=0;
    }
}

void _cio_status(void)
{
  err=1;
  
  if (trip==1)
    {
      _cio_status_poll();
      err=OS.dcb.dstats;
      memcpy(status_save,OS.dvstat,4);
    }
  else
    {
      OS.dvstat[0]=buffer_rx_len[OS.ziocb.drive-1]&0xFF;
      OS.dvstat[1]=0;
    }
  
  ret=OS.dvstat[2];   
}
