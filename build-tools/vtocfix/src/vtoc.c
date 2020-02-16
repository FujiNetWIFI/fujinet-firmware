/**
 * Routine for writing Mr. Robot VTOC
 * essentially blocks off sectors 0-214
 */

#include <atari.h>
#include <_atarios.h>
#include <stdlib.h>
#include <6502.h>
#include <string.h>
#include "vtoc.h"
#include "sio.h"

static VTOC vtoc;

void vtoc_write(unsigned char drive_num)
{
  memset(&vtoc.bitmap,0xFF,90); // Fill the bitmap as unused by default.
  
  vtoc.dos_code=0x02; // Dos 2
  vtoc.total_sectors=707; // 707 available sectors.
  vtoc.available_sectors=603; // 603 available sectors

  // Mark off Mr. Robot used sectors.
  vtoc.bitmap[0]=0x00;
  vtoc.bitmap[1]=0x00; 
  vtoc.bitmap[2]=0x00; 
  vtoc.bitmap[3]=0x00; 
  vtoc.bitmap[4]=0x00; 
  vtoc.bitmap[5]=0x00; 
  vtoc.bitmap[6]=0x00; 
  vtoc.bitmap[7]=0x00; 
  vtoc.bitmap[8]=0x00; 
  vtoc.bitmap[9]=0x00; 
  vtoc.bitmap[10]=0x00; 
  vtoc.bitmap[11]=0x00; 
  vtoc.bitmap[12]=0x00; 
  vtoc.bitmap[13]=0x00; 
  vtoc.bitmap[14]=0x00; 
  vtoc.bitmap[15]=0x00; 
  vtoc.bitmap[16]=0x00; 
  
  // Mark off VTOC sectors
  vtoc.bitmap[45]=0x00;
  vtoc.bitmap[46]=0x7F;

  vtoc.bitmap[89]=0xFF; // Block off configuration sectors.
  
  OS.dcb.ddevic=0x31; // Disk drive
  OS.dcb.dunit=drive_num;
  OS.dcb.dcomnd='P'; // Write, no verify
  OS.dcb.dbuf=&vtoc;
  OS.dcb.dbyt=128;
  OS.dcb.daux=360;    // VTOC sector
  OS.dcb.dtimlo=0x0F; // Default timeout
  OS.dcb.dstats=0x80; // Write
  siov();
}
