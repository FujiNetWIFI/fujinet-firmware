/**
 * FujiNet Tools for CLI
 *
 * fhost - view/change host slots
 *
 * usage:
 *  fhost [hs #] [hostname]
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

const char msg_host_slot[]="HOST SLOT #";

union
{
  char host[8][32];
  unsigned char rawData[256];
} hostSlots;

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
 * Write host slots
 */
void host_write(void)
{
  OS.dcb.ddevic=0x70;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd=0xF3;
  OS.dcb.dstats=0x80;
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
 * show options
 */
void opts(char* argv[])
{
  print(argv[0]);
  print(" <hs#> [HOSTNAME]\x9b\x9b");
  print("<hs#> - host slot (1-8)\x9b\x9b");
  print("If hostname is not specified, host slot will be erased.\x9b");
}

/**
 * main
 */
int main(int argc, char* argv[])
{
  unsigned char sa=argv[1][0];
  unsigned char s=sa-0x30;
  
  if (argc<2 || argc>3)
    {
      opts(argv);
      return(1);
    }

  if (s<1 || s>8)
    {
      print("INVALID SLOT NUMBER.\x9b");
      return(1);
    }

  s-=1;
  
  // Read in host and device slots from FujiNet
  host_read();

  // alter host slot
  if (argc==2)
    {
      // Erase host
      memset(hostSlots.host[s],0x00,sizeof(hostSlots.host[s]));
      print(msg_host_slot);
      printc(&sa);
      print(" cleared.\x9b");
    }
  else
    {
      // Alter host value
      strcpy(hostSlots.host[s],argv[2]);
      print(msg_host_slot);
      printc(&sa);
      print(" changed to: ");
      print(argv[2]);
    }

  // write the deviceSlots back to FujiNet
  host_write();

  // Mount server
  host_mount(s);
  
  return(0);
}
