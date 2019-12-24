/**
 * Fujinet Configurator
 *
 * Diskulator
 */

#include <atari.h>
#include <string.h>
#include <peekpoke.h>
#include <conio.h>
#include <stdbool.h>
#include <stdlib.h>
#include "diskulator.h"
#include "screen.h"
#include "sio.h"
#include "bar.h"
#include "die.h"

unsigned char path[256]="/";
unsigned char c;
unsigned char files[16][36];
unsigned char diskulator_done=false;
unsigned char selected_host;
unsigned char filter[32];

extern unsigned char* video_ptr;
extern unsigned char* dlist_ptr;
extern unsigned short screen_memory;
extern unsigned char* font_ptr;

union
{
  unsigned char host[8][32];
  unsigned char rawData[256];
} hostSlots;

union
{
  struct
  {
  unsigned char hostSlot;
  unsigned char file[36];
  } slot[8];
  unsigned char rawData[296];
} deviceSlots;

/**
 * Read host slots
 */
void diskulator_read_host_slots(void)
{
  // Query for host slots
  OS.dcb.ddevic=0x70;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd=0xF4; // Get host slots
  OS.dcb.dstats=0x40;
  OS.dcb.dbuf=&hostSlots.rawData;
  OS.dcb.dtimlo=0x0f;
  OS.dcb.dbyt=256;
  OS.dcb.daux=0;
  siov();
}

/**
 * Write host slots
 */
void diskulator_write_host_slots(void)
{
  OS.dcb.ddevic=0x70;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd=0xF3;
  OS.dcb.dstats=0x80;
  OS.dcb.dbuf=&hostSlots.rawData;
  OS.dcb.dtimlo=0x0f;
  OS.dcb.dbyt=256;
  OS.dcb.daux=0;
  siov();
}

/**
 * Mount a host slot
 */
void diskulator_mount_host(unsigned char c)
{
  if (hostSlots.host[c][0]!=0x00)
    {
      OS.dcb.ddevic=0x70;
      OS.dcb.dunit=1;
      OS.dcb.dcomnd=0xF9;
      OS.dcb.dstats=0x00;
      OS.dcb.dbuf=NULL;
      OS.dcb.dtimlo=0x0f;
      OS.dcb.dbyt=0;
      OS.dcb.daux=c;
      siov();
    }
}

/**
 * Enter a diskulator Host
 */
void diskulator_host(void)
{
  bool host_done=false;
  unsigned char k;
  
  screen_clear();
  bar_clear();

  screen_puts(0,0,"   TNFS HOST LIST   ");

  diskulator_read_host_slots();
  
  if (OS.dcb.dstats!=0x01)
    {
      screen_puts(21,0,"COULD NOT GET HOSTS!");
      die();
    }

  screen_puts( 0,21,"  enter TO SELECT  ");
  screen_puts(21,21," space  TO EDIT   ");
  
  // Display host slots
  for (c=0;c<8;c++)
    {
      unsigned char n=c+1;
      unsigned char nc[2];

      utoa(n,nc,10);
      screen_puts(2,c+2,nc);
      
      if (hostSlots.host[c][0]!=0x00)
	screen_puts(4,c+2,hostSlots.host[c]);
      else
	screen_puts(4,c+2,"Empty");
    }

  // reset cursor
  c=0;

  while (host_done==false)
    {
      bar_clear();
      bar_show(c+3);

      k=cgetc();
      switch(k)
	{
	case 0x1C: // UP
	  if (c>0)
	    c--;
	  break;
	case 0x1D: // DOWN
	  if (c<8)
	    c++;
	  break;
	case 0x20: // SPACE
	  if (hostSlots.host[c][0]==0x00)
	    {
	      screen_puts(3,c+2,"                                    ");
	    }
	  screen_input(3,c+2,hostSlots.host[c]);
	  if (hostSlots.host[c][0]==0x00)
	    {
	      screen_puts(3,c+2,"Empty");
	    }
	  break;
	case 0x9B: // ENTER
	  selected_host=c;

	  // Write hosts
	  diskulator_write_host_slots();
	 
	  // Mount host
	  diskulator_mount_host(c);
	  
	  host_done=true;
	  break;
	}
    }
}



/**
 * Search
 */
void diskulator_search(void)
{
  screen_clear();
  bar_clear();
  
  screen_puts(0, 0,"ENTER SEARCH FILTER");
  screen_puts(0,21," EMPTY LINE CLEARS ");

  screen_puts(0,1,">");
  screen_input(1,1,filter);

}

/**
 * Select an image
 */
