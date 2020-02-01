/**
 * CIO Get call
 */

#include <atari.h>
#include <6502.h>
#include <stdbool.h>
#include "sio.h"
#include "misc.h"

extern unsigned char err;
extern unsigned char ret;
extern unsigned char aux1_save[8];
extern unsigned char aux2_save[8];
extern unsigned char* rp;
extern unsigned char buffer_rx[256];
extern unsigned char buffer_rx_len;

bool skip_char;

extern void _cio_status(void); // Used to get length to fetch.

void _cio_get(void)
{
  err=1;
  if (buffer_rx_len==0)
    {
      // Buffer empty, get length and read it in.
      _cio_status();
      buffer_rx_len=OS.dvstat[0];

      err=siov(DEVIC_N,
	       OS.ziocb.drive,
	       'r',
	       DSTATS_READ,
	       &buffer_rx,
	       buffer_rx_len,
	       DTIMLO_DEFAULT,
	       buffer_rx_len,
	       0);
      
      rp=&buffer_rx[0];
    }

  if ((buffer_rx_len==0) && (OS.dvstat[2]==0)) // dvstat[2] = disconnected
    {
      err=ret=136;
      return;
    }
  else if (buffer_rx_len==0)
    {
      err=ret=136;
      return;
    }
  
  
  if (!(OS.ziocb.command&2)) // GETREC
    {
      if (*rp==0x0a)
	{
	  *rp=0x9b;
	}
      else if (*rp==0x0d)
	{
	  *rp=0x20;
	}
    }

  // Send next char in buffer.
  if (buffer_rx_len>0)
    {
      buffer_rx_len--;
      ret=*rp++;
    }
}
