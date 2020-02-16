/**
 * FujiNet Tools for CLI
 *
 * Error output
 *
 * Author: Thomas Cherryhomes
 *  <thom.cherryhomes@gmail.com>
 *
 * Released under GPL, see COPYING
 * for details
 */

#include <atari.h>
#include "conio.h"

const char error_138[]="FUJINET NOT RESPONDING\x9B";
const char error_139[]="FUJINET NAK\x9b";
const char error_144[]="FUJINET COMPLETE WITH ERROR\x9B";
const char error[]="SIO ERROR\x9b"; 

/**
 * Show error
 */
void err_sio(void)
{
  switch (OS.dcb.dstats)
    {
    case 138:
      print(error_138);
      break;
    case 139:
      print(error_139);
      break;
    case 144:
      print(error_144);
      break;
    default:
      print(error);
      break;
    }
}
