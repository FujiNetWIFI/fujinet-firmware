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

unsigned char c;
unsigned char files[16][36];
unsigned char diskulator_done=false;

extern unsigned char* video_ptr;
extern unsigned char* dlist_ptr;
extern unsigned short screen_memory;
extern unsigned char* font_ptr;

union
{
  unsigned char host[8][32];
  unsigned char rawData[256];
} hostSlots;

/**
 * Enter a diskulator Host
 */
void diskulator_host(void)
{ 
  screen_clear();
  bar_clear();

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

  screen_puts(0,0,"   TNFS HOST LIST   ");
  
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

  bar_show(3);

  die();
}

/**
 * Select an image
 */
void diskulator_select(void)
{
}

/**
 * Select destination drive
 */
void diskulator_drive(void)
{
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
  while (diskulator_done==false)
    {
      diskulator_host();
      diskulator_select();
      diskulator_drive();
    }
  diskulator_boot();
}
