/**
 * CIO Get call
 */

#include <atari.h>
#include <6502.h>
#include "sio.h"

extern unsigned char err;
extern unsigned char ret;
extern unsigned char packet[512];

extern void _cio_status(void);

void _cio_get_chr(void)
{ 
}

void _cio_get_rec(void)
{
}

void _cio_get(void)
{  
  switch(OS.ziocb.command)
    {
    case 0x05: // GETREC
      _cio_get_rec();
      break;
    case 0x07: // GETCHR
      _cio_get_chr();
      break;
    }  
}