void diskulator_select(void)
{
  unsigned char num_entries;
  unsigned char selector_done=false;
  unsigned char e;
  unsigned char k;

 reopen: // yes, this is a hack.
  
  screen_clear();
  bar_clear();

  screen_puts(0,0,"    DISK IMAGES    ");

  screen_puts( 0,21,"   s TO SEARCH    ");
  screen_puts(20,21," return TO SELECT ");

  while(1)
    {
      // Open Dir
      OS.dcb.ddevic=0x70;
      OS.dcb.dunit=1;
      OS.dcb.dcomnd=0xF7;
      OS.dcb.dstats=0x80;
      OS.dcb.dbuf=&path;
      OS.dcb.dtimlo=0x0F;
      OS.dcb.dbyt=256;
      OS.dcb.daux=selected_host;
      siov();

      // Read directory
      while ((path[0]!=0x7F) || (num_entries<16))
	{
	  memset(path,0,sizeof(path));
	  path[0]=0x7f;
	  OS.dcb.dcomnd=0xF6;
	  OS.dcb.dstats=0x40;
	  OS.dcb.dbuf=&path;
	  OS.dcb.dbyt=36;
	  OS.dcb.daux1=36;
	  OS.dcb.daux2=selected_host;
	  siov();

	  if (path[0]=='.')
	    continue;
	  else if (path[0]==0x7F)
	    break;
	  else
	    {
	      strcpy(files[num_entries],path);
	      screen_puts(0,num_entries+2,path);
	      num_entries++;
	    }
	}

      // Close dir read
      OS.dcb.dcomnd=0xF5;
      OS.dcb.dstats=0x00;
      OS.dcb.dbuf=NULL;
      OS.dcb.dtimlo=0x0F;
      OS.dcb.dbyt=0;
      OS.dcb.daux=selected_host;
      siov();

      while (selector_done==false)
	{
	  bar_clear();
	  bar_show(e+3);
	  k=cgetc();
	  switch(k)
	    {
	    case 0x1C: // Up
	      if (e>0)
		e--;
	      break;
	    case 0x1D: // Down
	      if (e<num_entries)
		e++;
	      break;
	    case 's': // Search
	      diskulator_search();
	      goto reopen;
	      break;
	    case 0x9B: // Enter
	      selector_done=true;
	      bar_set_color(0x97);
	      memset(path,0,sizeof(path));
	      strcpy(path,files[e]);

	      return;
      
	      break;
	    }
	}
    }
}

/**
 * Read drive tables
 */
void diskulator_read_device_slots(void)
{
  // Read Drive Tables
  OS.dcb.ddevic=0x70;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd=0xF2;
  OS.dcb.dstats=0x40;
  OS.dcb.dbuf=&deviceSlots.rawData;
  OS.dcb.dtimlo=0x0f;
  OS.dcb.dbyt=296;
  OS.dcb.daux=0;
  siov();
}

/**
 * Write drive tables
 */
void diskulator_write_device_slots(void)
{
  OS.dcb.ddevic=0x70;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd=0xF1;
  OS.dcb.dstats=0x80;
  OS.dcb.dbuf=&deviceSlots.rawData;
  OS.dcb.dtimlo=0x0f;
  OS.dcb.dbyt=296;
  OS.dcb.daux=0;
  siov();
}

/**
 * Mount device slot
 */
void diskulator_mount_device(unsigned char c)
{
  OS.dcb.ddevic=0x70;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd=0xF8;
  OS.dcb.dstats=0x00;
  OS.dcb.dbuf=NULL;
  OS.dcb.dtimlo=0x0f;
  OS.dcb.dbyt=0;
  OS.dcb.daux=c; // drive slot
  siov();
}

/**
 * Select destination drive
 */
void diskulator_drive(void)
{
  unsigned char c,k;
  bool drive_done=false;
  
  screen_clear();
  bar_clear();
  
  screen_puts(0,0,"MOUNT TO DRIVE SLOT");
  screen_puts(0,21,"enter SELECT e EJECT");

  diskulator_read_device_slots();
  
  // Display drive slots
  for (c=0;c<8;c++)
    {
      unsigned char d[4];
      d[0]='D';
      d[1]=0x31+c;
      d[2]=':';
      d[3]=0x00;
      screen_puts(0,c+2,d);
      screen_puts(4,c+2,deviceSlots.slot[c].file[0]!=0x00 ? deviceSlots.slot[c].file : "Empty");
    }

  c=0;
  
  while (drive_done==false)
    {
      bar_clear();
      bar_show(c+3);

      k=cgetc();
      switch(k)
	{
	case 0x1C: // UP
	  if (c>0)
	    c--;
	  break;
	case 0x1D: // DOWN
	  if (c<8)
	    c++;
	  break;
	case 'e':  // E
	  screen_puts(4,c+2,"Empty                               ");
	  memset(deviceSlots.slot[c].file,0,sizeof(deviceSlots.slot[c].file));
	  deviceSlots.slot[c].hostSlot=0xFF;
	  break;
	case 0x9B: // RETURN
	  deviceSlots.slot[c].hostSlot=selected_host;
	  strcpy(deviceSlots.slot[c].file,path);

	  diskulator_write_device_slots();
	  diskulator_mount_device(c);
	  drive_done=true;
	  break;
	}
    }
}

/**
 * Do coldstart
 */
void diskulator_boot(void)
{
  asm("jmp $E477");
}

/**
 * Mount all Hosts
 */
void diskulator_mount_all_hosts(void)
{
  unsigned char e;

  bar_clear();
  screen_clear();

  screen_puts(0,0,"MOUNTING ALL HOSTS");
  
  for (e=0;e<8;e++)
    {
      diskulator_mount_host(e);
      if (OS.dcb.dstats!=0x01)
	{
	  screen_puts(0,21,"MOUNT ERROR!");
	  die();
	}
    }
}

/**
 * Mount all devices
 */
void diskulator_mount_all_devices(void)
{
  unsigned char e;
  
  bar_clear();
  screen_clear();

  screen_puts(0,0,"MOUNTING ALL DEVICES");

  for (e=0;e<8;e++)
    {
      diskulator_mount_device(e);
      if (OS.dcb.dstats!=0x01)
	{
	  screen_puts(0,21,"MOUNT ERROR!");
	  die();
	}      
    }
}

/**
 * Run the Diskulator
 */
void diskulator_run(void)
{
  if (GTIA_READ.consol==0x04) // Option
    {
      diskulator_read_host_slots();
      diskulator_read_device_slots();
      diskulator_mount_all_hosts();
      diskulator_mount_all_devices();
      diskulator_boot();
    }
  
  while (diskulator_done==false)
    {
      diskulator_host();
      diskulator_select();
      diskulator_drive();
    }
  diskulator_boot();
}
