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
#include "sio.h"
#include "conio.h"
#include "err.h"

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
 * main
 */
int main(void)
{
  unsigned char i=0;
  
  // Read in host and device slots from FujiNet
  disk_read();

  print("\x9b");
  
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
  
  return(0);
}
