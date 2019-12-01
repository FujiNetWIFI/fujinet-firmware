/**
 * FujiNet Configuration Program
 */

#include <atari.h>
#include <string.h>
#include <peekpoke.h>
#include <conio.h>
#include "config.h"
#include "screen.h"
#include "sio.h"
#include "bar.h"
#include "die.h"

bool _configured=false;
unsigned char _num_networks;

extern unsigned char* video_ptr;
extern unsigned char* dlist_ptr;
extern unsigned short screen_memory;
extern unsigned char* font_ptr;

unsigned char fontPatch[24]={
			 0,0,0,0,0,0,3,51,
			 0,0,3,3,51,51,51,51,
			 48,48,48,48,48,48,48,48
};

void dlist=
  {
   DL_BLK8,
   DL_BLK8,
   DL_BLK8,
   DL_LMS(DL_CHR20x8x2),
   DISPLAY_MEMORY,

   DL_CHR20x8x2,
   DL_CHR40x8x1,
   DL_CHR40x8x1,
   DL_CHR40x8x1,
   DL_CHR40x8x1,
   DL_CHR40x8x1,
   DL_CHR40x8x1,
   DL_CHR40x8x1,
   DL_CHR40x8x1,
   DL_CHR40x8x1,
   DL_CHR40x8x1,
   DL_CHR40x8x1,
   DL_CHR40x8x1,
   DL_CHR40x8x1,
   DL_CHR40x8x1,
   DL_CHR40x8x1,
   DL_CHR40x8x1,
   DL_CHR40x8x1,
   DL_CHR40x8x1,
   DL_CHR40x8x1,
   DL_CHR40x8x1,
   DL_CHR20x8x2,
   DL_CHR20x8x2,   
   DL_JVB,
   0x600
  };

union 
{
  struct
  {
    char ssid[32];
    signed char rssi;
  };
  unsigned char rawData[33];
} ssidInfo;

union
{
  struct
  {
    char ssid[32];
    char password[64];
  };
  unsigned char rawData[96];
} netConfig;

unsigned char config_sector[128];
unsigned char wifiStatus;

#define COLOR_SETTING_NETWORK 0x66
#define COLOR_SETTING_FAILED 0x33
#define COLOR_SETTING_SUCCESSFUL 0xB4
#define COLOR_CHECKING_NETWORK 0x26

/**
 * Is device configured?
 */
bool configured(void)
{
  OS.dcb.ddevic=0x31;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd='R'; // Is device configured?
  OS.dcb.dstats=0x40; // Peripheral->Computer
  OS.dcb.dbuf=&config_sector;
  OS.dcb.dtimlo=0x0F; // 15 second timeout
  OS.dcb.dbyt=128;      // single sector
  OS.dcb.daux=720;
  siov();

  if (config_sector[127]==0xFF)
    _configured=true;
  else
    _configured=false;
  
  return _configured;
}

/**
 * Return number of networks
 */
unsigned char config_do_scan(unsigned char* num_networks)
{
  OS.dcb.ddevic=0x70;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd=0xFD; // do scan
  OS.dcb.dstats=0x40; // Peripheral->Computer
  OS.dcb.dbuf=num_networks;
  OS.dcb.dtimlo=0x0F; // 15 second timeout
  OS.dcb.dbyt=1;      // 1 byte response
  OS.dcb.daux=0;
  siov();

  return OS.dcb.dstats;
}

/**
 * Return Network entry from last scan
 */
unsigned char config_scan_result(unsigned char n)
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

  return OS.dcb.dstats;
}

/**
 * Write desired SSID and password to SIO
 */
unsigned char config_set_ssid(void)
{
  OS.dcb.ddevic=0x70;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd=0xFB; // Set SSID
  OS.dcb.dstats=0x80; // Computer->Peripheral
  OS.dcb.dbuf=&netConfig.rawData;
  OS.dcb.dtimlo=0x0f; // 15 second timeout
  OS.dcb.dbyt=sizeof(netConfig.rawData);
  OS.dcb.daux=0;
  siov();

  return OS.dcb.dstats;
}

/**
 * Get WiFi Network Status
 */
unsigned char config_get_wifi_status(void)
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

  return OS.dcb.dstats;  
}

/**
 * Print the ssid at index i
 */
void config_print_ssid(unsigned char i)
{
  screen_puts(2,i+3,(char *)ssidInfo.ssid);
}


/**
 * Print the RSSI at index i
 */
void config_print_rssi(unsigned char i)
{
  char out[4]={0x20,0x20,0x20};

  if (ssidInfo.rssi>-40)
    {
      out[0]=0x01;
      out[1]=0x02;
      out[2]=0x03;
    }
  else if (ssidInfo.rssi>-60)
    {
      out[0]=0x01;
      out[1]=0x02;
    }
  else
    {
      out[0]=0x01;
    }

  screen_puts(35,i+3,out);
}

/**
 * Print networks
 */
void config_print_networks(unsigned char n)
{
  unsigned char i;
  
  for (i=0;i<n;i++)
    {
      config_scan_result(i);
      config_print_ssid(i);
      config_print_rssi(i);
    }
}

