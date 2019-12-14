/**
 * PLATOTerm64 - A PLATO Terminal for the Commodore 64
 * Based on Steve Peltz's PAD
 * 
 * Author: Thomas Cherryhomes <thom.cherryhomes at gmail dot com>
 *
 * terminal.c - Terminal state functions
 */

/* Some functions are intentionally stubbed. */
#pragma warn(unused-param, off)

#include <stdbool.h>
#include <string.h>
#include "terminal.h"
#include "screen.h"

/**
 * ASCII Features to return in Features
 */
#define ASC_ZFGT        0x01
#define ASC_ZPCKEYS     0x02
#define ASC_ZKERMIT     0x04
#define ASC_ZWINDOW     0x08

/**
 * protocol.c externals
 */
extern CharMem CurMem;
extern padBool TTY;
extern padBool ModeBold;
extern padBool Rotate;
extern padBool Reverse;
extern DispMode CurMode;
extern padBool FlowControl;

/**
 * screen.c externals
 */
extern uint8_t CharWide;
extern uint8_t CharHigh;
extern padPt TTYLoc;

/**
 * terminal_init()
 * Initialize terminal state
 */
void terminal_init(void)
{
  terminal_set_tty();
}

/**
 * terminal_initial_position()
 * Set terminal initial position after splash screen.
 */
void terminal_initial_position(void)
{
  TTYLoc.x=0;
  TTYLoc.y=100; // Right under splashscreen.
}

/**
 * terminal_set_tty(void) - Switch to TTY mode
 */
void terminal_set_tty(void)
{
  screen_clear();
  TTY=true;
  ModeBold=padF;
  Rotate=padF;
  Reverse=padF;
  CurMem=M0;
  /* CurMode=ModeRewrite; */
  CurMode=ModeWrite;    /* For speed reasons. */
  CharWide=8;
  CharHigh=16;
  TTYLoc.x = 0;        // leftmost coordinate on screen
  TTYLoc.y = 495;      // Top of screen - one character height
}

/**
 * terminal_set_plato(void) - Switch to PLATO mode
 */
void terminal_set_plato(void)
{
  TTY=false;
  screen_clear();
  CharWide=8;
  CharHigh=16;
}

#define FONTPTR(a) (((a << 1) + a) << 1)

// Temporary PLATO character data, 8x16 matrix
static unsigned char char_data[16];

const unsigned char BTAB[]={0x80,0x40,0x20,0x10,0x08,0x04,0x02,0x01}; // flip one bit on (OR)
const unsigned char BTAB_5[]={0x08,0x10,0x10,0x20,0x20,0x40,0x80,0x80}; // flip one bit on for the 5x6 matrix (OR)
const unsigned char TAB_0_5[]={0x05,0x05,0x05,0x04,0x04,0x04,0x03,0x03,0x02,0x02,0x01,0x01,0x01,0x00,0x00,0x00};
const unsigned char TAB_0_5i[]={0x00,0x00,0x00,0x01,0x01,0x01,0x02,0x02,0x03,0x03,0x04,0x04,0x04,0x05,0x05,0x05};
const unsigned char TAB_0_4[]={0x00,0x00,0x01,0x02,0x02,0x03,0x03,0x04}; // return 0..4 given index 0 to 7
const unsigned char TAB_0_25[]={0,5,10,15,20,25}; // Given index 0 of 5, return multiple of 5.

const unsigned char PIX_THRESH[]={0x03,0x02,0x03,0x03,0x02, // Pixel threshold table.
				   0x03,0x02,0x03,0x03,0x02,
				   0x02,0x01,0x02,0x02,0x01,
				   0x02,0x01,0x02,0x02,0x01,
				   0x03,0x02,0x03,0x03,0x02,
				   0x03,0x02,0x03,0x03,0x02};

static unsigned char PIX_WEIGHTS[30];


static unsigned char pix_cnt;     // total # of pixels
static unsigned char curr_word;   // current word
static unsigned char u,v;       // loop counters

extern unsigned char fontm23[768];

/**
 * terminal_char_load - Store a character into the user definable
 * character set.
 */
void terminal_char_load(padWord charnum, charData theChar)
{
  // Clear char data. 
  memset(char_data,0,sizeof(char_data));
  memset(PIX_WEIGHTS,0,sizeof(PIX_WEIGHTS));
  memset(&fontm23[FONTPTR(charnum)],0,6);
  pix_cnt=0;
  
  // Transpose character data.  
  for (curr_word=0;curr_word<8;curr_word++)
    {
      for (u=16; u-->0; )
	{
	  if (theChar[curr_word] & 1<<u)
	    {
	      pix_cnt++;
	      PIX_WEIGHTS[TAB_0_25[TAB_0_5[u]]+TAB_0_4[curr_word]]++;
	      char_data[u^0x0F&0x0F]|=BTAB[curr_word];
	    }
	}
    }

  // Determine algorithm to use for number of pixels.
  // Algorithm A is used when roughly half of the # of pixels are set.
  // Algorithm B is used either when the image is densely or sparsely populated (based on pix_cnt).
  if ((54 <= pix_cnt) && (pix_cnt < 85))
    {
      // Algorithm A - approx Half of pixels are set
      for (u=6; u-->0; )
  	{
  	  for (v=5; v-->0; )
  	    {
  	      if (PIX_WEIGHTS[TAB_0_25[u]+v] >= PIX_THRESH[TAB_0_25[u]+v])
  		fontm23[FONTPTR(charnum)+u]|=BTAB[v];
  	    }
  	}
    }
  else if ((pix_cnt < 54) || (pix_cnt >= 85))
    {
      // Algorithm B - Sparsely or heavily populated bitmaps

      // If densely set pixels, flip the bits.      
      for (u=16; u-->0; )
	{
	  if (pix_cnt >= 85)
	    char_data[u]^=0xFF;

	  for (v=8; v-->0; )
	    {
	      if (char_data[u] & (1<<v))
		{
		  fontm23[FONTPTR(charnum)+TAB_0_5i[u]]|=BTAB_5[v];
		}
	    }
	}

      if (pix_cnt >= 85)
      	{
      	  for (u=6; u-->0; )
      	    {
      	      fontm23[FONTPTR(charnum)+u]^=0xFF;
      	      fontm23[FONTPTR(charnum)+u]&=0xF8;
      	    }
      	}
    }
}
