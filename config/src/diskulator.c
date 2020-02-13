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
unsigned char o;
unsigned char files[16][36];
unsigned char diskulator_done=false;
unsigned char slot_done=true;
unsigned char selected_host;
unsigned char filter[32];
unsigned char prev_consol;

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
    unsigned char mode;
    unsigned char file[36];
  } slot[8];
  unsigned char rawData[304];
} deviceSlots;

/**
 * Do coldstart
 */
void diskulator_boot(void)
{
  asm("jmp $E477");
}

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
      OS.dcb.dtimlo=0x01;
      OS.dcb.dbyt=0;
      OS.dcb.daux=c;
      siov();
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
  OS.dcb.dbyt=sizeof(deviceSlots.rawData);
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
  OS.dcb.dbyt=sizeof(deviceSlots.rawData);
  OS.dcb.daux=0;
  siov();
}

/**
 * Mount device slot
 */
void diskulator_mount_device(unsigned char c, unsigned char o)
{
  OS.dcb.ddevic=0x70;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd=0xF8;
  OS.dcb.dstats=0x00;
  OS.dcb.dbuf=NULL;
  OS.dcb.dtimlo=0x01;
  OS.dcb.dbyt=0;
  OS.dcb.daux1=c;
  OS.dcb.daux2=o;
  siov();
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
      if (deviceSlots.slot[e].hostSlot!=0xFF)
	{
	  diskulator_mount_host(deviceSlots.slot[e].hostSlot);
	}
      
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
      if (deviceSlots.slot[e].hostSlot!=0xFF)
	diskulator_mount_device(e,deviceSlots.slot[e].mode);
      
      if (OS.dcb.dstats!=0x01)
	{
	  screen_puts(0,21,"MOUNT ERROR!");
	  die();
	}      
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

  // Temporarily patch display list for this screen.
  POKE(0x60F,6);
  POKE(0x610,6);
  POKE(0x61B,2);
  POKE(0x61C,2);
  
  screen_puts(0,0,"   TNFS HOST LIST   ");

  diskulator_read_host_slots();
  
  if (OS.dcb.dstats!=0x01)
    {
      screen_puts(21,0,"COULD NOT GET HOSTS!");
      die();
    }

  // Display host slots
  for (c=0;c<8;c++)
    {
      unsigned char n=c+1;
      unsigned char nc[2];

      utoa(n,nc,10);
      screen_puts(2,c+1,nc);
      
      if (hostSlots.host[c][0]!=0x00)
	screen_puts(4,c+1,hostSlots.host[c]);
      else
	screen_puts(4,c+1,"Empty");
    }

  // Display Device Slots
  diskulator_read_device_slots();
  
  screen_puts(20,9,"    DRIVE SLOTS    ");
  
  // Display drive slots
  for (c=0;c<8;c++)
    {
      unsigned char d[4];
      d[0]='D';
      d[1]=0x31+c;
      d[2]=':';
      d[3]=0x00;
      screen_puts(0,c+11,d);
      screen_puts(4,c+11,deviceSlots.slot[c].file[0]!=0x00 ? deviceSlots.slot[c].file : "Empty");
    }

 rehosts:
  // reset cursor
  c=0;

 rehosts_jump:
  screen_puts(0,20,"\xD9\xB2\xA5\xB4\xB5\xB2\xAE\x19Pick\xD9\xA5\x19" "Edit\xD9\xA4\x19" "Drives\xD9\xAF\xB0\xB4\xA9\xAF\xAE\x19" "Boot");
  screen_puts(0,21," \xD9\x91\x8D\x98\x19" "Drive Slots \xD9\xB3\xA8\xA9\xA6\xB4\x80\x91\x8D\x98\x19Host Slots ");
  
  bar_clear();
  bar_show(2);
  
  while (host_done==false)
    {
      // Quick boot
      if (GTIA_READ.consol==0x03)
	{
	  diskulator_mount_all_hosts();
	  diskulator_mount_all_devices();
	  diskulator_boot();
	}
      
      if (kbhit())
	{
	  k=cgetc();
	  switch(k)
	    {
	    case 0x1C: // UP
	    case '-':
	      if (c>0)
		c--;
	      break;
	    case 0x1D: // DOWN
	    case '=':
	      if (c<8)
		c++;
	      break;
	    case 0x21: // SHIFT 1-8
	    case 0x22:
	    case 0x23:
	    case 0x24:
	    case 0x25:
	    case 0x26:
	    case 0x27:
	      c=k-0x21;
	      break;
	    case 0x31:
	    case 0x32:
	    case 0x33:
	    case 0x34:
	    case 0x35:
	    case 0x36:
	    case 0x37:
	    case 0x38:
	      c=k-0x31;
	      goto jump_to_devs;
	      break;
	    case 0x40: // special case for 8
	      c=7;
	      break;
	    case 'e': // edit
	      if (hostSlots.host[c][0]==0x00)
		{
		  screen_puts(3,c+1,"                                    ");
		}
	      screen_input(3,c+1,hostSlots.host[c]);
	      if (hostSlots.host[c][0]==0x00)
		{
		  screen_puts(4,c+1,"Empty");
		}
	      diskulator_write_host_slots();
	      break;
	    case 'd':
	    jump_to_devs:
	      host_done=true;
	      slot_done=false;
	      screen_puts(0,20,"        \xD9\xAA\x19" "Eject\xD9\xA9\x19Hosts\xD9\xAE\x19New          ");
	      
	      break;
	    case 0x9B: // ENTER
	      selected_host=c;
	      
	      // Write hosts
	      diskulator_write_host_slots();
	      
	      // Mount host
	      diskulator_mount_host(c);
	      
	      host_done=true;
	      slot_done=true;
	      break;
	    }
	  if (k>0)
	    {
	      bar_clear();
	      bar_show(c+2);
	      k=0;
	    }
	}
      prev_consol=GTIA_READ.consol;
    }

  bar_clear();
  if (slot_done==false)
    bar_show(13);

  bar_clear();
  bar_show(c+13);
  
  c=0;
  
  while (slot_done==false)
    {
      // Quick boot
      if (GTIA_READ.consol==0x03)
	{
	  diskulator_mount_all_hosts();
	  diskulator_mount_all_devices();
	  diskulator_boot();
	}

      if (kbhit())
	{
	  k=cgetc();
	  switch(k)
	    {
	    case 0x1C: // UP
	    case '-':
	      if (c>0)
		c--;
	      break;
	    case 0x1D: // DOWN
	    case '=':
	      if (c<8)
		c++;
	      break;
	    case 0x21:
	    case 0x22:
	    case 0x23:
	    case 0x24:
	    case 0x25:
	    case 0x26:
	    case 0x27:
	      c=k-0x21;
	      slot_done=true;
	      host_done=false;
	      goto rehosts_jump;
	      break;
	    case 0x31:
	    case 0x32:
	    case 0x33:
	    case 0x34:
	    case 0x35:
	    case 0x36:
	    case 0x37:
	    case 0x38:
	      c=k-0x31;
	      break;
	    case 0x40: // special case for 8
	      c=7;
	      slot_done=true;
	      host_done=false;
	      goto rehosts_jump;
	      break;
	    case 'h': // Hosts
	      slot_done=true;
	      host_done=false;
	      goto rehosts;
	    case 'j': // EJECT
	      screen_puts(4,c+11,"Empty                               ");
	      memset(deviceSlots.slot[c].file,0,sizeof(deviceSlots.slot[c].file));
	      deviceSlots.slot[c].hostSlot=0xFF;
	      diskulator_write_device_slots();
	      break;
	    }
	  if (k>0)
	    {
	      bar_clear();
	      bar_show(c+13);
	      k=0;
	    }
	}      
      prev_consol=GTIA_READ.consol;
    }
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

  // If we select an empty host, bail back to host selection.
  if (hostSlots.host[selected_host][0]==0x00)
    return;
  
  POKE(0x60F,2);
  POKE(0x610,2);
  POKE(0x61B,6);
  POKE(0x61C,6);
  
  screen_clear();
  bar_clear();

  screen_puts(0,0,"    DISK IMAGES    ");

  screen_puts( 0,21,"ret PICK esc ABORT");
  screen_puts(20,21,"                  ");

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
      OS.dcb.dtimlo=0x01;
      OS.dcb.dbyt=0;
      OS.dcb.daux=selected_host;
      siov();
      
      e=0;
      while (selector_done==false)
	{
	  if (kbhit())
	    k=cgetc();
	  
	  switch(k)
	    {
	    case 0x1C: // Up
	    case '-':
	      if (e>0)
		e--;
	      break;
	    case 0x1D: // Down
	    case '=':
	      if (e<num_entries)
		e++;
	      break;
	    case 0x1B: // ESC
	      selector_done=true;
	      memset(path,0,sizeof(path));
	      bar_set_color(0x97);
	      return;
	    case 0x9B: // Enter
	      selector_done=true;
	      bar_set_color(0x97);
	      memset(path,0,sizeof(path));
	      strcpy(path,files[e]);
	      return;
	    }
	  if (k>0)
	    {
	      bar_clear();
	      bar_show(e+3);
	      k=0;
	    }
	}
    }
}

