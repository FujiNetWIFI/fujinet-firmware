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
 * Flush output to socket, also called from cio-special.
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
  if (OS.dvstat[2]==0)
    err=136;
  else
    buffer_tx[buffer_tx_len++]=ret;

  // Note to Stefan D: Initially I checked for PUTREC, here, and used it as an
  // opportunity to flush the put buffer if an EOL was encountered.
  // However, BASIC DOES NOT USE PUTREC when doing a PRINT #iocb, and it is expected
  // that if we do a PRINT #1; then it shows up on the other end, simplifying things
  // considerably.
  //
  // As always, if I am completely and utterly wrong, feel free to thwap me on the head
  // and correct me. :)
  
  if ((ret==0x9B) || (buffer_tx_len==0xFF))
    cio_put_flush();
  
}
