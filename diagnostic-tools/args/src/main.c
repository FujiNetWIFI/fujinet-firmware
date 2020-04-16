/**
 * Diagnostic tools
 *
 * args - show cli args
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

unsigned char buf[40];

void args(int argc, char* argv[])
{
  unsigned char i;
  unsigned char tmp[5];
  
  for (i=0;i<argc;i++)
    {
      itoa(i,tmp,10);
      print(tmp);
      print(": ");
      print(argv[i]);
      print("\x9b");
    }
}

int main(int argc, char* argv[])
{
  unsigned char daux1;

  if (_is_cmdline_dos())
    {
      args(argc,argv);
    }
  else
    {
      // Dos 2
      print("CLI DOS ONLY.\x9b\x9b");
    }
  
  return(0);
}
