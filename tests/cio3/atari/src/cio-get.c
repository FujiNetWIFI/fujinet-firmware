/**
 * CIO Get call
 */

#include <atari.h>
#include <6502.h>
#include "sio.h"

extern unsigned char err;
extern unsigned char ret;

void _cio_get(void)
{
  OS.dcb.ddevic=0x70; // Network adapter
  OS.dcb.dunit=1;
  OS.dcb.dcomnd='r';  // write tcp string
  OS.dcb.dstats=0x40; // specify a read
  OS.dcb.dtimlo=0x1f; // Timeout
  OS.dcb.dbyt=OS.ziocb.buflen;
  OS.dcb.dbuf=&OS.ziocb.buffer; // A packet.
  OS.dcb.daux1=OS.ziocb.buflen;
  siov();
  err=OS.dcb.dstats;
}
