/**
 * Network Testing tools
 *
 * nread - read from network connection
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
unsigned char daux1=0;
unsigned char daux2=0;
unsigned short len;

unsigned char nread(void)
{
  OS.dcb.ddevic=0x71;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd='R';
  OS.dcb.dstats=0x40;
  OS.dcb.dbuf=&buf;
  OS.dcb.dtimlo=0x0f;
  OS.dcb.dbyt=len;
  OS.dcb.daux=len;
  siov();

  if (OS.dcb.dstats!=1)
    {
      err_sio();
      return 1;
    }
  else
    {
      print("READ:\x9b");
      printl(buf,len);
      print("\x9b");
      return 0;
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
  unsigned char ret=0;
  
  OS.lmargn=2;
  OS.dspflg=1;
  
  if (_is_cmdline_dos())
    {
      if (argc<2)
	{
	  opts(argv);
	  return(1);
	}
      len=atoi(argv[1]);
    }
  else
    {
      // DOS 2.0/MYDOS
      print("\x9b");
      
      print("LEN? ");
      get_line(tmp,sizeof(tmp));
      len=atoi(tmp);      
    }

  ret=nread();
  OS.dspflg=0;
  
  return(0);
}
