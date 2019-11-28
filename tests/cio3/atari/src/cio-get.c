/**
 * CIO Get call
 */

#include <atari.h>
#include <6502.h>
#include "sio.h"

extern unsigned char err;
extern unsigned char ret;
extern unsigned char packet[256];

extern void _cio_status(void);

unsigned char getlen;
unsigned char* p;

void _cio_get_chr(void)
{ 
  if (getlen>0)
    {
      ret=*p++;
      getlen--;
    }
  else
    {
      err=136; // EOF
      ret=0;
    }
}

void _cio_get_rec(void)
{
  err=1;  
  
  _cio_get_chr();

  if (err==136)
    return;
  
  if (ret==0x0D)
    {
      *p++;
      getlen--;
      ret=0x9B; // turn into EOL
    }
}

void _cio_get(void)
{

  // If buffer is empty, get next buffer from esp
  
  if (getlen==0)
    {
      _cio_status(); // Returns available bytes in DVSTAT and DVSTAT+1
      getlen=OS.dvstat[0];

      if (getlen>0)
	{
	  OS.dcb.ddevic=0x70;
	  OS.dcb.dunit=1;
	  OS.dcb.dcomnd='r';
	  OS.dcb.dstats=0x40;
	  OS.dcb.dtimlo=0x1f;
	  OS.dcb.dbyt=getlen;
	  OS.dcb.dbuf=&packet;
	  OS.dcb.daux=getlen;
	  siov();
	  err=OS.dcb.dstats;
	  p=&packet[0];
	}
    }
  
  switch(OS.ziocb.command)
    {
    case 0x05: // GETREC
      _cio_get_rec();
      break;
    case 0x07: // GETCHR
      _cio_get_chr();
      break;
    }  

  OS.color2=getlen;

}
