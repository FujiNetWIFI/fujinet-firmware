/**
 * http-fetch CIO driver
 */

#include <atari.h>
#include <6502.h>
#include <stdio.h>
#include <string.h>
#include "cio.h"

devhdl_t devhdl;
unsigned char packet[256];


extern void cio_open(void);
extern void cio_close(void);
extern void cio_get(void);
extern void cio_put(void);
extern void cio_status(void);
extern void cio_special(void);

unsigned char ret;
unsigned char err;
long filesize;

void cio_init(void)
{
}

void main(void)
{
  unsigned char i;
  // Populate a devhdl table for our new N: device.
  devhdl.open        = (char *)cio_open-1;
  devhdl.close       = (char *)cio_close-1;
  devhdl.get         = (char *)cio_get-1;
  devhdl.put         = (char *)cio_put-1;
  devhdl.status      = (char *)cio_status-1;
  devhdl.special     = (char *)cio_special-1;
  devhdl.init        = cio_init;

  // find next hatabs entry
  for (i=0;i<11;i++)
    {
      if (OS.hatabs[i].id==0x00)
	break;
    }

  // And inject our handler table into its slot.
  OS.hatabs[i].id='N';         // N: device
  OS.hatabs[i].devhdl=&devhdl; // handler table for N: device.

  // Manually setting memlo, is there a symbol that can get me actual top of data?
  OS.memlo=(void *)0x279E;
}
