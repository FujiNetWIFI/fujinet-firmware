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

unsigned char buf[255];

union
{
  struct
  {
    char ssid[32];
    char hostname[64];
    unsigned char localIP[4];
    unsigned char gateway[4];
    unsigned char netmask[4];
    unsigned char dnsIP[4];
    unsigned char macAddress[6];
  };
  unsigned char rawData[118];
} adapterConfig;

/**
 * Read Device Slots
 */
void adapter_config(void)
{
  OS.dcb.ddevic=0x70;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd=0xE8;
  OS.dcb.dstats=0x40;
  OS.dcb.dbuf=&adapterConfig.rawData;
  OS.dcb.dtimlo=0x0f;
  OS.dcb.dbyt=sizeof(adapterConfig.rawData);
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
  print("\xa3\xc6\xf5\xea\xe9\xce\xe5\xf4\xa0\xce\xe5\xf4\xf7\xef\xf2\xeb\xa0\xC3\xef\xee\xe6\xe9\xe7\xf5\xf2\xe1\xf4\xe9\xef\xee\x9b\x9b"); // #FujiNet Network Configuration
  print("\x9c\x9c\x9c\x9c\x9c\x9c\x9c\x9c\x9c\x9c");
}

/**
 * print a dotted quad address
 */
void print_address(unsigned char* address)
{
  unsigned char tmp[4];

  itoa(address[0],tmp,10);
  print(tmp);
  print(".");
  itoa(address[1],tmp,10);
  print(tmp);
  print(".");
  itoa(address[2],tmp,10);
  print(tmp);
  print(".");
  itoa(address[3],tmp,10);
  print(tmp);  
}

/**
 * Print MAC address as : separated HEX
 */
void print_mac(unsigned char* mac)
{
  unsigned char tmp[3];

  itoa(mac[0],tmp,16);
  print(tmp);
  print(":");
  itoa(mac[1],tmp,16);
  print(tmp);
  print(":");
  itoa(mac[2],tmp,16);
  print(tmp);
  print(":");
  itoa(mac[3],tmp,16);
  print(tmp);
  print(":");
  itoa(mac[4],tmp,16);
  print(tmp);
  print(":");
  itoa(mac[5],tmp,16);
  print(tmp);
}

/**
 * main
 */
int main(void)
{

  OS.lmargn=2;
  
  // Read adapter config
  adapter_config();

  print("\x9b");

  if ((PEEK(0x718)==51) || (PEEK(0x718)==53) ||(PEEK(0x718)==56))
    dos3_clear();
  
  print("           SSID: ");
  print(adapterConfig.ssid);
  print("\x9b");

  print("       Hostname: ");
  print(adapterConfig.hostname);
  print("\x9b");
  
  print("     IP Address: ");
  print_address(adapterConfig.localIP);
  print("\x9b");

  print("Gateway Address: ");
  print_address(adapterConfig.gateway);
  print("\x9b");

  print("    DNS Address: ");
  print_address(adapterConfig.dnsIP);
  print("\x9b");
  
  print("        Netmask: ");
  print_address(adapterConfig.netmask);
  print("\x9b");

  print("    MAC Address: ");
  print_mac(adapterConfig.macAddress);

  print("\x9b");

  if (!_is_cmdline_dos())
    {
      print("\x9bPRESS \xA0\xD2\xC5\xD4\xD5\xD2\xCE\xA0 TO CONTINUE.\x9b");
      get_line(buf,sizeof(buf));
    }
  
  return(0);
}
