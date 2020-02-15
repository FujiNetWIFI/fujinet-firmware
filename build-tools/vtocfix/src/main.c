/**
 * vtocfix - Fix VTOC to block off first 96 sectors, and last four sectors
 *
 * Author:
 *  Thomas Cherryhomes <thom.cherryhomes@gmail.com>
 *
 */

#include <atari.h>
#include <stdlib.h>
#include <peekpoke.h>
#include "conio.h"
#include "vtoc.h"
#include "sio.h"
#include "cio.h"

unsigned char buf[8];

void main(void)
{
  OS.lmargn=2;
  print ("\x9b");
  print("#FUJINET VTOCFIX 0.1\x9b\x9b");
  print("THIS PROGRAM RESERVES THE REQ'D\x9b");
  print("SECTORS IN THE VTOC FOR CONFIG\x9b\x9b");
  print("INSERT A BLANK FORMATTED DISK\x9b");
  print("IN D1 AND PRESS RETURN\x9b");
  get_line(buf,sizeof(buf));

  vtoc_write(1);

  if (OS.dcb.dstats==0x01)
    {
      print("VTOC WRITTEN.\x9b");
    }
  else
    {
      print("DISK ERROR.\x9b");
    }
}
