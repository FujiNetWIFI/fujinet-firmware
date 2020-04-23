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

extern void _cio_status(void);

/**
 * Flush output to socket, also called from cio-special.
 */
void cio_put_flush(void)
{
  _cio_status();

  if (OS.dvstat[2]==0)
    {
      err=136;
    }
  else
    {
      err=siov(DEVIC_N,
	       OS.ziocb.drive,
	       'W',
	       DSTATS_WRITE,
	       &buffer_tx,
	       buffer_tx_len,
	       DTIMLO_DEFAULT,
	       buffer_tx_len,
	       0);
      
      clear_tx_buffer();
    }
}

void _cio_put(void)
{
  buffer_tx[buffer_tx_len++]=ret;
  err=1;
  
  if (buffer_tx_len==0xFF)
    cio_put_flush();
  else if ((OS.ziocb.command==IOCB_PUTREC) && (ret==0x9B))
    cio_put_flush();
  
}
