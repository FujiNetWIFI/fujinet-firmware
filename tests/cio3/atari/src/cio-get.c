/**
 * CIO Get call
 */

#include <atari.h>
#include <6502.h>
#include "sio.h"

extern unsigned char err;
extern unsigned char ret;
extern unsigned char packet[256];

unsigned char getlen;

void _cio_get_rec(void)
{
  char* p=(char *)OS.ziocb.buffer;
  unsigned char i;
  
  OS.dcb.ddevic=0x70; // Network adapter
  OS.dcb.dunit=1;
  OS.dcb.dcomnd='r';  // write tcp string
  OS.dcb.dstats=0x40; // specify a read
  OS.dcb.dtimlo=0x1f; // Timeout
  OS.dcb.dbyt=255;
  OS.dcb.dbuf=&OS.ziocb.buffer; // A packet.
  OS.dcb.daux=255;
  siov();
  err=OS.dcb.dstats;

  // Massage the data
  for (i=0;i<255;i++)
    {
      if ((p[i]==0x0A) || (p[i]==0x0D))
	{
	  p[i]=0x9B; // Make it an EOL.
	}
    }
  
  ret=p[i];
}

void _cio_get_chr(void)
{
  char* p=(char *)OS.ziocb.buffer;
  
  OS.dcb.ddevic=0x70; // Network adapter
  OS.dcb.dunit=1;
  OS.dcb.dcomnd='r';  // write tcp string
  OS.dcb.dstats=0x40; // specify a read
  OS.dcb.dtimlo=0x1f; // Timeout
  OS.dcb.dbyt=1;
  OS.dcb.dbuf=&OS.ziocb.buffer; // A packet.
  OS.dcb.daux=1;
  siov();
  err=OS.dcb.dstats;
  ret=p[0];
}

void _cio_get(void)
{
  switch(OS.ziocb.command)
    {
    case 0x05: // GETREC
      _cio_get_rec();
      break;
    case 0x07: // GETCHR
      _cio_get_chr();
      break;
    }  
}
