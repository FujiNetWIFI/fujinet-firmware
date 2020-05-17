/**
 * Network Testing tools
 *
 * nstest - open a network connection
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

unsigned char buf[256]="N:TCP://TMA-1:2000/\x9b";
unsigned char d0[4], d1[4], d2[4], d3[4];
unsigned char trip=0;

extern void prcd(void);

void nsopen(void)
{
  OS.dcb.ddevic=0x71;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd='O';
  OS.dcb.dstats=0x80;
  OS.dcb.dbuf=&buf;
  OS.dcb.dtimlo=0x0f;
  OS.dcb.dbyt=256;
  OS.dcb.daux1=12;
  OS.dcb.daux2=2;
  siov();

  if (OS.dcb.dstats!=1)
    {
      err_sio();
      exit(OS.dcb.dstats);
    }
}

void nstest(void)
{
  if (trip==1)
    {
      OS.dcb.ddevic=0x71;
      OS.dcb.dunit=1;
      OS.dcb.dcomnd='S';
      OS.dcb.dstats=0x40;
      OS.dcb.dbuf=&OS.dvstat;
      OS.dcb.dtimlo=0x0f;
      OS.dcb.dbyt=4;
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
	  itoa(OS.dvstat[0],d0,16);
	  itoa(OS.dvstat[1],d1,16);
	  itoa(OS.dvstat[2],d2,16);
	  itoa(OS.dvstat[3],d3,16);
	  print(d0);
	  print(" ");
	  print(d1);
	  print(" ");
	  print(d2);
	  print(" ");
	  print(d3);
      print("\x9b");
	}
      trip=0;
    }
}

void main(void)
{
  OS.lmargn=2;
  OS.ch=0xFF;
  OS.vprced=prcd;
  
  nsopen();

  PIA.pactl |= 1;
  
  while(OS.ch==0xFF)
    {
      nstest();
    }

  PIA.pactl |= 0;
}
