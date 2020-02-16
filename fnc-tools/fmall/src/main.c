/**
 * FujiNet Tools for CLI
 *
 * fmall - Mount all disk slots
 *
 * usage:
 *  fmall
 *
 * Author: Thomas Cherryhomes
 *  <thom.cherryhomes@gmail.com>
 *
 * Released under GPL, see COPYING
 * for details
 */

#include <atari.h>
#include <string.h>
#include <stdlib.h>
#include <peekpoke.h>
#include "sio.h"
#include "conio.h"
#include "err.h"

unsigned char buf[8];

union
{
  unsigned char host[8][32];
  unsigned char rawData[256];
} hostSlots;

union
{
  struct
  {
    unsigned char hostSlot;
    unsigned char mode;
    unsigned char file[36];
  } slot[8];
  unsigned char rawData[304];
} deviceSlots;

/**
 * Remount all disk slots
 */
void remount_all(void)
{
  unsigned char c;

  OS.dcb.ddevic=0x70;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd=0xF4; // Get host slots
  OS.dcb.dstats=0x40;
  OS.dcb.dbuf=&hostSlots.rawData;
  OS.dcb.dtimlo=0x0f;
  OS.dcb.dbyt=256;
  OS.dcb.daux=0;
  siov();

  // Read Device slots
  OS.dcb.ddevic=0x70;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd=0xF2;
  OS.dcb.dstats=0x40;
  OS.dcb.dbuf=&deviceSlots.rawData;
  OS.dcb.dtimlo=0x0f;
  OS.dcb.dbyt=sizeof(deviceSlots.rawData);
  OS.dcb.daux=0;
  siov();
  
  for (c=0;c<8;c++)
    {
      if (hostSlots.host[c][0]!=0x00)
	{
	  OS.dcb.ddevic=0x70;
	  OS.dcb.dunit=1;
	  OS.dcb.dcomnd=0xF9;
	  OS.dcb.dstats=0x00;
	  OS.dcb.dbuf=NULL;
	  OS.dcb.dtimlo=0x01;
	  OS.dcb.dbyt=0;
	  OS.dcb.daux=c;
	  siov();
	}
    }

  for (c=0;c<8;c++)
    {
      if (deviceSlots.slot[c].hostSlot!=0xFF)
	{
	  OS.dcb.ddevic=0x70;
	  OS.dcb.dunit=1;
	  OS.dcb.dcomnd=0xF8;
	  OS.dcb.dstats=0x00;
	  OS.dcb.dbuf=NULL;
	  OS.dcb.dtimlo=0x01;
	  OS.dcb.dbyt=0;
	  OS.dcb.daux1=c;
	  OS.dcb.daux2=deviceSlots.slot[c].mode;
	  siov();
	}
    }
}

/**
 * Clear up to status bar for DOS 3
 */
void dos3_clear(void)
{
  print("\x1c\x1c\x1c\x1c\x1c\x1c\x1c\x1c\x1c\x1c");
  print("\xCD\xEF\xF5\xEE\xF4\xA0\xC1\xCC\xCC\xA0\xC4\xE5\xF6\xE9\xE3\xE5\xA0\xD3\xEC\xEE\xF4\xF3\x9b\x9b"); // Mount all Device Slots
  print("\x9c\x9c\x9c\x9c\x9c\x9c\x9c\x9c\x9c\x9c");
}

/**
 * main
 */
int main(void)
{

  OS.lmargn=2;
  
  print("\x9b");

  if (PEEK(0x718)==53)
    dos3_clear();

  print("MOUNTING ALL DEVICE SLOTS...");
  remount_all();

  if (OS.dcb.dstats==1)
    {
      print("OK");
    }
  else
    {
      print("ERROR");
    }
  
  if (!_is_cmdline_dos())
    {
      print("\x9bPRESS \xA0\xD2\xC5\xD4\xD5\xD2\xCE\xA0 TO CONTINUE.\x9b");
      get_line(buf,sizeof(buf));
    }
  
  return(0);
}
