/**
 * Network Testing tools
 *
 * nstatus - return network connection status
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

void nstatus(void)
{
  unsigned char i;
  unsigned char tmp[3];
  
  OS.dcb.ddevic=0x71;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd='S';
  OS.dcb.dstats=0x40;
  OS.dcb.dbuf=OS.dvstat;
  OS.dcb.dtimlo=0x0f;
  OS.dcb.dbyt=4;
  OS.dcb.daux1=0;
  OS.dcb.daux2=0;
  siov();

  if (OS.dcb.dstats!=1)
    {
      err_sio();
      exit(OS.dcb.dstats);
    }
  else
    {
      print("\x9b" "STATUS: ");
      for (i=0;i<4;i++)
	{
	  itoa(OS.dvstat[i],tmp,16);
	  print(tmp);
	  print(" ");
	}
      print("\x9bNET STATUS: ");
      err_net();
      
      print("\x9b");
    }
}

void main(void)
{
  OS.lmargn=2;
  nstatus();
}
