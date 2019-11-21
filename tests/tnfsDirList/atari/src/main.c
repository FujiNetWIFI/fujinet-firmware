/**
 * Test program #12 - TNFS Dir list
 */

#include <atari.h>
#include <6502.h>
#include <stdio.h>

unsigned char buf[256];
int i;
int num_entries;

/**
 * Main
 */
void main(void)
{
  printf("AtariWiFi Test Program #11\n");
  printf("TNFS Directory List\n\n");

  printf("Opening directory...");

  buf[0]='/';
  buf[1]=0x00;
  
  OS.dcb.ddevic=0x70;          // Network device
  OS.dcb.dunit=1;              // Unit 1
  OS.dcb.dcomnd='$';           // TNFS Dir open
  OS.dcb.dstats=0x80;          // Write
  OS.dcb.dbuf=&buf;
  OS.dcb.dtimlo=0x80;          // Timeout
  OS.dcb.dbyt=256;             // path size
  OS.dcb.daux=0;               // no aux

  asm("CLI");
  asm("JSR $E459"); // Do SIOV call

  if (OS.dcb.dstats==138)
    {
      printf("No Fujinet!\n\n");
      return;
    }
  
  printf ("\n\nReading Directory...\n\n");
   
  while (OS.dcb.dstats==0x01)
    {
      OS.dcb.ddevic=0x70;          // Network device
      OS.dcb.dunit=1;              // Unit 1
      OS.dcb.dcomnd='%';           // TNFS Read
      OS.dcb.dstats=0x40;          // Write
      OS.dcb.dbuf=&buf;
      OS.dcb.dtimlo=0x80;          // Timeout
      OS.dcb.dbyt=36;             // path size
      OS.dcb.daux=0;               // next entry.

      asm("CLI");
      asm("JSR $E459"); // Do SIOV call

      printf("%s\n",buf);      
    }

  printf("Closing Directory...\n\n");
  
  // Close dir.
  OS.dcb.ddevic=0x70;          // Network device
  OS.dcb.dunit=1;              // Unit 1
  OS.dcb.dcomnd='^';           // TNFS Dir open
  OS.dcb.dstats=0x00;          // no bytes
  OS.dcb.dbuf=&buf;
  OS.dcb.dtimlo=0x80;          // Timeout
  OS.dcb.dbyt=0;             // path size
  OS.dcb.daux=0;               // no aux

  asm("CLI");
  asm("JSR $E459"); // Do SIOV call

}
