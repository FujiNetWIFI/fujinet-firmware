/**
 * CIO Get call
 */

#include <atari.h>
#include <6502.h>
#include <stdbool.h>
#include "sio.h"

extern unsigned char err;
extern unsigned char ret;
extern unsigned char packet[256];
extern long filesize;

extern void _cio_status(void);

unsigned char packetlen;
unsigned char* p;
unsigned char done=false;

void _cio_get_chr(void)
{
  if (done==true)
    {
      err=136;
      ret=0;
    } 
  else if (filesize==0)
    {
      err=3; // EOF
      done=true;
      ret=0;
    }
  else
    {
      err=1; 
      ret=*p++;
      packetlen--;
      filesize--;
    }
}

void _cio_get(void)
{

  // If buffer is empty, get next buffer from esp

  if (packetlen==0)
    {
      packetlen=256;
      OS.dcb.ddevic=0x70;
      OS.dcb.dunit=1;
      OS.dcb.dcomnd=0xE5;
      OS.dcb.dstats=0x40;
      OS.dcb.dtimlo=0x1f;
      OS.dcb.dbyt=256;
      OS.dcb.dbuf=&packet;
      OS.dcb.daux=0;
      siov();
      err=OS.dcb.dstats;
      p=&packet[0];
    }
  
  _cio_get_chr();  
}
