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

unsigned char path[256]="/";
unsigned char buf[40];

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
 * Open TNFS directory on host slot
 */
void directory_open(unsigned char hs)
{
  // Open TNFS directory
  OS.dcb.ddevic=0x70;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd=0xF7;
  OS.dcb.dstats=0x80;
  OS.dcb.dbuf=&path;
  OS.dcb.dtimlo=0x0F;
  OS.dcb.dbyt=256;
  OS.dcb.daux=hs;
  siov();

  if (OS.dcb.dstats!=1)
    {
      err_sio();
      exit(OS.dcb.dstats);
    }
}

/**
 * Read TNFS directory entry in host slot
 * and of specific length
 */
void directory_read(unsigned char hs, unsigned char len)
{
  OS.dcb.dcomnd=0xF6;
  OS.dcb.dstats=0x40;
  OS.dcb.dbuf=&path;
  OS.dcb.dbyt=len;
  OS.dcb.daux1=len;
  OS.dcb.daux2=hs;
  siov();
  
  if (OS.dcb.dstats!=1)
    {
      err_sio();
      exit(OS.dcb.dstats);
    }
}

/**
 * Close TNFS Directory
 */
void directory_close(unsigned char hs)
{
  // Close directory
  OS.dcb.dcomnd=0xF5;
  OS.dcb.dstats=0x00;
  OS.dcb.dbuf=NULL;
  OS.dcb.dtimlo=0x01;
  OS.dcb.dbyt=0;
  OS.dcb.daux=hs;
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
  print(" <hs#>\x9b\x9b");
  print("<hs#> - host slot (1-8)\x9b");
}

/**
 * Clear up to status bar for DOS 3
 */
void dos3_clear(void)
{
  print("\x1c\x1c\x1c\x1c\x1c\x1c\x1c\x1c\x1c\x1c");
  print("\xCC\xE9\xE3\XE4\xA0\xC6\xe9\xec\xe5\xf3\xa0\xef\xee\xa0\xC8\xEF\xF3\xf4\x9b\x9b"); // List Files on Host
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
      // DOS 2.0
      print("\x9b");

      if (PEEK(0x718)==53)
	dos3_clear();
      
      print("HOST SLOT (1-8)? ");
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
  host_read();

  // Mount host for reading
  host_mount(s);
  
  print("\x9b");

  print(hostSlots.host[s]);
  
  print(":\x9b\x9b");

  directory_open(s);

  // Read directory
  while (path[0]!=0x7F)
    {
      memset(path,0,sizeof(path));
      path[0]=0x7f;

      directory_read(s,36); // show 36 chars max
      
      if (path[0]=='.')
	continue;
      else if (path[0]==0x7F)
	break;
      else
	{
	  print(path);
	  print("\x9b");
	}
    }

  directory_close(s);

  if (!_is_cmdline_dos())
    {
      print("\x9bPRESS \xA0\xD2\xC5\xD4\xD5\xD2\xCE\xA0 TO CONTINUE.\x9b");
      get_line(buf,sizeof(buf));
    }  
  
  return(0);
}
