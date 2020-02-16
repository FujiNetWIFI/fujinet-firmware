/**
 * FujiNet Tools for CLI
 *
 * fld - list disk slots
 *
 * usage:
 *  fld
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

unsigned char buf[40];

union
{
  struct
  {
    unsigned char hostSlot;
    unsigned char mode;
    char file[36];
  } slot[8];
  unsigned char rawData[304];
} deviceSlots;

/**
 * Read Device Slots
 */
void disk_read(void)
{
  // Read Drive Tables
  OS.dcb.ddevic=0x70;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd=0xF2;
  OS.dcb.dstats=0x40;
  OS.dcb.dbuf=&deviceSlots.rawData;
  OS.dcb.dtimlo=0x0f;
  OS.dcb.dbyt=sizeof(deviceSlots.rawData);
  OS.dcb.daux=0;
  siov();

  if (OS.dcb.dstats!=1)
    {
      err_sio();
      exit(OS.dcb.dstats);
    }
}

/**
 * Clear up to status bar for DOS 3
 */
void dos3_clear(void)
{
  print("\x1c\x1c\x1c\x1c\x1c\x1c\x1c\x1c\x1c\x1c");
  print("\xCC\xE9\xF3\XF4\xA0\xC4\xEE\xF6\xE9\xE3\xE5\xA0\xD3\xec\xef\xf4\xf3\x9b\x9b"); // List Device Slots
  print("\x9c\x9c\x9c\x9c\x9c\x9c\x9c\x9c\x9c\x9c");
}

/**
 * main
 */
int main(void)
{
  unsigned char i=0;

  OS.lmargn=2;
  
  // Read in host and device slots from FujiNet
  disk_read();

  print("\x9b");

  if (PEEK(0x718)==53)
    dos3_clear();
  
  for (i=0;i<8;i++)
    {
      unsigned char n=i+0x31;
      unsigned char hs=deviceSlots.slot[i].hostSlot+0x31;
      unsigned char m=(deviceSlots.slot[i].mode==0x03 ? 'W' : 'R');

      if (deviceSlots.slot[i].hostSlot!=0xFF)
	{
	  print("D");
	  printc(&n);
	  print(": ");
	  print("(");
	  printc(&hs);
	  print(") ");
	  print("(");
	  printc(&m);
	  print(") ");
	  print(deviceSlots.slot[i].file);
	  print("\x9b");
	}
      else
	{
	  print("D");
	  printc(&n);
	  print(": ");
	  print("Empty\x9b");
	}
    }

  if (!_is_cmdline_dos())
    {
      print("\x9bPRESS \xA0\xD2\xC5\xD4\xD5\xD2\xCE\xA0 TO CONTINUE.\x9b");
      get_line(buf,sizeof(buf));
    }
  
  return(0);
}
