/**
 * Diagnostic tools
 *
 * audio - tell ESP to send audio test
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

void audio(unsigned char daux1)
{
  // Enable motor.
  PIA.pactl=(daux1==1 ? 52 : 60);
  
  OS.dcb.ddevic=0x70;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd=(daux1==1 ? 'A' : 'B');
  OS.dcb.dstats=0x00;
  OS.dcb.dbuf=NULL;
  OS.dcb.dtimlo=0x0f;
  OS.dcb.dbyt=0;
  OS.dcb.daux1=daux1;
  OS.dcb.daux2=0;
  siov();

  if (OS.dcb.dstats!=1)
    {
      err_sio();
      exit(OS.dcb.dstats);
    }
  else
    {
      if (daux1==0)
	print("AUDIO OFF.\x9b");
      else
	print("AUDIO ON.\x9b");
    }
}

int main(int argc, char* argv[])
{
  unsigned char daux1;

  if (_is_cmdline_dos())
    {
      if (argc==1)
	{
	  print(argv[0]);
	  print(" <0|1>\x9b");
	  return(1);
	}
      else
	daux1=atoi(argv[1]);
    }
  else
    {
      // Dos 2
      print("Turn 0=OFF 1=ON? ");
      get_line(buf,40);
      daux1=atoi(buf);
    }

  audio(daux1);
  
  return(0);
}
