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
#include "diskulator.h"
#include "screen.h"
#include "sio.h"
#include "bar.h"
#include "die.h"

unsigned char hostname[256];
unsigned char c;
unsigned char files[16][36];

extern unsigned char* video_ptr;
extern unsigned char* dlist_ptr;
extern unsigned short screen_memory;
extern unsigned char* font_ptr;

/**
 * Enter a diskulator Host
 */
void diskulator_host(void)
{ 
  screen_clear();
  bar_clear();
  memset(&hostname,0,sizeof(hostname));
  screen_puts( 0,0,"   ENTER HOSTNAME   ");
  screen_puts(21,0,"  OF TNFS SERVER   ");

  screen_puts(0,1,">");
  screen_input(1,1,hostname);

  // Mount host
  OS.dcb.ddevic=0x70;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd=0xF9;
  OS.dcb.dstats=0x80;
  OS.dcb.dbuf=&hostname;
  OS.dcb.dtimlo=0x0f;
  OS.dcb.dbyt=256;
  OS.dcb.daux=0;
  siov();
}

/**
 * Select an image
 */
void diskulator_select(void)
{
  unsigned short page=0;
  unsigned short offset=0;
  unsigned char num_entries;
  unsigned char k=0;
  bool in_selector;
  unsigned char e=0;
  
  while (1)
    {
      screen_clear();
      screen_puts(0,0,"FILE LIST ON:");
      screen_puts(20,0,hostname);
      screen_puts(24,21,"SELECT IMAGE");

      // Open Dir
      memcpy(&hostname,0x00,sizeof(hostname));
      strcpy(hostname,"/");
      OS.dcb.ddevic=0x70;
      OS.dcb.dunit=1;
      OS.dcb.dcomnd=0xF7;
      OS.dcb.dstats=0x80;
      OS.dcb.dbuf=&hostname;
      OS.dcb.dtimlo=0x0F;
      OS.dcb.dbyt=256;
      OS.dcb.daux=(page<<4);
      siov();

      num_entries=0;
      
      // loop and read dir
      while ((hostname[0]!=0x7F))
	{
	  memset(hostname,0,sizeof(hostname));
	  hostname[0]=0x7f;
	  OS.dcb.ddevic=0x70;
	  OS.dcb.dunit=1;
	  OS.dcb.dcomnd=0xF6;
	  OS.dcb.dstats=0x40;
	  OS.dcb.dbuf=&hostname;
	  OS.dcb.dtimlo=0x0F;
	  OS.dcb.dbyt=36;
	  OS.dcb.daux=36;
	  siov();
	  
	  if (hostname[0]=='.')
	    continue;
	  else if (hostname[0]==0x7F)
	    break;
	  else
	    {
	      strcpy(files[num_entries],hostname);
	      screen_puts(0,num_entries+2,hostname);
	      num_entries++;
	    }
	  
	}

      // Close dir read
      OS.dcb.ddevic=0x70;
      OS.dcb.dunit=1;
      OS.dcb.dcomnd=0xF5;
      OS.dcb.dstats=0x00;
      OS.dcb.dbuf=&hostname;
      OS.dcb.dtimlo=0x0F;
      OS.dcb.dbyt=0;
      OS.dcb.daux=0;
      siov();

      if (k==0x1C)
	e=0;
      else if (k==0x1D)
	e=num_entries;
      else
	e=0;

      in_selector=true;
      while (in_selector==true)
	{
	  bar_clear();
	  bar_show(e+3);
	  k=cgetc();
	  switch(k)
	    {
	    case 0x9B:  // EOL
	      OS.pcolr0=OS.pcolr1=OS.pcolr2=OS.pcolr3=0x97;
	      in_selector==false;
	      memset(hostname,0,sizeof(hostname));
	      strcpy(hostname,files[e]);
	      OS.dcb.ddevic=0x70;
	      OS.dcb.dunit=1;
	      OS.dcb.dcomnd=0xF8;
	      OS.dcb.dstats=0x80;
	      OS.dcb.dbuf=&hostname;
	      OS.dcb.dtimlo=0x0f;
	      OS.dcb.dbyt=256;
	      OS.dcb.daux=0;
	      siov();

	      if (OS.dcb.dstats==0x01)
		OS.pcolr0=OS.pcolr1=OS.pcolr2=OS.pcolr3=0x52;

	      OS.rtclok[0]=OS.rtclok[1]=OS.rtclok[2]=0;
	      
	      while (OS.rtclok[2]<64) { }
	      return;
	      break;
	    case 0x1C:  // UP
	      if (e>0)
		e--;
	      else
		{
		  if (page>0)
		    {
		      page--;
		      in_selector=false;
		    }
		}
	      break;
	    case 0x1D:  // DOWN
	      if (e<num_entries)
		e++;
	      else
		{
		  if (num_entries<16)
		    {
		    }
		  else
		    {
		      page--;
		      in_selector=false;
		    }
		}
	      break;
	    }
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
 * Run the Diskulator
 */
void diskulator_run(void)
{
  diskulator_host();
  diskulator_select();
  diskulator_boot();
}
