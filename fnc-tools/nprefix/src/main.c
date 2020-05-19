/**
 * Network Testing tools
 *
 * nprefix - set N: prefix.
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

void nprefix(void)
{
  OS.dcb.ddevic=0x71;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd=0xFE;
  OS.dcb.dstats=0x80;
  OS.dcb.dbuf=&buf;
  OS.dcb.dtimlo=0x1f;
  OS.dcb.dbyt=256;
  OS.dcb.daux1=daux1;
  OS.dcb.daux2=daux2;
  siov();

  if (OS.dcb.dstats!=1)
    {
      err_sio();
      exit(OS.dcb.dstats);
    }
  else
    {
      print("N: = ");
      print(buf);
      print("\x9b");
    }
}

int main(int argc, char* argv[])
{
  OS.lmargn=2;
  
  if (_is_cmdline_dos())
    {
      if (argc<2)
	goto interactive;
      else
	strcpy(buf,argv[1]);
    }
  else
    {
    interactive:
      // DOS 2.0/MYDOS
      print("\x9b");
      print("");
      print("ENTER PREFIX OR \xD2\xC5\xD4\xD5\xD2\xCE TO CLEAR\x9b");
      get_line(buf,240);
    }

  nprefix();
  return(0);
}