/**
 * Select destination drive
 */
void diskulator_drive(void)
{
  unsigned char c,k;
  bool drive_done=false;

  // If nothing is selected, simply return.
  if ((path[0]==0x00) || (hostSlots.host[selected_host][0]==0x00))
    return;
  
  POKE(0x60F,2);
  POKE(0x610,2);
  POKE(0x61B,6);
  POKE(0x61C,6);
  
  screen_clear();
  bar_clear();
  
  screen_puts(0,0,"MOUNT TO DRIVE SLOT");
  screen_puts( 0,21," 1-8 OR return PICK ");
  screen_puts(20,21," esc ABORT  e EJECT ");
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

  bar_clear();
  bar_show(c+3);
  
  while (drive_done==false)
    {
      k=cgetc();
      switch(k)
	{
	case 0x1C: // UP
	case '-':
	  if (c>0)
	    c--;
	  break;
	case 0x1D: // DOWN
	case '=':
	  if (c<8)
	    c++;
	  break;
	case 'e':  // E
	  screen_puts(4,c+2,"Empty                               ");
	  memset(deviceSlots.slot[c].file,0,sizeof(deviceSlots.slot[c].file));
	  deviceSlots.slot[c].hostSlot=0xFF;
	  diskulator_write_device_slots();
	  break;
	case 0x1B: // ESC
	  drive_done=true;
	  break;
	case 0x31:
	case 0x32:
	case 0x33:
	case 0x34:
	case 0x35:
	case 0x36:
	case 0x37:
	case 0x38:
	  c=k-0x31;
	  bar_clear();
	  bar_show(c+3);
	  goto rorw;
	  break;
	case 0x9B: // RETURN
	rorw:
	  screen_puts( 0,21,"       ENTER:       ");
	  screen_puts(20,21,"r R/O w R/W esc ABRT");

	  o=0;
	  
	  k=cgetc();

	  if (k=='r')
	    o|=0x01;
	  else if (k=='w')
	    o|=0x02;
	  else if (k==0x1B)
	    goto drive_slot_abort;
	  
	  deviceSlots.slot[c].hostSlot=selected_host;
	  deviceSlots.slot[c].mode=o;
	  strcpy(deviceSlots.slot[c].file,path);

	  diskulator_write_device_slots();
	  diskulator_mount_device(c,o);
	drive_slot_abort:
	  drive_done=true;
	  break;
	}
      if (k>0)
	{
	  bar_clear();
	  bar_show(c+3);
	  k=0;
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
