/**
 * Network Testing tools
 *
 * naccept - accept a pending listening connection.
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

void naccept(void)
{
  OS.dcb.ddevic=0x71;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd='A';
  OS.dcb.dstats=0x00;
  OS.dcb.dbuf=NULL;
  OS.dcb.dtimlo=0x0f;
  OS.dcb.dbyt=0;
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
      print("ACCEPTED\x9b");
    }
}

void main(void)
{
  OS.lmargn=2;
  
  naccept();
}
