/**
 * player/missile bar test
 */

#include <atari.h>
#include <conio.h>
#include <string.h>

extern void bar_setup_regs(void);

char* bar_pmbase=(unsigned char *)0x3400;

void bar_show(unsigned char y)
{
  unsigned char scrpos=((y<<2)+12);

  memset(bar_pmbase,0,1024);
  
  // First the missiles (rightmost portion of bar)
  bar_pmbase[384+scrpos+0]=0xFF;
  bar_pmbase[384+scrpos+1]=0xFF;
  bar_pmbase[384+scrpos+2]=0xFF;
  bar_pmbase[384+scrpos+3]=0xFF;

  // Then the players (p0)
  bar_pmbase[512+scrpos+0]=0xFF;
  bar_pmbase[512+scrpos+1]=0xFF;
  bar_pmbase[512+scrpos+2]=0xFF;
  bar_pmbase[512+scrpos+3]=0xFF;

  // P1
  bar_pmbase[640+scrpos+0]=0xFF;
  bar_pmbase[640+scrpos+1]=0xFF;
  bar_pmbase[640+scrpos+2]=0xFF;
  bar_pmbase[640+scrpos+3]=0xFF;

  // P2
  bar_pmbase[768+scrpos+0]=0xFF;
  bar_pmbase[768+scrpos+1]=0xFF;
  bar_pmbase[768+scrpos+2]=0xFF;
  bar_pmbase[768+scrpos+3]=0xFF;

  // P3
  bar_pmbase[896+scrpos+0]=0xFF;
  bar_pmbase[896+scrpos+1]=0xFF;
  bar_pmbase[896+scrpos+2]=0xFF;
  bar_pmbase[896+scrpos+3]=0xFF;  
}

void main(void)
{
  char y,newy;
  char ch;
  bar_setup_regs();

  while (1)
    {
      if (kbhit())
	{
	  ch=cgetc();

	  if (ch==0x1C && y>0) // up
	    newy=y-1;
	  else if (ch==0x1D && y<24) // down
	    newy=y+1;

	  y=newy;
	  bar_show(y);
	}
    }
}
