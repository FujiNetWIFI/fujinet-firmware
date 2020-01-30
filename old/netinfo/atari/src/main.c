/**
 * #AtariWiFi test program #7
 *
 * Display Network Info
 *
 * Author:
 *  Thomas Cherryhomes
 *  <thom.cherryhomes@gmail.com>
 *
 */

#include <atari.h>
#include <6502.h>
#include <conio.h>

/**
 * The Netinfo structure to make it easy.
 *
 * ssid = the currently connected access point
 * bssid = the MAC address of the access point (in little endian order)
 * ipAddress = the IP address of the adapter (in little endian order)
 * macAddress = the MAC address of the adapter (in little endian order)
 * rssi = The calculated signal strength in dBm.
 */
typedef union _netinfo
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
} NetInfo;

NetInfo ni;

void print_network_info(NetInfo* ni)
{
  cprintf("SSID: %s\r\n",ni->ssid);
  cprintf("BSSID: %02x:%02x:%02x:%02x:%02x:%02x\r\n", ni->bssid[5], ni->bssid[4], ni->bssid[3], ni->bssid[2], ni->bssid[1], ni->bssid[0]);
  cprintf("IP: %u.%u.%u.%u\r\n",ni->ipAddress[3],ni->ipAddress[2],ni->ipAddress[1],ni->ipAddress[0]);
  cprintf("MAC: %02x:%02x:%02x:%02x:%02x:%02x\r\n", ni->macAddress[5], ni->macAddress[4], ni->macAddress[3], ni->macAddress[2], ni->macAddress[1], ni->macAddress[0]);
  cprintf("RSSI: %ld\r\n",ni->rssi);
  cprintf("\r\n\r\n");
}

void main(void)
{
  struct regs r;
  OS.dcb.ddevic=0x70;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd='!';
  OS.dcb.dstats=0x40;
  OS.dcb.dbuf=ni.rawData;
  OS.dcb.dtimlo=0x0F;
  OS.dcb.dunuse=0;
  OS.dcb.dbyt=64;
  OS.dcb.daux1=0;
  OS.dcb.daux2=0;
  r.pc=0xE459;
  _sys(&r);

  clrscr();
  cprintf("#AtariWiFi Test Program #8\r\n\r\n");

  if (OS.dcb.dstats==138)
    {
      cprintf("Request timed out.\r\n");
    }
  else if (OS.dcb.dstats==1)
    {
      print_network_info(&ni);
    }
  else
    {
      cprintf("Error #%d\r\n",OS.dcb.dstats);
    }

  for (;;) {}
}
