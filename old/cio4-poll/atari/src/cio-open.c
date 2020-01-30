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

  // Start setting up DCB.
  OS.dcb.ddevic=0x70; // Network card
  OS.dcb.dunit=1;     // device unit 1
  OS.dcb.dstats=0x80; // Write connect request to peripheral.
  OS.dcb.dbuf=&packet; // Packet
  OS.dcb.dtimlo=0x1F; // Timeout
  OS.dcb.daux=0;      // no aux byte

  if (OS.ziocb.aux2==128)
    {
      // 128 = listen
      OS.dcb.dcomnd='l';
      OS.dcb.dbyt=5;
    }
  else
    {
      // Connect
      OS.dcb.dcomnd='c';
      OS.dcb.dbyt=256;      
    }
  
  siov();

  // Clear buffer
  memset(&packet,0x00,sizeof(packet));
  
  ret=err=OS.dcb.dstats;
}
