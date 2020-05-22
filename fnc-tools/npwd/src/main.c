/**
 * Network Testing tools
 *
 * npwd - show N: prefix
 *
 * Author: Thomas Cherryhomes
 *  <thom.cherryhomes@gmail.com>
 *
 * Released under GPL 3.0
 * See COPYING for details.
 */

#include <atari.h>
#include <string.h>
#include <stdlib.h>
#include <peekpoke.h>
#include "sio.h"
#include "conio.h"
#include "err.h"

unsigned char buf[256];
unsigned char unit;

void npwd(void)
{
  OS.dcb.ddevic=0x71;
  OS.dcb.dunit=unit;
  OS.dcb.dcomnd=0x30;
  OS.dcb.dstats=0x40;
  OS.dcb.dbuf=&buf;
  OS.dcb.dtimlo=0x1f;
  OS.dcb.dbyt=256;
  OS.dcb.daux1=0;
  OS.dcb.daux2=0;
  siov();

  if (OS.dcb.dstats!=1)
    {
      err_sio();
      exit(OS.dcb.dstats);
    }
}

int main(int argc, char* argv[])
{  
  OS.lmargn=2;

  if (argc<2)
    unit=1;
  else if (argv[1][1]==':')
    unit=1;
  else if ((argv[1][1]>'0') || (argv[1][1]<'9'))
    unit=argv[1][1]-0x30;
  else
    unit=1;
  
  npwd();
  
  if (buf[0]==0x00)
    print("NO PREFIX SET.\x9b");
  else
    print(buf);

  print("\x9b");
  
  return(0);
}
