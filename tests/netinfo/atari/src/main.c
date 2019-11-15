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
#include <stdio.h>

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

/**
 * Function to retrieve network info from adapter.
 *
 * Params: A pointer to a NetInfo struct.
 * Returns: Status (1 on success, otherwise error.)
 *          Filled in NetInfo struct on success.
 */
unsigned char sio_get_network_info(NetInfo* ni)
{
  struct regs r;
  OS.dcb.ddevic=0x70;          // Network control device
  OS.dcb.dunit=0;              // unit 1
  OS.dcb.dcomnd='!';           // Get network info command
  OS.dcb.dbuf=ni->rawData;     // Pointer to netinfo buffer
  OS.dcb.dtimlo=0x0f;          // 16 frame timeout
  OS.dcb.dstats=0x40;          // This is a read.
  OS.dcb.dbyt=64;              // 64 bytes of data
  OS.dcb.daux=0;               // no aux for now.

  // $E459 = SIOV
  r.pc=0xE459;
  _sys(&r);

  return OS.dcb.dstats;
}

void print_network_info(NetInfo* ni)
{
  printf("SSID: %s\n",ni->ssid);
  printf("BSSID: %02x:%02x:%02x:%02x:%02x:%02x\n", ni->bssid[5], ni->bssid[4], ni->bssid[3], ni->bssid[2], ni->bssid[1], ni->bssid[0]);
  printf("IP: %u.%u.%u.%u\n",ni->ipAddress[3],ni->ipAddress[2],ni->ipAddress[1],ni->ipAddress[0]);
  printf("MAC: %02x:%02x:%02x:%02x:%02x:%02x\n", ni->macAddress[5], ni->macAddress[4], ni->macAddress[3], ni->macAddress[2], ni->macAddress[1], ni->macAddress[0]);
  printf("RSSI: %ld",ni->rssi);
  printf("\n\n");
}

void main(void)
{
  NetInfo ni;
  unsigned char error;
  printf("#AtariWiFi Test Program #8\n");
  printf("Get Network Information.\n");
  printf("\n\n");

  printf("Reading...");

  error=sio_get_network_info(&ni);

  if (error==138)
    {
      printf("Timeout!\n");
    }
  else if (error==1)
    {
      printf("Success!\n");
      print_network_info(&ni);
    }
  else
    {
      printf("Error #%d",error);
    }

  printf("Program done. ");
  
  for (;;) { } // spin.
}
