/**
 * CIO Open call
 */

#include <atari.h>
#include <6502.h>
#include <string.h>
#include "sio.h"

extern unsigned char err;
extern unsigned char ret;
extern unsigned char packet[256];

void _cio_open(void)
{
  char *p=(char *)OS.ziocb.buffer;

  // remove EOL
  p[OS.ziocb.buflen-1]=0x00;

  // Scoot buffer past the N:
  p+=2;

  // Copy into packet
  strcpy(packet,p);
  
  // I am ignoring the aux1/aux2 for this test, and simply assuming the open parameters.

  OS.dcb.ddevic=0x70; // Network card
  OS.dcb.dunit=1;     // device unit 1
  OS.dcb.dcomnd='c';  // Do a connect
  OS.dcb.dstats=0x80; // Write connect request to peripheral.
  OS.dcb.dbuf=&packet; // Packet
  OS.dcb.dbyt=256;     // packet size
  OS.dcb.dtimlo=0x1F; // Timeout
  OS.dcb.daux=0;      // no aux byte
  siov();
  
  err=OS.dcb.dstats;
}
