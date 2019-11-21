/**
 * #AtariWiFi Test #9 - Get/Set SSID/Password
 *
 * Author: Thomas Cherryhomes
 *  <thom.cherryhomes@gmail.com>
 */

#include <atari.h>
#include <6502.h>
#include <string.h>

/**
 * The Netinfo structure to make it easy.
 *
 * ssid = the currently connected access point
 * bssid = the MAC address of the access point (in little endian order)
 * ipAddress = the IP address of the adapter (in little endian order)
 * macAddress = the MAC address of the adapter (in little endian order)
 * rssi = The calculated signal strength in dBm.
 */
union
{
  struct
  {
    unsigned char ssid[32];
    unsigned char bssid[6];
    unsigned char ipAddress[4];
    unsigned char macAddress[6];
    unsigned long rssi;
    unsigned char reserved[12];
  };
  unsigned char rawData[64];
} ni;

unsigned char status[4]; // Network status

/**
 * The EEPROM Data structure
 */
union
{
  struct
  {
    unsigned long magicNumber; // 4
    char ssid[32]; // 32
    char key[64]; // 64
    unsigned char reserved[412]; // Reserved block.
  };
  unsigned char rawData[512];
} ee;

/**
 * main
 */
void main(void)
{
  struct regs r;
  int i;
  
  strcpy(ee.ssid,"Cherryhomes");
  strcpy(ee.key,"e1xb64XC46");

  OS.color2=0x42;
  
  for (i=0;i<16384;i++) { }

  OS.color2=0x00;
  
  OS.dcb.ddevic=0x70;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd='"';
  OS.dcb.dstats=0x80;
  OS.dcb.dbuf=ee.rawData;
  OS.dcb.dtimlo=0x0F;
  OS.dcb.dunuse=0;
  OS.dcb.dbyt=512;
  OS.dcb.daux1=0;
  OS.dcb.daux2=0;
  r.pc=0xE459;
  _sys(&r);

  OS.color2=0x84;
  
}
