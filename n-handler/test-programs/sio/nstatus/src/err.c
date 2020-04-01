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
const char error[]="SIO ERROR\x9b"; 

/**
 * Show network error
 */
void err_net(void)
{
  switch(OS.dvstat[3])
    {
    case 0:
      print("OK");
      break;
    case 128:
      print("NOT CONNECTED");
      break;
    case 129:
      print("COULD NOT ALLOC BUFFERS");
      break;
    case 130:
      print("COULD NOT OPEN PROTOCOL");
      break;
    case 165:
      print("INVALID DEVICESPEC");
      break;
    case 170:
      print("COULD NOT CONNECT.");
      break;
    }
}

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
    default:
      print(error);
      break;
    }
}
