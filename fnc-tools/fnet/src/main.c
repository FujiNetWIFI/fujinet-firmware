/**
 * FujiNet Tools for CLI
 *
 * fnet - set Network SSID/Password
 *
 * usage:
 *  fnet <SSID> [PASSWORD]
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

unsigned char buf[80];

union
{
  struct
  {
    char ssid[32];
    char password[64];
  };
  unsigned char rawData[96];
} netConfig;

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
 * main
 */
int main(int argc, char* argv[])
{
  unsigned char wifiStatus=0;

  OS.soundr=0;
  OS.lmargn=2;

  if (_is_cmdline_dos())
    {
      if (argc<3)
	{
	  print(argv[0]);
	  print(" <SSID> <PASSWORD>\x9b");
	  exit(1);
	}
      
      strcpy(netConfig.ssid,argv[1]);
      strcpy(netConfig.password,argv[2]);
    }
  else
    {
      // Dos 2
      print("ENTER SSID: ");
      get_line(buf,sizeof(buf));
      strcpy(netConfig.ssid,buf);
      
      print("ENTER PASSWORD: ");
      get_line(buf,sizeof(buf));
      strcpy(netConfig.password,buf);      
    }
  
  print("Connecting to network: ");
  print(netConfig.ssid);
  print("...");
    
  OS.dcb.ddevic=0x70;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd=0xFB; // Set SSID
  OS.dcb.dstats=0x80; // Computer->Peripheral
  OS.dcb.dbuf=&netConfig.rawData;
  OS.dcb.dtimlo=0x0f; // 15 second timeout
  OS.dcb.dbyt=sizeof(netConfig.rawData);
  OS.dcb.daux=0;
  siov();

  if (OS.dcb.dstats==1)
    {

      // Wait for connection
      OS.rtclok[0]=OS.rtclok[1]=OS.rtclok[2]=0;
      
      while (OS.rtclok[1]<1)
	{
	  OS.dcb.ddevic=0x70;
	  OS.dcb.dunit=1;
	  OS.dcb.dcomnd=0xFA; // Return wifi status
	  OS.dcb.dstats=0x40; // Peripheral->Computer
	  OS.dcb.dbuf=&wifiStatus;
	  OS.dcb.dtimlo=0x0F; // 15 second timeout
	  OS.dcb.dbyt=1;
	  OS.dcb.daux1=0;     
	  siov();
	  
	  if (wifiStatus==0x03) // IW_CONNECTED
	    {
	      print("OK!\x9b");
	      remount_all();
	      return 0;
	    }
	}
      
      print("Not Connected.\x9b");
    }
  else
    {
      err_sio();
    }
  
  return wifiStatus;  
}
