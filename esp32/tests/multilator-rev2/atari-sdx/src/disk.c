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
#include "disk.h"
#include "sio.h"
#include "error.h"
#include "host.h"

extern unsigned char path[256];

extern union
{
  char host[8][32];
  unsigned char rawData[256];
} hostSlots;

extern union
{
  struct
  {
    unsigned char hostSlot;
    unsigned char mode;
    char file[36];
  } slot[8];
  unsigned char rawData[304];
} deviceSlots;

/**
 * Read Device Slots
 */
void disk_read(void)
{
  // Read Drive Tables
  OS.dcb.ddevic=0x70;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd=0xF2;
  OS.dcb.dstats=0x40;
  OS.dcb.dbuf=&deviceSlots.rawData;
  OS.dcb.dtimlo=0x0f;
  OS.dcb.dbyt=sizeof(deviceSlots.rawData);
  OS.dcb.daux=0;
  siov();
}

/**
 * Write device slots
 */
void disk_write(void)
{
  OS.dcb.ddevic=0x70;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd=0xF1;
  OS.dcb.dstats=0x80;
  OS.dcb.dbuf=&deviceSlots.rawData;
  OS.dcb.dtimlo=0x0f;
  OS.dcb.dbyt=sizeof(deviceSlots.rawData);
  OS.dcb.daux=0;
  siov();
}

/**
 * Mount device slot
 */
void _disk_mount(unsigned char c, unsigned char o)
{
  OS.dcb.ddevic=0x70;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd=0xF8;
  OS.dcb.dstats=0x00;
  OS.dcb.dbuf=NULL;
  OS.dcb.dtimlo=0x01;
  OS.dcb.dbyt=0;
  OS.dcb.daux1=c;
  OS.dcb.daux2=o;
  siov();
  if (OS.dcb.dstats!=1)
    {
      sio_error();
      exit(OS.dcb.dstats);
    }
}

/**
 * Mount disk in device slot
 */
unsigned char disk_mount(int argc, char* argv[])
{
  unsigned char ds=atoi(argv[2]);
  unsigned char hs=atoi(argv[3]);
  unsigned char m=(argv[4][0]=='W' ? 0x03 : 0x01);
  
  if (argc<5)
    {
      printf("\n%s M <DS#> <HS#> <R|W> <FNAME>\n",argv[0]);
      return 1;
    }

  if ((ds<1) || (ds>8))
    {
      printf("\nINVALID DRIVE SLOT NUMBER.\n");
      return 1;
    }

  if ((hs<1) || (hs>8))
    {
      printf("\nINVALID HOST SLOT NUMBER.\n");
      return 1;
    }

  // Adjust slot indexes
  ds-=1;
  hs-=1;
  
  // Read Host Slots
  host_read();
  
  // Read device slots
  disk_read();

  deviceSlots.slot[ds].hostSlot=hs;
  deviceSlots.slot[ds].mode=m;
  strcpy(deviceSlots.slot[ds].file,argv[5]);

  // Write device slot again.
  disk_write();

  // mount the host
  host_mount(deviceSlots.slot[ds].hostSlot);

  // mount the disk image
  _disk_mount(ds,m);

  printf("\nOK\n");
  return 0;
}

/**
 * Eject disk in device slot
 */
unsigned char disk_eject(int argc, char* argv[])
{
  unsigned char ds=atoi(argv[2]);

  if (argc<3)
    {
      printf("\n%s E <DS#>\n",argv[0]);
      return 1;
    }

  if ((ds<1) || (ds>8))
    {
      printf("\nINVALID DRIVE SLOT NUMBER.\n");
      return 1;
    }

  // Read Host Slots
  host_read();
  
  // Read device slots
  disk_read();

  // Adjust slot index
  ds-=1;
  
  deviceSlots.slot[ds].hostSlot=0xFF; // Eject disk

  // Write device slot again.
  disk_write();

  printf("\nOK\n");
  return 0;
}
