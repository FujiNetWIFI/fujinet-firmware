/**
 * FujiNet Configuration Program
 *
 * Screen functions
 */

#include <string.h>
#include <conio.h>
#include "screen.h"

unsigned char* video_ptr;
unsigned char* dlist_ptr;
unsigned short screen_memory;
unsigned char* font_ptr;

void screen_clear()
{
  memset(video_ptr,0,GRAPHICS_0_SCREEN_SIZE);
}

/**
 * Print ATASCII string to display memory
 */
void screen_puts(unsigned char x,unsigned char y,char *s)
{
  char offset;
  
  do
    {     
      if (*s < 32)
	{
	  offset=64;
	}
      else if (*s<96)
	{
	  offset=-32;
	}
      else
	{
	  offset=0;
	}
      
      SetChar(x++,y,*s+offset);

      ++s;
      
    } while(*s!=0);
}

/**
 * Input a string at x,y
 */
int screen_input(unsigned char x, unsigned char y, char* s)
{
  unsigned char c,k,o;
  unsigned char outc[2]={0,0};
  unsigned char q;
  
  o=0;
  c=x;

  for (q=0;q<strlen(s);q++)
    if (s[q]==0x00)
      break;
    else
      {
	c++;
	o++;
      }
  
  SetChar(c+1,y,0x80); // turn on cursor

  k=0;
  
  while (k!=155)
    {
      k=cgetc();

      if ( k== 0x1B ) // ESC key to cancel
        return -1;

      if ((k==0x7E) && (c>x)) // backspace
	{
	  SetChar(c+1,y,0);
	  s[--o]=0;
	  c--;
	  SetChar(c+1,y,0x80);
	}
      else if (k==0x9b) // return (EOL)
	{
	  SetChar(c+1,y,GetChar(c+1,y)&0x7F);
	  // exit while.
	}
      else if (((k>0x20) && (k<0x7b) || (k==0x20))) // printable ascii
	{
	  SetChar(c+1,y,GetChar(c+1,y)&0x7F);
	  outc[0]=k;
	  screen_puts(c+1,y,outc);
	  SetChar(c+2,y,0x80);
	  s[o++]=k;
	  c++;
	}
    }
}
