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

  // remove EOL
  p[OS.ziocb.buflen-1]=0x00;

  // scoot buffer past the N:
  p+=2;

  // copy into packet
  strcpy(packet,p);

  // Set up common bits of DCB
  OS.dcb.ddevic=0x70;
  OS.dcb.dunit=1;
  OS.dcb.dtimlo=0x1F;
  OS.dcb.dbuf=&packet;
  OS.dcb.daux=0;
  
  switch(OS.ziocb.command)
    {
    case 'a':
      OS.dcb.dstats=0x40;
      OS.dcb.dbyt=1;
      OS.dcb.dcomnd='a';
      OS.dcb.dbuf=&OS.dvstat;
      break;
    }
  siov();
  err=OS.dcb.dstats;
}
