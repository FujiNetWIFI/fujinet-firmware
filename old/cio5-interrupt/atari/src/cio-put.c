/**
 * CIO Put call
 */

#include <atari.h>
#include <6502.h>
#include <string.h>
#include "sio.h"

extern unsigned char err;
extern unsigned char ret;
extern unsigned char packet[256];

unsigned char putlen;

void _cio_put_flush(void)
{
  OS.dcb.ddevic=0x70; // Network adapter
  OS.dcb.dunit=1;
  OS.dcb.dcomnd='w';  // write tcp string
  OS.dcb.dstats=0x80; // specify a write
  OS.dcb.dtimlo=0x1f; // Timeout
  OS.dcb.dbyt=putlen;
  OS.dcb.dbuf=&packet; // A packet.
  OS.dcb.daux1=putlen;
  siov();
  err=OS.dcb.dstats;
  putlen=0;
}


void _cio_put(void)
{
  if (ret!=0x9b)
    packet[putlen++]=ret;
  else
    {
      packet[putlen++]=ret;
      _cio_put_flush();
      memset(&packet,0,sizeof(packet));
    }
  err=1;
}
