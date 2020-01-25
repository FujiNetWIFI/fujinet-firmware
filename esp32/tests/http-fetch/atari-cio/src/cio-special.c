/**
 * CIO Special (XIO) call
 */

#include <atari.h>
#include <6502.h>
#include <string.h>
#include "sio.h"

extern unsigned char err;
extern unsigned char ret;
extern unsigned char packet[256];

void _cio_special(void)
{
  char *p=(char *)OS.ziocb.buffer;
  unsigned char i;
  
  // scoot buffer past the N:
  p+=2;
  
  // copy into packet
  strcpy(packet,p);

  // Remove EOL
  for (i=0;i<strlen(packet);i++)
    packet[i]=(packet[i]==0x9B ? 0x00 : packet[i]);
  
  // Set up common bits of DCB
  OS.dcb.ddevic=0x70;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd=0xE3;
  OS.dcb.dstats=0x80; // Write by default.
  OS.dcb.dtimlo=0x1F;
  OS.dcb.dbuf=&packet;
  OS.dcb.dbyt=80;
  OS.dcb.daux1=OS.ziocb.aux1;
  OS.dcb.daux2=OS.ziocb.aux2;

  siov();
  err=OS.dcb.dstats;
}
