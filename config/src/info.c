/**
 * FujiNet Configuration Program
 */

#include <atari.h>
#include <string.h>
#include <conio.h>
#include <stdlib.h>
#include <peekpoke.h>
#include "info.h"
#include "screen.h"
#include "sio.h"
#include "bar.h"
#include "die.h"
#include "config.h"

unsigned char kchar;

extern union
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
    unsigned char bssid[6];
  };
  unsigned char rawData[124];
} adapterConfig;


/**
 * Print IP address at position
 */
void print_ip(unsigned char x, unsigned char y, unsigned char* buf)
{
  unsigned char i=0;
  unsigned char o=0;
  unsigned char tmp[4];

  for (i=0;i<4;i++)
    {
      itoa(buf[i],tmp,10);
      screen_puts(x+o,y,tmp);
      o+=strlen(tmp);
      if (i<3)
	screen_puts(x+(o++),y,".");
    }
}

/**
 * Print MAC address at position
 */
void print_mac(unsigned char x, unsigned char y, unsigned char* buf)
{
  unsigned char i=0;
  unsigned char o=0;
  unsigned char tmp[3];

  for (i=0;i<6;i++)
    {
      itoa(buf[i],tmp,16);
      screen_puts(x+o,y,tmp);
      o+=strlen(tmp);
      if (i<5)
	screen_puts(x+(o++),y,":");
    }
}

/**
 * Show network info
 */
void info_run(void)
{
  screen_clear();
  bar_clear();
  
  // Patch display list

  POKE(0x60A,7);
  POKE(0x60B,6);
  POKE(0x60F,2);
  POKE(0x610,2);

  screen_puts(0,4, "  #FUJINET  CONFIG  ");
  screen_puts(11,14,"Press \xD9\xA3\x19 reconnect");
  screen_puts(9,15,"Any other key to return");
  screen_puts( 5,5, "      SSID:");
  screen_puts( 5,6, "  Hostname:");
  screen_puts( 5,7, "IP Address:");
  screen_puts( 5,8, "   Gateway:");
  screen_puts( 5,9,"       DNS:");
  screen_puts( 5,10,"   Netmask:");
  screen_puts( 5,11,"       MAC:");
  screen_puts( 5,12,"     BSSID:");

  // Grab adapter config
  OS.dcb.ddevic=0x70;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd=0xE8;
  OS.dcb.dstats=0x40;
  OS.dcb.dbuf=&adapterConfig.rawData;
  OS.dcb.dtimlo=0x0f;
  OS.dcb.dbyt=sizeof(adapterConfig.rawData);
  OS.dcb.daux=0;
  siov();    
  
  screen_puts(17,5,adapterConfig.ssid);
  screen_puts(17,6,adapterConfig.hostname);
  print_ip(17,7,adapterConfig.localIP);
  print_ip(17,8,adapterConfig.gateway);
  print_ip(17,9,adapterConfig.dnsIP);
  print_ip(17,10,adapterConfig.netmask);
  print_mac(17,11,adapterConfig.macAddress);
  print_mac(17,12,adapterConfig.bssid);
  
  while (!kbhit()) { } // Wait for key.
  
  kchar = cgetc();

  if ( kchar == 67 || kchar == 99 )
  {
    config_connect();
  }

  // Patch it back
  POKE(0x60A,2);
  POKE(0x60B,2);
  POKE(0x60F,6);
  POKE(0x610,6);
}
