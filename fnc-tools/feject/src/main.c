/**
 * FujiNet Tools for CLI
 *
 * feject - Eject disk in device slot
 *
 * usage:
 *  feject <ds#>
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
  char host[8][32];
  unsigned char rawData[256];
} hostSlots;

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
 * Read host slots
 */
void host_read(void)
{
  // Query for host slots
  OS.dcb.ddevic=0x70;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd=0xF4; // Get host slots
  OS.dcb.dstats=0x40;
  OS.dcb.dbuf=&hostSlots.rawData;
  OS.dcb.dtimlo=0x0f;
  OS.dcb.dbyt=256;
  OS.dcb.daux=0;
  siov();

  if (OS.dcb.dstats!=1)
    {
      err_sio();
      exit(OS.dcb.dstats);
    }
}

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
 * Mount device slot
 */
void disk_umount(unsigned char c)
{
  OS.dcb.ddevic=0x70;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd=0xE9;
  OS.dcb.dstats=0x00;
  OS.dcb.dbuf=NULL;
  OS.dcb.dtimlo=0x01;
  OS.dcb.dbyt=0;
  OS.dcb.daux1=c;
  OS.dcb.daux2=0;
  siov();
  if (OS.dcb.dstats!=1)
    {
      err_sio();
      exit(OS.dcb.dstats);
    }
}


/**
 * Write device slots
 */
void disk_write(void)
{
  OS.dcb.ddevic=0x70;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd=0xF1;
  OS.dcb.dstats=0x80;
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
 * show options
 */
void opts(char* argv[])
{
  print(argv[0]);
  print(" <ds#>\x9b\x9b");
  print("<ds#> - device slot (1-8)\x9b");
}

/**
 * Clear up to status bar for DOS 3
 */
void dos3_clear(void)
{
  print("\x1c\x1c\x1c\x1c\x1c\x1c\x1c\x1c\x1c\x1c");
  print("\xC5\xea\xe5\xe3\xf4\xa0\xc4\xe9\xf3\xeb\xa0\xc9\xec\xe1\xe7\xe5\x9b\x9b"); // Eject Disk Image
  print("\x9c\x9c\x9c\x9c\x9c\x9c\x9c\x9c\x9c\x9c");
}

/**
 * main
 */
int main(int argc, char* argv[])
{
  unsigned char s=argv[1][0]-0x30;

  OS.lmargn=2;
  
  if (_is_cmdline_dos())
    {
      if (argc<2)
	{
	  opts(argv);
	  return(1);
	}
    }
  else
    {
      // DOS 2.0/MYDOS
      print("\x9b");

      if (PEEK(0x718)==53)
	dos3_clear();
      
      print("EJECT FROM WHICH DEVICE SLOT? ");
      get_line(buf,sizeof(buf));
      s=buf[0]-0x30;
    }
  
  if (s<1 || s>8)
    {
      print("INVALID SLOT NUMBER.\x9b");
      return(1);
    }
  
  s-=1;
  
  // Read in host and device slots from FujiNet
  disk_read();

  if (deviceSlots.slot[s].hostSlot!=0xFF)
    {
      // unmount disk
      disk_umount(s);
      
      // mark disk as unmounted in deviceSlots
      deviceSlots.slot[s].hostSlot=0xFF;
      memset(deviceSlots.slot[s].file,0,sizeof(deviceSlots.slot[s].file));
      
      // write the deviceSlots back to FujiNet
      disk_write();
      
      s+=0x31;
      
      print("Disk D");
      printc(&s);
      print(": ejected.\x9b");
    }
  else
    {
      print("Disk D");
      s+=0x31;
      printc(&s);
      print(": not in use.\x9b");
    }

  if (!_is_cmdline_dos())
    {
      print("\x9bPRESS \xA0\xD2\xC5\xD4\xD5\xD2\xCE\xA0 TO CONTINUE.\x9b");
      get_line(buf,sizeof(buf));
    }
  
  return(0);
}
