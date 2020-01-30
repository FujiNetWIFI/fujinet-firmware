/**
 * CIO test driver
 */

#include <atari.h>
#include <6502.h>

#include <stdio.h>
#include <string.h>

devhdl_t devhdl;
char* output=(unsigned char *)0x0600; // Output buffer in page 6.

void cio_open(void)
{
}

void cio_close(void)
{
}

void cio_get(void)
{
}

void cio_put(void)
{
}

void cio_status(void)
{
}

void cio_special(void)
{
  if (OS.ziocb.command==0xFE) // XIO 254
    {
      OS.dcb.ddevic=0x70;
      OS.dcb.dunit=1;
      OS.dcb.dcomnd='Y'; // example cmd
      OS.dcb.dstats=0x40; // Read from periph
      OS.dcb.dbuf=output;
      OS.dcb.dtimlo=0x0f; // Quick
      OS.dcb.dbyt=20;
      OS.dcb.daux=0;
      asm("JSR $E459");
      asm("LDY #$01");
    }
}

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
  OS.memlo=(void *)0x1FA1;
}
