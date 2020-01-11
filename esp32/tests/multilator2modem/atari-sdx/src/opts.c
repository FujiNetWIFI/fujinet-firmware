/**
 * Config for SpartaDOS/DOS XL
 *
 * Author: Thom Cherryhomes
 *  <thom.cherryhomes@gmail.com>
 *
 * Licensed under GPL v. 3
 * See COPYING for details
 */

#include <stdio.h>
#include <atari.h>
#include "opts.h"

/**
 * Show options
 */
void opts(void)
{
  printf("  N [SSID] [PASSWORD]  SET WIFI NETWORK\n");
  printf("\n");
  printf("  M <DS#> <HS#> <R|W> <FNAME> MOUNT ATR\n");
  printf("    DS#   - DEVICE SLOT NUMBER (1-8)\n");
  printf("    HS#   - HOST SLOT NUMBER (1-8)\n");
  printf("    R|W   - R/O or R/W\n");
  printf("    FNAME - FILENAME\n");
  printf("\n");
  printf("  E <DS#> - EJECT DISK IN DEVICE SLOT\n");
  printf("\n");
  printf("  H <HS#> <HOSTNAME> - SET HOST SLOT\n");
  printf("\n");
  printf("  LH - LIST HOST SLOTS\n");
  printf("  LD - LIST DEVICE SLOTS\n");
  printf("\n");
  printf("  LS <HS#> - LIST FILES ON HOST");
}

/**
 * Shown when DOS isn't cmdline
 */
void wrong_dos(void)
{
  printf("\n\nThis program is not for DOS 2 or MYDOS\n");
  printf("Exiting...\n");
  OS.rtclok[0]=OS.rtclok[1]=OS.rtclok[2]=0;
  while (OS.rtclok[1]<1) { }
}
