/**
 * Diskulator prototype
 */

#include <conio.h>
#include <atari.h>
#include <stdlib.h>
#include <string.h>
#include <6502.h>

unsigned char buf[256];

void siov(void)
{
  asm("JSR $E459");
}

void select_host(void)
{
  clrscr();
  cputs("----------------------------------------");
  cputs("Welcome to #FujiNet Test #15 Diskulator ");
  cputs("----------------------------------------");
  cputs("\r\n\r\n");
  cputs("Hostname: ");
  cursor(1);
  cscanf("%s",buf);

  cputs("\r\nConnecting...");
  
  OS.dcb.ddevic=0x70;
  OS.dcb.dcomnd='H';
  OS.dcb.dstats=0x80;
  OS.dcb.dbuf=&buf;
  OS.dcb.dtimlo=0x1f;
  OS.dcb.dbyt=256;
  siov();

  if (OS.dcb.dstats==138)
    {
      cputs("Timed out!\r\n");
      exit(0);
    }

  cputs("Connected!\r\n");
}

void select_image(void)
{
  unsigned char i=0;
  unsigned char ch;
  
  buf[0]=0x00; // guard char

  // Open the directory.
  OS.dcb.ddevic=0x70;          // Network device
  OS.dcb.dunit=1;              // Unit 1
  OS.dcb.dcomnd='$';           // TNFS Dir open
  OS.dcb.dstats=0x80;          // Write
  OS.dcb.dbuf=&buf;
  OS.dcb.dtimlo=0x80;          // Timeout
  OS.dcb.dbyt=256;             // path size
  siov();
  
  while (buf[0]!=0xFF)
    {
      clrscr();
      memset(&buf,0,36);
      OS.dcb.dcomnd='%';           // TNFS Read
      OS.dcb.dstats=0x40;          // Write
      OS.dcb.dtimlo=0x80;          // Timeout
      OS.dcb.dbyt=36;             // path size
      siov();

      cputs(buf);
      
      i++;
      
      if (buf[0]=='.')
	continue;
      else if (buf[0]==0xFF)
	break;

      if (i>15)
	{
	  cputs("<ESC> to Select, any other key to Continue");

	  while (!kbhit()) { }

	  ch=cgetc();

	  if (ch==0x1B)
	    break;
	  else
	    i=0;
	}
    }

  // Close the directory.
  OS.dcb.dcomnd='^';           // TNFS Dir close
  OS.dcb.dstats=0x00;          // no bytes
  OS.dcb.dtimlo=0x80;          // Timeout
  OS.dcb.dbyt=0;             // path size
  siov();
  
  cputs("Filename: ");
  cscanf("%s",buf);

  // Mount the image
  OS.dcb.dcomnd='M';           // mount image
  OS.dcb.dstats=0x80;          // Write
  OS.dcb.dbuf=&buf;
  OS.dcb.dtimlo=0x80;          // Timeout
  OS.dcb.dbyt=256;             // path size
  siov();
}

void reboot(void)
{
  asm("JMP $E477");
}

void main(void)
{
  select_host();
  select_image();
  reboot();
}
