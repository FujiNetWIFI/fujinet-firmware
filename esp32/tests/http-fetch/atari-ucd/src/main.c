/**
 * ucd - Set the base URL for N:
 *
 * Author: Thomas Cherryhomes
 *  <thom.cherryhomes@gmail.com>
 *
 * Licensed under GPL v3, see
 * LICENSE for details.
 */

#include <atari.h>
#include <string.h>
#include <stdlib.h>
#include <peekpoke.h>
#include "sio.h"
#include "conio.h"
#include "err.h"

unsigned char url[80];

void set_base_url(void)
{
  OS.dcb.ddevic=0x70;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd=0xE3; // Set Base URL
  OS.dcb.dstats=0x80;
  OS.dcb.dbuf=&url;
  OS.dcb.dtimlo=0x0f;
  OS.dcb.dbyt=sizeof(url);
  OS.dcb.daux=0;
  siov();
  
  if (OS.dcb.dstats!=1)
    {
      err_sio();
      exit(OS.dcb.dstats);
    }
}

int main(int argc, char* argv[])
{
  unsigned char i;

  OS.lmargn=2;
  
  if (_is_cmdline_dos() && argc>1)
    strcpy(url,argv[1]);
  else
    {
      // DOS 2.0
      print("\x9b");
      print("ENTER BASE URL OR \xA0\xD2\xC5\xD4\xD5\xD2\xCE\xA0 TO CLEAR?\x9b");
      get_line(url,sizeof(url));

      // remove EOL
      for (i=0;i<sizeof(url);i++)
	if (url[i]==0x9b)
	  url[i]=0x00;
    }

  set_base_url();

  print("N: = ");
  print(url);
  print("\x9b");
  return(0);
}
