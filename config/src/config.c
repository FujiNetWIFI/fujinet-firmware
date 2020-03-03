/**
 * FujiNet Configuration Program
 */

#include <atari.h>
#include <string.h>
#include <conio.h>
#include <stdlib.h>
#include "config.h"
#include "screen.h"
#include "sio.h"
#include "bar.h"
#include "die.h"

bool _configured=false;
bool hidden_network_selected=false;

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
unsigned char successful=0; // connection successful?

#define COLOR_SETTING_NETWORK 0x66
#define COLOR_SETTING_FAILED 0x33
#define COLOR_SETTING_SUCCESSFUL 0xB4
#define COLOR_CHECKING_NETWORK 0x26

/**
 * Is device configured?
 */
bool configured(void)
{

  if (GTIA_READ.consol==5)
    {
      _configured=false;
      return _configured;
    }
  
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
  OS.dcb.dbyt=4;      // 4 byte response
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

  if (n>16)
    n=16;
  
  for (i=0;i<n;i++)
    {
      config_scan_result(i);
      config_print_ssid(i);
      config_print_rssi(i);
    }
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
 * Read config from "disk"
 */
void config_read(void)
{
  OS.dcb.ddevic=0x31;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd='R';
  OS.dcb.dstats=0x40;
  OS.dcb.dbuf=&config_sector;
  OS.dcb.dtimlo=0x0F;
  OS.dcb.dbyt=128;
  OS.dcb.daux=720;
  siov();

  strcpy(netConfig.ssid,&config_sector[0]);
  strcpy(netConfig.password,&config_sector[32]);
}

/**
 * wait for connection
 */
bool config_wait_for_wifi(void)
{
  OS.rtclok[0]=OS.rtclok[1]=OS.rtclok[2]=0;
 
  while (1)
    {
      if ((OS.rtclok[2] & 0x3f)==0)
	{
	  OS.pcolr0=OS.pcolr1=OS.pcolr2=OS.pcolr3=COLOR_CHECKING_NETWORK;
	  config_get_wifi_status();
	  
	  if (wifiStatus==3)
	    {
	      OS.pcolr0=OS.pcolr1=OS.pcolr2=OS.pcolr3=COLOR_SETTING_SUCCESSFUL;
	      config_write();
	      
	      OS.rtclok[0]=OS.rtclok[1]=OS.rtclok[2]=0;
	      
	      while (OS.rtclok[2]<128) { } // Delay
	      
	      bar_clear();
	      
	      _configured=true;
	      successful=true;
	      
	      return true;
	    }
	  
	}
      else
	{
	  OS.pcolr0=OS.pcolr1=OS.pcolr2=OS.pcolr3=COLOR_SETTING_NETWORK;
	}
      
      if (OS.rtclok[1]==2) // we timed out...
	{
	  screen_puts(0,21," COULD NOT CONNECT. ");
	  OS.rtclok[2]=0;
	  while (OS.rtclok[2]<128) { }

	  bar_clear();

	  _configured=false;
	  
	  return false;
	}
    } 
}

/**
 * Connect to configured network
 */
bool config_connect(void)
{
  screen_clear();
  config_read();
  screen_puts(0,0,"WELCOME TO #FUJINET! CONNECTING TO NET ");
  screen_puts(2,2,netConfig.ssid);
  bar_show(3);
  config_set_ssid();
  return config_wait_for_wifi();
}

/**
 * Enter a hidden network
 */
void config_hidden_net(void)
{
  screen_puts(2,3,"                                      ");
  bar_show(4);
  screen_puts(1,21,"ENTER NETWORK NAME                      ");
  screen_input(1,3,netConfig.ssid);
  hidden_network_selected=true;
}

/**
 * Run Wifi scan and Configuration
 */
void config_run(void)
{
  unsigned char s; // status
  unsigned char num_networks[4]; // Number of networks
  unsigned char y=0; // cursor
  unsigned char done=0; // selection done?
  unsigned char k; // keypress
  
  while(successful==false)
    {
      screen_clear();
      screen_puts(0,0,"WELCOME TO #FUJINET!");
      screen_puts(0,21,"SCANNING NETWORKS...   PLEASE WAIT.   ");

      OS.ch=0xFF;
      s=config_do_scan(num_networks);
      OS.ch=0xFF;
      if (s!=1)
	{
	  config_print_error(s);
	  die();
	}
      
      screen_puts(0,21,"  SELECT A NETWORK  ");
      screen_puts(20,21,"OR X FOR HIDDEN NET");
      
      done=false;
      OS.ch=0xff;
      while (done==false)
	{
	  bar_clear();
	  bar_show(y+4);

	  k=cgetc();
	  if (k==0x9B)
	    done=true;
	  else if (((k==0x1C) || (k=='-')) && (y>0))
	    y--;
	  else if (((k==0x1D) || (k=='=')) && (y<num_networks[0]))
	    y++;
	  else if (k=='x')
	    {
	      done=true;
	      config_hidden_net();
	    }
	}
      
      screen_puts(0,21,"  ENTER PASSWORD:                      ");
      
      done=false;
      
      memset(&netConfig.password,0x00,sizeof(netConfig.password));

      screen_input(1,19,netConfig.password);
            
      screen_puts(0,21,"  SETTING NETWORK                      ");

      OS.pcolr0=OS.pcolr1=OS.pcolr2=OS.pcolr3=COLOR_SETTING_NETWORK;
      
      // Setting network involves getting the entry again, and returning
      // it in a 'set ssid' ;)

      if (hidden_network_selected==false)
	{
	  config_scan_result(y); // Y just happens to be on the item we want
	  strcpy(netConfig.ssid,ssidInfo.ssid);
	}
      
      config_set_ssid(); // send to fujinet

      done=false;

      OS.rtclok[0]=OS.rtclok[1]=OS.rtclok[2]=0;
      config_wait_for_wifi();
    }

}
