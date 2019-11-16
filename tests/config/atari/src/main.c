/**
 * #AtariWiFi Test #9 - Get/Set SSID/Password
 *
 * Author: Thomas Cherryhomes
 *  <thom.cherryhomes@gmail.com>
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

unsigned char status[4]; // Network status

NetInfo ni;


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
 * Press any key to continue message.
 */
void press_key(void)
{
  cprintf("Press RETURN to continue.");
  cgetc();
}

void print_network_info(NetInfo* ni)
{
  cprintf("SSID: %s\r\n",ni->ssid);
  cprintf("BSSID: %02x:%02x:%02x:%02x:%02x:%02x\r\n", ni->bssid[5], ni->bssid[4], ni->bssid[3], ni->bssid[2], ni->bssid[1], ni->bssid[0]);
  cprintf("IP: %u.%u.%u.%u\r\n",ni->ipAddress[3],ni->ipAddress[2],ni->ipAddress[1],ni->ipAddress[0]);
  cprintf("MAC: %02x:%02x:%02x:%02x:%02x:%02x\r\n", ni->macAddress[5], ni->macAddress[4], ni->macAddress[3], ni->macAddress[2], ni->macAddress[1], ni->macAddress[0]);
  cprintf("RSSI: %ld\r\n",ni->rssi);
  cprintf("\r\n\r\n");
  press_key();
}

/**
 * Get the Network info
 */
void get_network_info(void)
{
  struct regs r;

  // Get status
  OS.dcb.ddevic=0x70;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd='S';
  OS.dcb.dstats=0x40;
  OS.dcb.dbuf=&status;
  OS.dcb.dtimlo=0x0F;
  OS.dcb.dunuse=0;
  OS.dcb.dbyt=4;
  OS.dcb.daux1=0;
  OS.dcb.daux2=0;
  r.pc=0xE459;
  _sys(&r);

  if (status[0]!=0x03)
    cprintf("Connecting...\r\n");
  
  while (status[0]!=0x03)  // 3 = WL_CONNECTED
    {
      // Get status
      OS.dcb.ddevic=0x70;
      OS.dcb.dunit=1;
      OS.dcb.dcomnd='S';
      OS.dcb.dstats=0x40;
      OS.dcb.dbuf=&status;
      OS.dcb.dtimlo=0x0F;
      OS.dcb.dunuse=0;
      OS.dcb.dbyt=4;
      OS.dcb.daux1=0;
      OS.dcb.daux2=0;
      r.pc=0xE459;
      _sys(&r);
    }
  
  // Status is ready
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
}

/**
 * Set Network Info
 */
void set_network_info(void)
{
  struct regs r;
  cursor(1);
  cprintf("SSID: ");
  cscanf("%s",ee.ssid);
  cprintf("\r\nPassword: ");
  cscanf("%s",ee.key);

  cprintf("\r\n\r\nWriting configuration...\r\n\r\n");
  
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

  /* cprintf("\r\n\r\nReading Configuration...\r\n"); */
  /* get_network_info(); */
  /* print_network_info(&ni); */
}

void main(void)
{
  clrscr();
  cprintf("#AtariWiFi Test Program #9 - Config\r\n\r\n");
  set_network_info();
  /* cprintf("Getting Network Configuration...\r\n\r\n"); */

  /* get_network_info(); */
  
  /* if (ni.ssid[0]==0x00) */
  /*   { */
  /*     set_network_info(); */
  /*   } */
  /* else */
  /*   { */
  /*     print_network_info(&ni); */
  /*   } */
}
