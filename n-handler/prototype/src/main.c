/**
 * http-fetch CIO driver
 */

#include <atari.h>
#include <6502.h>
#include <stdio.h>
#include <string.h>
#include "cio.h"
#include "conio.h"
#include "config.h"

devhdl_t devhdl;

extern void cio_open(void);
extern void cio_close(void);
extern void cio_get(void);
extern void cio_put(void);
extern void cio_status(void);
extern void _cio_status_poll(void);
extern void cio_special(void);
extern void intr(void);

const char banner_error[]="#FUJINET ERROR\x9b";
const char banner_ready[]="#FUJINET READY\x9b";

unsigned char ret;
unsigned char err;
unsigned char buffer_rx[MAX_DEVICES][256];
unsigned char buffer_tx[MAX_DEVICES][256];
unsigned char buffer_rx_len[MAX_DEVICES];
unsigned char buffer_tx_len[MAX_DEVICES];
unsigned char* rp; // receive ptr
unsigned char* tp; // transmit ptr
unsigned char trip=0;
void main(void)
{
  unsigned char i;

  OS.lmargn=2;  
  // Populate a devhdl table for our new N: device.
  devhdl.open        = (char *)cio_open-1;
  devhdl.close       = (char *)cio_close-1;
  devhdl.get         = (char *)cio_get-1;
  devhdl.put         = (char *)cio_put-1;
  devhdl.status      = (char *)cio_status-1;
  devhdl.special     = (char *)cio_special-1;

  // find next hatabs entry
  for (i=0;i<11;i++)
    if (OS.hatabs[i].id==0x00)
      break;

  // And inject our handler table into its slot.
  OS.hatabs[i].id='N';         // N: device
  OS.hatabs[i].devhdl=&devhdl; // handler table for N: device.

  _cio_status_poll();

  if (err==1)
    print(banner_ready);
  else
    print(banner_error);
  
  // Manually setting memlo, is there a symbol that can get me actual top of data?
  OS.memlo=(void *)0x3C00;
}
