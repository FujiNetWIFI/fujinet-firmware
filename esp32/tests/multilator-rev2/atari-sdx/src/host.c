/**
 * Config for SpartaDOS/DOS XL
 *
 * Author: Thom Cherryhomes
 *  <thom.cherryhomes@gmail.com>
 *
 * Licensed under GPL v. 3
 * See COPYING for details
 */

#include <atari.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "host.h"
#include "sio.h"
#include "error.h"

extern union
{
  char host[8][32];
  unsigned char rawData[256];
} hostSlots;

/**
 * Mount a host slot
 */
void host_mount(unsigned char c)
{
  if (hostSlots.host[c][0]!=0x00)
    {
      OS.dcb.ddevic=0x70;
      OS.dcb.dunit=1;
      OS.dcb.dcomnd=0xF9;
      OS.dcb.dstats=0x00;
      OS.dcb.dbuf=NULL;
      OS.dcb.dtimlo=0x01;
      OS.dcb.dbyt=0;
      OS.dcb.daux=c;
      siov();
    }
}

/**
 * Read host slots
 */
void host_read(void)
{
  // Query for host slots
  OS.dcb.ddevic=0x70;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd=0xF4; // Get host slots
  OS.dcb.dstats=0x40;
  OS.dcb.dbuf=&hostSlots.rawData;
  OS.dcb.dtimlo=0x0f;
  OS.dcb.dbyt=256;
  OS.dcb.daux=0;
  siov();

  if (OS.dcb.dstats!=1)
    {
      sio_error();
      exit(OS.dcb.dstats);
    }
}

/**
 * Write host slots
 */
void host_write(void)
{
  OS.dcb.ddevic=0x70;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd=0xF3;
  OS.dcb.dstats=0x80;
  OS.dcb.dbuf=&hostSlots.rawData;
  OS.dcb.dtimlo=0x0f;
  OS.dcb.dbyt=256;
  OS.dcb.daux=0;
  siov();
  
  if (OS.dcb.dstats!=1)
    {
      sio_error();
      exit(OS.dcb.dstats);
    }
}

/**
 * Host Slot Config
 */
unsigned char host(int argc, char* argv[])
{
  unsigned char s=atoi(argv[2]);

  if (argc<3)
    {
      printf("\n%s H <HS#> [HOSTNAME]\n",argv[0]);
      return 1;
    }
  
  if ((s<1) || (s>8))
    {
      printf("\nINVALID SLOT NUMBER.\n");
      return 1;
    }

  // 0 index.
  s-=1;
  
  // Read the host slots
  host_read();

  // If empty argument, clear host slot.
  if (argc==3)
    memset(hostSlots.host[s],0,sizeof(hostSlots.host[s]));
  
  // copy in the new entry.
  strcpy(hostSlots.host[s],argv[3]);

  // write out the host slots.
  host_write();

  // Mount the host
  host_mount(s);
  
  // Write out ok
  printf("\nOK\n");
  return 0;
}

