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
#include "opts.h"
#include "net.h"
#include "list.h"
#include "host.h"
#include "disk.h"

unsigned char path[256]="/";

union
{
  char host[8][32];
  unsigned char rawData[256];
} hostSlots;

union
{
  struct
  {
    unsigned char hostSlot;
    unsigned char mode;
    char file[36];
  } slot[8];
  unsigned char rawData[304];
} deviceSlots;

int main(int argc, char* argv[])
{
  unsigned char r=0;
  
  if (!_is_cmdline_dos())
    {
      wrong_dos(); // in opts.c
      return(1);
    }
  
  if (argc==1)
    {
      printf("\n%s\n",argv[0]);
      opts();
      return(1);
    }

  OS.rtclok[2]=OS.rtclok[1]=OS.rtclok[0]=0;

  while (OS.rtclok[2]<16) { }

  printf("\n");
  
  if (argv[1][0]=='N')
    r=net(argc,argv);
  else if (argv[1][0]=='L' && argv[1][1]=='H')
    r=list_host_slots();
  else if (argv[1][0]=='L' && argv[1][1]=='D')
    r=list_device_slots();
  else if (argv[1][0]=='H')
    r=host(argc,argv);
  else if (argv[1][0]=='L' && argv[1][1]=='S')
    r=list_directory(argc,argv);
  else if (argv[1][0]=='M')
    r=disk_mount(argc,argv);
  else if (argv[1][0]=='E')
    r=disk_eject(argc,argv);
  
  return(r);
}
