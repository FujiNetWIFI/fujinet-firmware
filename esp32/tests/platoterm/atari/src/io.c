/**
 * PLATOTERM for Atari Cartridges
 *
 * Author: Thomas Cherryhomes <thom.cherryhomes at gmail dot com>
 *
 * I/O Functions
 */

#include <serial.h>
#include <stdbool.h>
#include <atari.h>
#include <stdlib.h>
#include "io.h"
#include "protocol.h"
#include "sio.h"

extern bool running;

static uint8_t recv_buffer[8192];
static uint8_t xmit_buffer[64];
static uint8_t xmit_buffer_len=0;
static uint8_t status[4];

extern padPt TTYLoc;

static unsigned char hostname[256]="N:TCP://irata.online:8005/";
static unsigned short bw=0;
static unsigned char connected=0;

unsigned char trip=0;
extern void ih(void);

uint8_t xoff_enabled=false;

/**
 * io_init() - Set-up the I/O
 */
void io_init(void)
{
  OS.vprced=ih;
  PIA.pactl |= 1;
  // Establish connection
  OS.dcb.ddevic=0x71;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd='O';
  OS.dcb.dstats=0x80;
  OS.dcb.dbuf=&hostname;
  OS.dcb.dtimlo=0x0f;
  OS.dcb.dbyt=256;
  OS.dcb.daux=0;
  siov();
}

/**
 * io_send_byte(b) - Send specified byte out
 */
void io_send_byte(uint8_t b)
{
  xmit_buffer[xmit_buffer_len++]=b;
}

/**
 * io_main() - The IO main loop
 */
void io_main(void)
{
  if (xmit_buffer_len>0)
    {
      OS.dcb.ddevic=0x71;
      OS.dcb.dunit=1;
      OS.dcb.dcomnd='W';
      OS.dcb.dstats=0x80;
      OS.dcb.dbuf=&xmit_buffer;
      OS.dcb.dtimlo=0x0f;
      OS.dcb.dbyt=xmit_buffer_len;
      OS.dcb.daux1=xmit_buffer_len;
      OS.dcb.daux2=0;
      siov();
      
      xmit_buffer_len=0;
      return;
    }
  
  if (trip==0)
    return;
  
  // Get # of bytes waiting
  OS.dcb.ddevic=0x71;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd='S';
  OS.dcb.dstats=0x40;
  OS.dcb.dbuf=&status;
  OS.dcb.dtimlo=0x0f;
  OS.dcb.dbyt=4;
  OS.dcb.daux=0;
  siov();

  bw=(status[1]<<8)+status[0];
  connected=status[2];
  
  // These functions are all I needed to change to port over to the N: device.
  
  if (bw>0)
    {
      // Do a read into into recv buffer and ShowPLATO
      OS.dcb.ddevic=0x71;
      OS.dcb.dunit=1;
      OS.dcb.dcomnd='R';
      OS.dcb.dstats=0x40;
      OS.dcb.dbuf=&recv_buffer;
      OS.dcb.dbyt=bw;
      OS.dcb.daux=bw;
      siov();
      ShowPLATO((padByte *)recv_buffer, bw);
      bw=trip=0;
    }

  if (connected==0)
    {
      io_done();
    }
  
  PIA.pactl |= 1;
}

/**
 * io_done() - Called to close I/O
 */
void io_done(void)
{
  ShowPLATO("\r\nDISCONNECTED.\r\n",17);
  OS.dcb.ddevic=0x71;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd='C';
  OS.dcb.dstats=0x00;
  OS.dcb.dbuf=0;
  OS.dcb.dbyt=0;
  OS.dcb.daux=0;
  siov();
}
