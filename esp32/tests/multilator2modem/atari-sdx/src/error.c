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

/**
 * Print error
 */
void sio_error(void)
{
  printf("SIO ERROR- %d\n\n",OS.dcb.dstats);
}
