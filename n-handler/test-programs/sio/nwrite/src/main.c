/**
 * Network Testing tools
 *
 * nwrite - write to network connection
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

void nwrite(void)
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
  else
    {
      print("WRITTEN\x9b");
    }
}

void main(void)
{
  OS.lmargn=2;

  print("ENTER DATA FOLLOWED BY \xD2\xC4\xD3\xD4\xD2\xCE\x9b");
  get_line(buf,255);

  nwrite();
}
