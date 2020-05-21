/**
 * Network Testing tools
 *
 * ncd - set N: prefix.
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
unsigned char i=0;

void ncd(unsigned char unit)
{
  OS.dcb.ddevic=0x71;
  OS.dcb.dunit=unit;
  OS.dcb.dcomnd=0x2C;
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
}

int main(int argc, char* argv[])
{
  unsigned char tmp[2]={0,0};
  unsigned char u=1;
  
  OS.lmargn=2;
  
  if (_is_cmdline_dos())
    {
      if (argc<2)
	goto interactive;
      else
	{
	  for (i=1;i<=argc;i++)
	    {
	      strcat(buf,argv[i]);
	      if (i<argc-1)
		strcat(buf," ");
	    }
	}
    }
  else
    {
    interactive:
      // DOS 2.0/MYDOS
      print("\x9b");
      print("ENTER PREFIX OR \xD2\xC5\xD4\xD5\xD2\xCE TO CLEAR\x9b");
      get_line(buf,240);
    }

  // if no device, set a device path.
  if ((buf[1]!=':') && (buf[2]!=':'))
    {
      memmove(&buf[2],&buf[0],sizeof(buf)-3);
      buf[0]='N';
      buf[1]=':';
    }
  else if (buf[2]==':')
    u=buf[1]-0x30;

  ncd(u);
  
  return(0);
}
