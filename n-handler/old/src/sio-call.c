/**
 * C wrapper for SIOV
 */

#include <atari.h>
#include "sio.h"

unsigned char siov(unsigned char ddevic,
		   unsigned char dunit,
		   unsigned char dcomnd,
		   unsigned char dstats,
		   void *dbuf,
		   unsigned short dbyt,
		   unsigned char dtimlo,
		   unsigned char daux1,
		   unsigned char daux2)
{
  OS.dcb.ddevic=ddevic;
  OS.dcb.dunit=dunit;
  OS.dcb.dcomnd=dcomnd;
  OS.dcb.dstats=dstats;
  OS.dcb.dbuf=dbuf;
  OS.dcb.dbyt=dbyt;
  OS.dcb.dtimlo=dtimlo;
  OS.dcb.daux1=daux1;
  OS.dcb.daux2=daux2;
  _siov();
  
  return OS.dcb.dstats;
}
