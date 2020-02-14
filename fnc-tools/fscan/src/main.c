/**
 * FujiNet Tools for CLI
 *
 * fscan - scan and return list of wireless networks
 *
 * usage:
 *  fscan
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

union 
{
  struct
  {
    char ssid[32];
    signed char rssi;
  };
  unsigned char rawData[33];
} ssidInfo;

unsigned char num_networks[4];

/**
 * Return number of networks
 */
void scan(void)
{
  OS.dcb.ddevic=0x70;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd=0xFD; // do scan
  OS.dcb.dstats=0x40; // Peripheral->Computer
  OS.dcb.dbuf=&num_networks;
  OS.dcb.dtimlo=0x0F; // 15 second timeout
  OS.dcb.dbyt=4;      // 4 byte response
  OS.dcb.daux=0;
  siov();

  if (OS.dcb.dstats!=0x01)
    {
      err_sio();
      exit(OS.dcb.dstats);
    }
}

/**
 * Return Network entry from last scan
 */
void scan_result(unsigned char n)
{
  OS.dcb.ddevic=0x70;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd=0xFC; // Return scan result
  OS.dcb.dstats=0x40; // Peripheral->Computer
  OS.dcb.dbuf=&ssidInfo.rawData;
  OS.dcb.dtimlo=0x0F; // 15 second timeout
  OS.dcb.dbyt=sizeof(ssidInfo.rawData);
  OS.dcb.daux1=n;     // get entry #n
  siov();

  if (OS.dcb.dstats!=0x01)
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
  print("\xD3\xE3\xE1\xEE\xA0\xC6\xEF\xF2\xA0\xCE\xE5\xF4\xF7\xEF\xF2\xEB\xF3\x9b\x9b"); // Scan for Networks
  print("\x9c\x9c\x9c\x9c\x9c\x9c\x9c\x9c\x9c\x9c");
}

/**
 * main
 */
int main(void)
{
  unsigned char i=0;

  if (PEEK(0x718)==53)
    dos3_clear();
  
  OS.lmargn=2;
  
  print("\x9b");
  print("Scanning...\x9b");
  scan();

  for (i=0;i<num_networks[0];i++)
    {
      scan_result(i);
      print("* ");
      print(ssidInfo.ssid);
      print("\x9b");
    }

  print("\x9b");
  
  return(0);
}
