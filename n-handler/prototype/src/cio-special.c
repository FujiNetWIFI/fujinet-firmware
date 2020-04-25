/**
 * CIO Special (XIO) call
 */

#include <atari.h>
#include <6502.h>
#include <string.h>
#include "sio.h"
#include "misc.h"
#include "config.h"

extern unsigned char err;
extern unsigned char ret;
extern unsigned char buffer_tx[MAX_DEVICES][256];

unsigned char inq_dstats;

void _cio_special(void)
{ 
  // First determine DSTATS for command

  err=siov(DEVIC_N,
	   OS.ziocb.drive,
	   0xFF, // DSTATS inquiry
	   0x40, // Read
	   &inq_dstats,
	   1,    // 1 byte
	   DTIMLO_DEFAULT,
	   OS.ziocb.command, // Requested command
	   0);   // not used

  if (err!=1) // inquiry failed?
    {
      ret=err=OS.dcb.dstats;
      return;
    }
  else if (inq_dstats==0xFF)
    {
      ret=err=146; // CIO command not implemented.
      return;
    }
  
  ret=err=siov(DEVIC_N, OS.ziocb.drive,
	       OS.ziocb.command,
	       inq_dstats,
	       (inq_dstats==0x00 ? NULL : buffer_tx[OS.ziocb.drive-1]),	   
	       256,
	       DTIMLO_DEFAULT,
	       OS.ziocb.aux1,
	       OS.ziocb.aux2);
}
