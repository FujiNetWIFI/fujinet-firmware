/**
 * Network Testing tools
 *
 * nopen - open a network connection
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

unsigned char buf[40];
unsigned char daux1=0;
unsigned char daux2=0;

void nopen(void)
{
  OS.dcb.ddevic=0x71;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd='O';
  OS.dcb.dstats=0x80;
  OS.dcb.dbuf=&buf;
  OS.dcb.dtimlo=0x0f;
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
      print("OPENED\x9b");
    }
}

void opts(char* argv[])
{
  print(argv[0]);
  print(" <devicespec> <daux1> <daux2>\x9b\x9b");
  print("Example devicespecs:\x9b");
  print("N:TCP:GOOGLE.COM:80\x9b");
  print("N:UDP:192.168.1.7:2000\x9b");
}

int main(int argc, char* argv[])
{
  char tmp[4];
  OS.lmargn=2;
  
  if (_is_cmdline_dos())
    {
      if (argc<4)
	{
	  opts(argv);
	  return(1);
	}

      strcpy(buf,argv[1]);
    }
  else
    {
      // DOS 2.0/MYDOS
      print("\x9b");
      
      print("DEVICESPEC? ");
      get_line(buf,sizeof(buf));

      print("DAUX1? ");
      get_line(buf,sizeof(tmp));
      daux1=atoi(tmp);

      print("DAUX2? ");
      get_line(buf,sizeof(tmp));
      daux2=atoi(tmp);
    }

  nopen();
  return(0);
}
