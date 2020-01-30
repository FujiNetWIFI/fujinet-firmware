/**
 * CIO Put call
 */

#include <atari.h>
#include <6502.h>
#include <string.h>
#include "sio.h"
#include "misc.h"

extern unsigned char err;
extern unsigned char ret;
extern unsigned char* tp;
extern unsigned char buffer_tx_len;
extern unsigned char buffer_tx[256];

/**
 * Flush output to socket
 */
void cio_put_flush(void)
{
  err=siov(DEVIC_N,
	   OS.ziocb.drive,
	   'w',
	   DSTATS_WRITE,
	   &buffer_tx,
	   buffer_tx_len,
	   DTIMLO_DEFAULT,
	   buffer_tx_len,
	   0);
  
  clear_tx_buffer();
}

void _cio_put(void)
{
  if ((ret==0x9b) || (buffer_tx_len>0xFE))
    {
      buffer_tx[buffer_tx_len++]=0x0d;
      buffer_tx[buffer_tx_len++]=0x0a;
      cio_put_flush();
    }
  else
    {
      buffer_tx[buffer_tx_len++]=ret;
    }
}
