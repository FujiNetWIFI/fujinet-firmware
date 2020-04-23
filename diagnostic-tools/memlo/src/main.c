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
#include "sio.h"
#include "conio.h"
#include "err.h"

void main(void)
{
  unsigned char tmp[5];
  
  OS.lmargn=2;

  print("\x9b");
  itoa(OS.memlo,tmp,16);
  print("MEMLO: $");
  print(tmp);
  print("\x9b\x9b");    
}