/**
 * Setup the config screen
 */
void config_setup(void)
{
  OS.color0=0x9C;
  OS.color1=0x0F;
  OS.color2=0x92;
  OS.color4=0x92;
  OS.coldst=1;
  OS.sdmctl=0; // Turn off screen
  memcpy((void *)DISPLAY_LIST,&dlist,sizeof(dlist)); // copy display list to $0600
  OS.sdlst=(void *)DISPLAY_LIST;                     // and use it.
  dlist_ptr=(unsigned char *)OS.sdlst;               // Set up the vars for the screen output macros
  screen_memory=PEEKW(560)+4;
  video_ptr=(unsigned char*)(PEEKW(screen_memory));

  // Copy ROM font
  memcpy((unsigned char *)FONT_MEMORY,(unsigned char *)0xE000,1024);

  // And patch it.
  font_ptr=(unsigned char*)FONT_MEMORY;
  memcpy(&font_ptr[520],&fontPatch,24);

  OS.chbas=0x20; // use the charset
  bar_clear();
  bar_setup_regs();
  
}

/**
 * Print error
 */
void config_print_error(unsigned char s)
{
  if (s==138)
    {
      screen_puts(0,21,"  NO FUJINET FOUND  ");
    }
  else if (s==139)
    {
      screen_puts(0,21,"    FUJINET NAK!    ");
    }
}

/**
 * Write config to "disk"
 */
void config_write(void)
{
  memset(&config_sector,0x00,sizeof(config_sector));

  strcpy(&config_sector[0],netConfig.ssid);
  strcpy(&config_sector[32],netConfig.password);
  config_sector[127]=0xFF; // Now configured.
  
  OS.dcb.ddevic=0x31;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd='P';
  OS.dcb.dstats=0x80;
  OS.dcb.dbuf=&config_sector;
  OS.dcb.dtimlo=0x0F;
  OS.dcb.dbyt=128;
  OS.dcb.daux=720; // 720 = the config sector.
  siov();
}

/**
 * Run Wifi scan and Configuration
 */
void config_run(void)
{
  unsigned char s; // status
  unsigned char num_networks; // Number of networks
  unsigned char y; // cursor
  unsigned char done; // selection done?
  unsigned char successful; // connection successful?
  unsigned char k; // keypress
  unsigned char x; // password cursor
  
  config_setup();
  
  while(successful==false)
    {
      screen_clear();
      screen_puts(0,0,"WELCOME TO #FUJINET!");
      screen_puts(0,21,"SCANNING NETWORKS...");
      
      s=config_do_scan(&num_networks);
      
      if (s!=1)
	{
	  config_print_error(s);
	  die();
	}
      else
	config_print_networks(num_networks);
      
      screen_puts(0,21,"  SELECT A NETWORK  ");
      
      done=false;
      
      while (done==false)
	{
	  bar_clear();
	  bar_show(y+4);

	  k=cgetc();
	  if (k==0x9B)
	    done=true;
	  else if ((k==0x1C) && (y>0))
	    y--;
	  else if ((k==0x1D) && (y<num_networks))
	    y++;
	}
      
      screen_puts(0,21,"  ENTER PASSWORD:   ");
      
      done=false;
      
      memset(&netConfig.password,0x00,sizeof(netConfig.password));
      
      while (done==false)
	{
	  k=cgetc();
	  if (k==126)
	    x--;
	  else if (k==155)
	    done=true;
	  else
	    netConfig.password[x]=k;
	}
      
      screen_puts(0,21,"  SETTING NETWORK   ");

      OS.pcolr0=OS.pcolr1=OS.pcolr2=OS.pcolr3=COLOR_SETTING_NETWORK;
      
      // Setting network involves getting the entry again, and returning
      // it in a 'set ssid' ;)
      
      config_scan_result(y); // Y just happens to be on the item we want
      strcpy(netConfig.ssid,ssidInfo.ssid);

      config_set_ssid(); // send to fujinet

      done=false;

      OS.rtclok[0]=OS.rtclok[1]=OS.rtclok[2]=0;
      
      while (done==false)
	{
	  if ((OS.rtclok[2] & 0x3f)==0)
	    {
	      OS.pcolr0=OS.pcolr1=OS.pcolr2=OS.pcolr3=COLOR_CHECKING_NETWORK;
	      config_get_wifi_status();

	      if (wifiStatus==3)
		{
		  successful=true;
		  done=true;
		}
	      
	    }
	  else
	    {
	      OS.pcolr0=OS.pcolr1=OS.pcolr2=OS.pcolr3=COLOR_SETTING_NETWORK;
	    }

	  if (OS.rtclok[1]==2) // we timed out...
	    {
	      successful=false;
	      done=true;
	      screen_puts(0,21," COULD NOT CONNECT. ");
	      OS.rtclok[2]=0;
	      while (OS.rtclok[2]<250) { }	      
	    }
	}
    }

  OS.pcolr0=OS.pcolr1=OS.pcolr2=OS.pcolr3=COLOR_SETTING_SUCCESSFUL;
  config_write();
  
  die();
  
}
