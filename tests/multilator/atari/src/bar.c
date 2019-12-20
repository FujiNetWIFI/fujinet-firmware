/**
 * FujiNet Configurator
 *
 * Functions to display a selection bar
 */

#include <atari.h>
#include <string.h>
#include "bar.h"

unsigned char* bar_pmbase=(unsigned char *)0x3800;

/**
 * Clear bar from screen
 */
void bar_clear(void)
{
  memset(bar_pmbase,0,1024);
}

/**
 * Show bar at Y position
 */
void bar_show(unsigned char y)
{
  unsigned char scrpos=((y<<2)+16);

  bar_clear();
  
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

/**
 * Set bar color
 */
void bar_set_color(unsigned char c)
{
  OS.pcolr0=OS.pcolr1=OS.pcolr2=OS.pcolr3=c;
}
