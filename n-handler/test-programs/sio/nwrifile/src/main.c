/**
 * Network Testing tools
 *
 * nwrifile - write to network connection
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

unsigned char buf[128];
unsigned char daux1=0;
unsigned char daux2=0;

void nwrifile(unsigned char* buf)
{
  OS.dcb.ddevic=0x71;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd='W';
  OS.dcb.dstats=0x80;
  OS.dcb.dbuf=&buf;
  OS.dcb.dtimlo=0x0f;
  OS.dcb.dbyt=strlen(buf);
  OS.dcb.daux=strlen(buf);
  siov();

  if (OS.dcb.dstats!=1)
    {
      err_sio();
      exit(OS.dcb.dstats);
    }
}

int main(int argc, char* argv[])
{
  unsigned char err=1;
  unsigned char i;
  
  OS.lmargn=2;

  if (_is_cmdline_dos())
    {
      // CLI DOS
      if (argc<2)
	{
	  args();
	  exit(1);
	}

      strcpy(buf,argv[1]);
    }
  else
    {
      // DOS 2.0
      print("\x9b");
      print("ENTER DEV:FILENAME OR \xD2\xC5\xD4\xD5\xD2\xCE TO ABORT\x9b");
      get_line(buf,128);
    }

  // OPEN File
  OS.iocb[1].command=IOCB_OPEN;
  OS.iocb[1].buffer=buf;
  OS.iocb[1].buflen=strlen(buf);
  OS.iocb[1].aux1=4;
  OS.iocb[1].aux2=
    OS.iocb[1].aux3=
    OS.iocb[1].aux4=
    OS.iocb[1].aux5=
    OS.iocb[1].spare=0;
  err=ciov(1); 

  if (err!=1)
    {
      print("COULD NOT OPEN FILE. ABORTING.\x9b");
      exit(1);
    }

  // READ data
  OS.iocb[1].command=IOCB_GETREC;
  OS.iocb[1].buflen=sizeof(buf);
  
  do
    {
      memset(buf,0,sizeof(buf));
      err=ciov();
      nwrifile(buf);
    } while (err!=0x88); // 0x88 = EOF

  print("\x9b" "DATA WRITTEN.\x9b");
  return(0);
}
