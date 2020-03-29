/**
 * FujiNet Configuration Program
 */

#include <atari.h>
#include <stdbool.h>
#include <string.h>
#include <peekpoke.h>
#include "bar.h"
#include "screen.h"
#include "config.h"
#include "diskulator.h"

extern unsigned char* video_ptr;
extern unsigned char* dlist_ptr;
extern unsigned short screen_memory;
extern unsigned char* font_ptr;

unsigned char fontPatch[40]={
			 0,0,0,0,0,0,3,51,
			 0,0,3,3,51,51,51,51,
			 48,48,48,48,48,48,48,48,
			 0,120,135,255,255,255,255,0,
			 0x00, 0x78, 0x87, 0xff, 0xff, 0xff, 0xff, 0x00
};

void config_dlist=
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
   0x600,
   0,0,0,0
  };

/**
 * Setup the config screen
 */
void setup(void)
{
  OS.color0=0x9C;
  OS.color1=0x0F;
  OS.color2=0x92;
  OS.color4=0x92;
  OS.coldst=1;
  OS.sdmctl=0; // Turn off screen
  memcpy((void *)DISPLAY_LIST,&config_dlist,sizeof(config_dlist)); // copy display list to $0600
  OS.sdlst=(void *)DISPLAY_LIST;                     // and use it.
  dlist_ptr=(unsigned char *)OS.sdlst;               // Set up the vars for the screen output macros
  screen_memory=PEEKW(560)+4;
  video_ptr=(unsigned char*)(PEEKW(screen_memory));

  // Copy ROM font
  memcpy((unsigned char *)FONT_MEMORY,(unsigned char *)0xE000,1024);

  // And patch it.
  font_ptr=(unsigned char*)FONT_MEMORY;
  memcpy(&font_ptr[520],&fontPatch,sizeof(fontPatch));

  OS.chbas=0x78; // use the charset
  bar_clear();
  bar_setup_regs();
  
}

void main(void)
{
  setup();

  if (configured()==false)
    config_run();
  else
    config_connect();

  diskulator_run();  
}
