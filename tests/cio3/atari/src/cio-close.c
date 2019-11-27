/**
 * CIO Close call
 */

#include <atari.h>
#include <6502.h>
#include "sio.h"

extern unsigned char err;
extern unsigned char ret;
extern unsigned char packet[256];

void _cio_close(void)
{
  OS.dcb.ddevic=0x70; // Network adapter
  OS.dcb.dunit=1;
  OS.dcb.dcomnd='d';  // disconnect
  OS.dcb.dstats=0x80; // specify a write
  OS.dcb.dtimlo=0x1f; // Timeout
  OS.dcb.dbyt=1;
  OS.dcb.dbuf=&packet; // A packet.
  OS.dcb.daux1=0;
  siov();
  err=OS.dcb.dstats;
}
