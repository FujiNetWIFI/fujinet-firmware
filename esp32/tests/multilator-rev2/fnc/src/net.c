/**
 * Config for SpartaDOS/DOS XL
 *
 * Author: Thom Cherryhomes
 *  <thom.cherryhomes@gmail.com>
 *
 * Licensed under GPL v. 3
 * See COPYING for details
 */

#include <atari.h>
#include <stdio.h>
#include <string.h>
#include "net.h"
#include "sio.h"
#include "error.h"

union
{
  struct
  {
    char ssid[32];
    char password[64];
  };
  unsigned char rawData[96];
} netConfig;

/**
 * Network Config
 */
unsigned char net(int argc, char* argv[])
{
  unsigned char wifiStatus=0;
  strcpy(netConfig.ssid,argv[2]);
  strcpy(netConfig.password,argv[3]);

  if (argc==2)
    printf("Connecting to last network...");
  else
    printf("Connecting to network: %s...",netConfig.ssid);
    
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
	      printf("OK!\n\n");
	      return 0;
	    }
	}
      
      printf("Not Connected.\n\n");
    }
  else
    {
      sio_error();
    }
  return wifiStatus;
}
