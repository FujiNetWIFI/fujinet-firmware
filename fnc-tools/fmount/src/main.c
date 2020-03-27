/**
 * FujiNet Tools for CLI
 *
 * fmount - Mount Disk Image
 *
 * usage:
 *  feject <ds#> <hs#> <R|W> <FNAME>
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
 * Mount a host slot
 */
void host_mount(unsigned char c)
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
      if (OS.dcb.dstats!=1)
	{
	  err_sio();
	  exit(OS.dcb.dstats);
	}
    }
}

/**
 * Mount device slot
 */
void disk_mount(unsigned char c, unsigned char o)
{
  OS.dcb.ddevic=0x70;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd=0xF8;
  OS.dcb.dstats=0x00;
  OS.dcb.dbuf=NULL;
  OS.dcb.dtimlo=0x01;
  OS.dcb.dbyt=0;
  OS.dcb.daux1=c;
  OS.dcb.daux2=o;
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
  print(" <DS#> <HS#> <R|W> <FNAME>\x9b\x9b");
  print("<DS#>   - device slot (1-8)\x9b");
  print("<HS#>   - host slot (1-8)\x9b");
  print("<R|W>   - Read / Write\x9b");
  print("<FNAME> - filename\x9b");
}

/**
 * main
 */
int main(int argc, char* argv[])
{
  unsigned char ds=argv[1][0]-0x30;
  unsigned char hs=argv[2][0]-0x30;
  unsigned char dsa=argv[1][0];
  unsigned char hsa=argv[2][0];
  unsigned char o=(argv[3][0]=='W' ? 0x02 : 0x01);

  OS.lmargn=2;
  
  if (_is_cmdline_dos())
    {
      if (argc<5)
	{
	  opts(argv);
	  return(1);
	}
      strcpy(buf,argv[4]);
    }
  else
    {
      // DOS 2.0
      print("\x9b");

      print("DEVICE SLOT (1-8)? ");
      get_line(buf,sizeof(buf));
      ds=buf[0]-0x30;
      dsa=buf[0];

      print("HOST SLOT (1-8)? ");
      get_line(buf,sizeof(buf));
      hs=buf[0]-0x30;
      hsa=buf[0];

      print("READ / WRITE (R/W)? ");
      get_line(buf,sizeof(buf));

      if (buf[0]=='w')
	buf[0]=='W';
      
      o=(buf[0]=='W' ? 0x02 : 0x01);

      print("FILENAME:\x9b");
      get_line(buf,sizeof(buf));
    }
  if (ds<1 || ds>8)
    {
      print("INVALID DRIVE SLOT NUMBER.\x9b");
      return(1);
    }

  if (hs<1 || hs>8)
    {
      print("INVALID HOST SLOT NUMBER.\x9b");
      return(1);
    }
  
  hs-=1;
  ds-=1;

  // Read in existing data from FujiNet
  host_read();
  disk_read();

  // Mount desired host
  host_mount(hs);

  // Set desired slot filename/mode
  strcpy(deviceSlots.slot[ds].file,buf);
  deviceSlots.slot[ds].mode=o;
  deviceSlots.slot[ds].hostSlot=hs;

  // Write out disk slot
  disk_write();

  // Mount the disk.
  disk_mount(ds,o);

  print("D");
  printc(&dsa);
  print(": ");
  print("(");
  printc(&hsa);
  print(") ");
  print("(");
  print((o==0x02 ? "W" : "R"));
  print(") ");
  print(buf);
  print("\x9b\x9b");

  if (!_is_cmdline_dos())
    {
      print("\x9bPRESS \xD2\xC5\xD4\xD5\xD2\xCE TO CONTINUE.\x9b");
      get_line(buf,sizeof(buf));
    }  

  return(0);
}
