/**
 * PLATOTERM for Atari Cartridges
 *
 * Author: Thomas Cherryhomes <thom.cherryhomes at gmail dot com>
 *
 * Screen Functions
 */

#include <peekpoke.h>
#include <tgi.h>
#include <string.h>
#include "screen.h"
#include "font.h"

extern void screen_beep(void);
extern uint16_t mul0625(uint16_t val);
extern uint16_t mul0375(uint16_t val);
extern void RenderGlyph(void);
extern const uint8_t font[];
extern uint8_t fontm23[];
extern uint8_t FONT_SIZE_X;
extern uint8_t FONT_SIZE_Y;

unsigned char CharWide=8;
unsigned char CharHigh=16;
padPt TTYLoc;
unsigned short cx;
unsigned char cy;
unsigned char CharCode;
unsigned char Flags;
unsigned char* GlyphData;
static short offset;

static const char s1[]="PLATOTERM 1.3 - No Touch 16K";
static const char s2[]="(C) 2019 IRATA.ONLINE";
static const char s3[]="TERMINAL READY";

/**
 * screen_init() - Set up the screen
 */
void screen_init(void)
{
  tgi_install(tgi_static_stddrv);
  tgi_init();
  tgi_clear();
}

/**
 * screen_splash
 */
void screen_splash(void)
{
  ShowPLATO((padByte *)s1,sizeof(s1));
  TTYLoc.x=0; TTYLoc.y-=16; ShowPLATO((padByte *)s2,sizeof(s2));
  TTYLoc.x=0; TTYLoc.y-=16; ShowPLATO((padByte *)s3,sizeof(s3));
  TTYLoc.x=0; TTYLoc.y-=32;
}

/**
 * screen_wait(void) - Sleep for approx 16.67ms
 */
void screen_wait(void)
{
}

/**
 * screen_clear - Clear the screen
 */
void screen_clear(void)
{
  tgi_clear();
}

/**
 * screen_set_pen_mode()
 * Set the pen mode based on CurMode.
 */
void screen_set_pen_mode(void)
{
  if (CurMode==ModeErase || CurMode==ModeInverse)
    tgi_setcolor(0);
  else
    tgi_setcolor(1);
}

/**
 * screen_block_draw(Coord1, Coord2) - Perform a block fill from Coord1 to Coord2
 */
void screen_block_draw(padPt* Coord1, padPt* Coord2)
{
  screen_set_pen_mode();
  tgi_bar(mul0625(Coord1->x),mul0375(Coord1->y^0x1FF),mul0625(Coord2->x),mul0375(Coord2->y^0x1FF));
}

/**
 * screen_dot_draw(Coord) - Plot a mode 0 pixel
 */
void screen_dot_draw(padPt* Coord)
{
  screen_set_pen_mode();
  tgi_setpixel(mul0625(Coord->x),mul0375(Coord->y^0x1FF));
}

/**
 * screen_line_draw(Coord1, Coord2) - Draw a mode 1 line
 */
void screen_line_draw(padPt* Coord1, padPt* Coord2)
{
  screen_set_pen_mode();
  tgi_line(mul0625(Coord1->x),mul0375(Coord1->y^0x1FF),mul0625(Coord2->x),mul0375(Coord2->y^0x1FF));
}

/**
 * screen_char_draw(Coord, ch, count) - Output buffer from ch* of length count as PLATO characters
 */
void screen_char_draw(padPt* Coord, unsigned char* ch, unsigned char count)
{
  unsigned char i;
  unsigned char dx=5;
  Flags=0;
  
  switch(CurMem)
    {
    case M0:
      GlyphData=(unsigned char *)font;
      offset=-32;
      break;
    case M1:
      GlyphData=(unsigned char *)font;
      offset=64;
      break;
    case M2:
      GlyphData=fontm23;
      offset=-32;
      break;
    case M3:
      GlyphData=fontm23;
      offset=32;      
      break;
    }

  if (CurMode==ModeInverse)
    Flags|=0x80;
  else if (CurMode==ModeWrite)
    Flags|=0x20;
  else if (CurMode==ModeErase)
    Flags|=0x10;
  
  if (ModeBold)
    {
      dx<<=1;
      Flags|=0x40;
    }
  cx=mul0625((Coord->x&0x1FF));

  if (ModeBold)
  cy=mul0375((Coord->y+30^0x1FF)&0x1FF);
  else
  cy=mul0375((Coord->y+15^0x1FF)&0x1FF);

  for (i=0;i<count;++i)
    {
      CharCode=ch[i]+offset;
      RenderGlyph();
      cx+=dx;
    }
}

/**
 * screen_tty_char - Called to plot chars when in tty mode
 */
void screen_tty_char(padByte theChar)
{
  if ((theChar >= 0x20) && (theChar < 0x7F)) {
    screen_char_draw(&TTYLoc, &theChar, 1);
    TTYLoc.x += CharWide;
  }
  else if ((theChar == 0x0b)) /* Vertical Tab */
    {
      TTYLoc.y += CharHigh;
    }
  else if ((theChar == 0x08) && (TTYLoc.x > 7))	/* backspace */
    {
      TTYLoc.x -= CharWide;

      tgi_setcolor(0);
      tgi_bar(mul0625(TTYLoc.x),mul0375(TTYLoc.y^0x1FF),mul0625(TTYLoc.x+CharWide),mul0375((TTYLoc.y+CharHigh)^0x1FF));
      tgi_setcolor(1);
    }
  else if (theChar == 0x0A)			/* line feed */
    TTYLoc.y -= CharHigh;
  else if (theChar == 0x0D)			/* carriage return */
    TTYLoc.x = 0;
  
  if (TTYLoc.x + CharWide > 511) {	/* wrap at right side */
    TTYLoc.x = 0;
    TTYLoc.y -= CharHigh;
  }
  
  if (TTYLoc.y < 0) {
    tgi_clear();
    TTYLoc.y=495;
  }
}

/**
 * screen_done()
 * Close down TGI
 */
void screen_done(void)
{
  tgi_done();
  tgi_uninstall();
  POKE(82,2);        // Reset left margin
  POKE(559,34);      // Reset ANTIC DMA to OS default
  POKE(764,255);     // Clear keyboard buffer
  POKE(0xD000,0);    // Move all missiles off screen.
  POKE(0xD001,0);    
  POKE(0xD002,0);
  POKE(0xD003,0);
  POKE(0xD01D,0);    // Tell GTIA no more players/missiles.
}
