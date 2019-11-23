/**
 * Diskulator prototype
 */

#include <conio.h>
#include <atari.h>
#include <stdlib.h>
#include <string.h>
#include <6502.h>
#include <unistd.h>

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
  OS.dcb.dunit=1;
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

  clrscr();
  
  // Open the directory.
  OS.dcb.ddevic=0x70;          // Network device
  OS.dcb.dunit=1;              // Unit 1
  OS.dcb.dcomnd='$';           // TNFS Dir open
  OS.dcb.dstats=0x80;          // Write
  OS.dcb.dbuf=&buf;
  OS.dcb.dtimlo=0x80;          // Timeout
  OS.dcb.dbyt=256;             // path size
  siov();
  
  while (buf[0]!=0x7F)
    {
      memset(&buf,0,36);
      OS.dcb.dcomnd='%';           // TNFS Read
      OS.dcb.dstats=0x40;          // Write
      OS.dcb.dtimlo=0x80;          // Timeout
      OS.dcb.dbyt=36;             // path size
      siov();

      if (buf[0]=='.')
	continue;
      else if (buf[0]==0x7F)
	break;
      
      cputs(buf);
      cputs("\r\n");
      
      i++;
      
      if (i>15)
	{
	  cputs("<RET> to Select, any other key next page.");

	  while (!kbhit()) { }

	  ch=cgetc();

	  if (ch==0x9B)
	    break;
	  else
	    {
	      i=0;
	      clrscr();
	    }
	}
    }

  // Close the directory.
  OS.dcb.dcomnd='^';           // TNFS Dir close
  OS.dcb.dstats=0x00;          // no bytes
  OS.dcb.dtimlo=0x80;          // Timeout
  OS.dcb.dbyt=0;             // path size
  siov();

  memset(&buf,0,sizeof(buf));
  
  cputs("Filename: ");
  cscanf("%s",buf);

  strcpy(buf,"jumpman.atr");
  
  // Mount the image
  OS.dcb.ddevic=0x70;
  OS.dcb.dunit=1;
  OS.dcb.dcomnd='M';
  OS.dcb.dstats=0x80;
  OS.dcb.dbuf=&buf;
  OS.dcb.dtimlo=0x1f;
  OS.dcb.dbyt=256;
  siov();

  cputs("\r\nRebooting...");
  sleep(2);
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
