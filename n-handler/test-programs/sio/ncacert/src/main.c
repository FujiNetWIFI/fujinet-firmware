/**
 * Network Testing tools
 *
 * ncacert - toggle headers on/off
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

unsigned char daux1=0;

void ncacert(void)
{
  OS.dcb.ddevic=0x71;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd='H';
  OS.dcb.dstats=0x00;
  OS.dcb.dbuf=NULL;
  OS.dcb.dtimlo=0x0f;
  OS.dcb.dbyt=0;
  OS.dcb.daux=daux1;
  siov();

  if (OS.dcb.dstats!=1)
    {
      err_sio();
      exit(OS.dcb.dstats);
    }
  else
    {
      print("HEADER TOGGLE: ");
      if (daux1==1)
	print("ENABLED\x9b");
      else
	print("DISABLED\x9b");
    }
}

void opts(char* argv[])
{
  print(argv[0]);
  print(" <len>\x9b\x9b");
}

int main(int argc, char* argv[])
{
  char tmp[4];
  OS.lmargn=2;
  
  if (_is_cmdline_dos())
    {
      if (argc<2)
	{
	  opts(argv);
	  return(1);
	}
      daux1=atoi(argv[1]);
    }
  else
    {
      // DOS 2.0/MYDOS
      print("\x9b");
      
      print("HEADER TOGGLE (0=DISABLE/1=ENABLE)? ");
      get_line(tmp,sizeof(tmp));
      daux1=atoi(tmp);      
    }

  ncacert();
  return(0);
}
