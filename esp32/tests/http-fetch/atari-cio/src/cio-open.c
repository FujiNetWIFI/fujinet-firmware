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
  char i;
  
  // remove EOL
  for (i=0;i<OS.ziocb.buflen;i++)
    if (p[i]==0x9B)
      p[i]=0x00;

  // Scoot buffer past the N:
  p+=2;

  // Copy into packet
  strcpy(packet,p);

  // Start setting up DCB.
  OS.dcb.ddevic=0x70; // Network card
  OS.dcb.dcomnd=0xE6;
  OS.dcb.dunit=1;     // device unit 1
  OS.dcb.dstats=0x80; // Write URL to peripheral
  OS.dcb.dbuf=&packet; // Packet
  OS.dcb.dbyt=80;     // 80 bytes
  OS.dcb.dtimlo=0x1F; // Timeout
  OS.dcb.daux=0;      // no aux byte
  
  siov();

  // Clear buffer
  memset(&packet,0x00,sizeof(packet));
  
  ret=err=OS.dcb.dstats;
}
