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

      if ((buffer_rx_len==0) || (OS.dvstat[2]==0)) // dvstat[2] = disconnected
	{
	  err=ret=136;
	  return;
	}

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

  if (!(OS.ziocb.command&2)) // GETREC
    {
      // Convert CR/LF to EOL
      if ((*rp==0x0d) || (*rp==0x0a))
	{
	  if (skip_char==true) // did we get another CR or LF?
	    {
	      *rp++; // Yes, skip
	      buffer_rx_len--;
	      skip_char=false;
	    }
	  else
	    {
	      *rp=0x9B; // Convert character to EOL
	      skip_char=true;
	    }
	}
      else
	{ // Otherwise we are not skipping a character.
	  skip_char=false;
	}
    }

  // Send next char in buffer.
  buffer_rx_len--;
  ret=*rp++;
}
