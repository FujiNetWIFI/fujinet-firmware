/**
 * CIO Special (XIO) call
 */

#include <atari.h>
#include <6502.h>
#include <string.h>
#include <peekpoke.h>
#include "sio.h"
#include "common.h"

extern unsigned char err;
extern unsigned char ret;
extern unsigned char packet[512];

static unsigned char* buf;

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
    {
      packet[i]=(packet[i]==0x9B ? 0x00 : packet[i]);
    }
  
  // Set up common bits of DCB
  OS.dcb.ddevic=0x70;
  OS.dcb.dunit=1;
  OS.dcb.dstats=0x40; // Read by default.
  OS.dcb.dtimlo=0x1F;
  OS.dcb.dbuf=&packet;
  OS.dcb.dbyt=256;
  OS.dcb.daux1=OS.ziocb.aux1;
  OS.dcb.daux2=OS.ziocb.aux2;
  
  switch(OS.ziocb.command)
    {
    case 'B': // UDP Begin
      OS.dcb.dcomnd=0xD0;
      OS.dcb.dstats=0x00;
      OS.dcb.dbyt=0;
      OS.dcb.dbuf=NULL;
      break;
    case 'D': // UDP Destination Address
      OS.dcb.dcomnd=0xD4;
      OS.dcb.dstats=0x80;
      OS.dcb.dbyt=64;
      break;
    case 'R': // UDP Read
      OS.dcb.dcomnd=0xD2;
      OS.dcb.dbuf=buf;
      OS.dcb.dbyt=aux12_to_aux(OS.ziocb.aux1,OS.ziocb.aux2);
      break;
    case 'S': // UDP Status
    case 0x0D: // Canonical CIO status cmd.
      OS.dcb.dcomnd=0xD1;
      OS.dcb.dbuf=&OS.dvstat;
      OS.dcb.dbyt=4;
      break;
    case 'U': // UDP Set Buffer
      OS.dcb.dstats=0x01; // No error return.
      buf=(unsigned char *)0x0600;
      goto no_sio;
      break;
    case 'W': // UDP Write
      OS.dcb.dcomnd=0xD3;
      OS.dcb.dstats=0x80;
      OS.dcb.dbuf=buf;
      OS.dcb.dbyt=aux12_to_aux(OS.ziocb.aux1,OS.ziocb.aux2);
      break;
    }
  siov();
 no_sio:
  err=OS.dcb.dstats;
}
