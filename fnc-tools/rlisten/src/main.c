/**
 * FujiNet Tools for CLI
 *
 * rlisten - Enable TCP port XXXX for listening
 *
 * usage:
 *  rlisten <port#>
 *
 * Author: Thomas Cherryhomes
 *  <thom.cherryhomes@gmail.com>
 *
 * Released under GPL, see COPYING
 * for details
 */

#include <atari.h>
#include <string.h>
#include <stdlib.h>
#include "sio.h"
#include "conio.h"
#include "err.h"

unsigned char buf[40];
unsigned short port;

/**
 * SIO command to set R: listening port
 */
unsigned char listen_port(unsigned short port)
{
  OS.dcb.ddevic=0x50;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd='L';
  OS.dcb.dstats=0x00;
  OS.dcb.dbuf=NULL;
  OS.dcb.dtimlo=0x01;
  OS.dcb.dbyt=0;
  OS.dcb.daux=port;

  siov();

  if (OS.dcb.dstats!=1)
    err_sio();

  return OS.dcb.dstats;
}

/**
 * show options
 */
void opts(char* argv[])
{
  print(argv[0]);
  print(" <port#>\x9b\x9b");
  print("<port#> - TCP Port # (1-65535)\x9b");
}

/**
 * main
 */
int main(int argc, char* argv[])
{
  unsigned char err=0;
  
  OS.lmargn=2;
  
  if (_is_cmdline_dos())
    {
      if (argc<2)
	{
	  opts(argv);
	  return(1);
	}
      port=atoi(argv[1]);
    }
  else
    {
      // DOS 2.0/MYDOS
      print("\x9b");

      print("LISTEN TO WHICH PORT? ");
      get_line(buf,sizeof(buf));
      port=atoi(buf);
    }
  
  if (port<1)
    {
      print("INVALID PORT NUMBER.\x9b");
      return(1);
    }

  err=listen_port(port);
  
  if (!_is_cmdline_dos())
    {
      print("\x9bPRESS \xD2\xC5\xD4\xD5\xD2\xCE TO CONTINUE.\x9b");
      get_line(buf,sizeof(buf));
    }
  
  return(err);
}
