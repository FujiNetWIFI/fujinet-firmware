/**
 * Network Testing tools
 *
 * ncopy - copy files to/from N:
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
#include "nsio.h"

void nopen(unsigned char unit, char* buf, unsigned char aux1)
{
  OS.dcb.ddevic=0x71;
  OS.dcb.dunit=unit;
  OS.dcb.dcomnd='O';
  OS.dcb.dstats=0x80;
  OS.dcb.dbuf=buf;
  OS.dcb.dtimlo=0x1f;
  OS.dcb.dbyt=256;
  OS.dcb.daux1=aux1;
  siov();
}

void nclose(unsigned char unit)
{
  OS.dcb.ddevic=0x71;
  OS.dcb.dunit=unit;
  OS.dcb.dcomnd='C';
  OS.dcb.dstats=0x00;
  OS.dcb.dbuf=NULL;
  OS.dcb.dtimlo=0x1f;
  OS.dcb.dbyt=0;
  OS.dcb.daux1=0;
  OS.dcb.daux2=0;
  siov();
}

void nread(unsigned char unit, char* buf, unsigned short len)
{
  OS.dcb.ddevic=0x71;
  OS.dcb.dunit=unit;
  OS.dcb.dcomnd='R';
  OS.dcb.dstats=0x40;
  OS.dcb.dbuf=buf;
  OS.dcb.dtimlo=0x1f;
  OS.dcb.dbyt=len;
  OS.dcb.daux=len;
  siov();
}

void nwrite(unsigned char unit, char* buf, unsigned short len)
{
  OS.dcb.ddevic=0x71;
  OS.dcb.dunit=unit;
  OS.dcb.dcomnd='W';
  OS.dcb.dstats=0x80;
  OS.dcb.dbuf=buf;
  OS.dcb.dtimlo=0x1f;
  OS.dcb.dbyt=len;
  OS.dcb.daux=len;
  siov();
}

void nstatus(unsigned char unit)
{
  OS.dcb.ddevic=0x71;
  OS.dcb.dunit=unit;
  OS.dcb.dcomnd='S';
  OS.dcb.dstats=0x40;
  OS.dcb.dbuf=OS.dvstat;
  OS.dcb.dtimlo=0x1f;
  OS.dcb.dbyt=4;
  OS.dcb.daux1=0;
  OS.dcb.daux2=0;
  siov();
}
