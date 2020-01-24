/**
 * CIO Close call
 */

#include <atari.h>
#include <6502.h>
#include <stddef.h> /* for NULL */
#include "sio.h"

extern unsigned char err;
extern unsigned char ret;
extern unsigned char packetlen;

void _cio_close(void)
{
  OS.dcb.ddevic=0x70; // Network adapter
  OS.dcb.dunit=1;
  OS.dcb.dcomnd=0xE4;  // sio_http_close()
  OS.dcb.dstats=0x00; // no data
  OS.dcb.dtimlo=0x1f; // Timeout
  OS.dcb.dbyt=0;
  OS.dcb.dbuf=NULL; // A packet.
  OS.dcb.daux1=0;
  siov();

  packetlen=0; // So we do a re-read.
  
  err=OS.dcb.dstats;
}
