/**
 * Diagnostic tools
 *
 * memlo - show cli memlo
 *
 * Author: Thomas Cherryhomes
 *  <thom.cherryhomes@gmail.com>
 *
 * Released under GPL 3.0
 * See COPYING for details.
 */

#include <atari.h>
#include <string.h>
#include <stdlib.h>
#include <peekpoke.h>
#include "conio.h"

unsigned char tmp[5];
unsigned short memlo;

void main(void)
{
  memlo=(*(unsigned*) (0x2E7));
  OS.lmargn=2;

  print("\x9b");
  itoa(memlo,tmp,16);
  print("MEMLO: $");
  print(tmp);
  print("\x9b\x9b");    
}
