/**
 * WIFISCAN - show WiFi networks returned
 * from a SIO WIFI SCAN call
 */

#include <atari.h>
#include <6502.h>
#include <string.h>
#include <peekpoke.h>

#define SetChar(x,y,a) video_ptr[(x)+(y)*40]=(a);
#define GetChar(x,y) video_ptr[(x)+(y)*40]
#define GRAPHICS_0_SCREEN_SIZE (40*25)
#define DISPLAY_LIST 0x0600
#define DISPLAY_MEMORY 0x3C00
#define FONT_MEMORY 0x3400

union 
{
  struct
  {
    char ssid[32];
    signed char rssi;
    char more;
  };
  unsigned char rawData[34];
} ssidInfo;

void dlist=
  {
   DL_BLK8,
   DL_BLK8,
   DL_BLK8,
   DL_LMS(DL_CHR20x8x2),
   DISPLAY_MEMORY,

   DL_CHR20x8x2,
   DL_CHR40x8x1,
   DL_CHR40x8x1,
   DL_CHR40x8x1,
   DL_CHR40x8x1,
   DL_CHR40x8x1,
   DL_CHR40x8x1,
   DL_CHR40x8x1,
   DL_CHR40x8x1,
   DL_CHR40x8x1,
   DL_CHR40x8x1,
   DL_CHR40x8x1,
   DL_CHR40x8x1,
   DL_CHR40x8x1,
   DL_CHR40x8x1,
   DL_CHR40x8x1,
   DL_CHR40x8x1,
   DL_CHR40x8x1,
   DL_CHR40x8x1,
   DL_CHR40x8x1,
   DL_CHR40x8x1,
   DL_CHR20x8x2,
   DL_CHR20x8x2,   
   DL_JVB,
   0x600
  };

static unsigned char* video_ptr;
static unsigned char* dlist_ptr;
static unsigned short screen_memory;
static unsigned char* font_ptr;

const char* title="   WIFI NETWORKS:   ";
const char* scan="SCANNING NETWORKS...";

unsigned char fontPatch[24]={
			 0,0,0,0,0,0,3,51,
			 0,0,3,3,51,51,51,51,
			 48,48,48,48,48,48,48,48
};

void clear_screen()
{
  memset(video_ptr,0,GRAPHICS_0_SCREEN_SIZE);
}

/**
 * Print ATASCII string to display memory
 */
void print_string(unsigned char x,unsigned char y,char *s)
{
  unsigned char inverse;
  char offset;
  
  do
    {
      inverse=*s&0x80;
      *s-=inverse;
      
      if (*s < 32)
	{
	  offset=64;
	}
      else if (*s<96)
	{
	  offset=-32;
	}
      else
	{
	  offset=0;
	}
      
      *s+=inverse;
      SetChar(x++,y,*s+offset);

      ++s;
      
    } while(*s!=0);
}

/**
 * Setup screen
 */
void setup()
{
  OS.coldst=1;    // Reset coldstart flag to force coldstart on RESET.
  OS.sdmctl=0x00; // Turn off DMA
  memcpy((void *)DISPLAY_LIST,&dlist,sizeof(dlist)); // copy display list to $0600
  OS.sdlst=(void *)DISPLAY_LIST;                     // and use it.
  
  dlist_ptr=(unsigned char *)OS.sdlst;               // Set up the vars for the screen output macros
  screen_memory=PEEKW(560)+4;
  video_ptr=(unsigned char*)(PEEKW(screen_memory));

  // Copy ROM font
  memcpy((unsigned char *)FONT_MEMORY,(unsigned char *)0xE000,1024);

  // And patch it.
  font_ptr=(unsigned char*)FONT_MEMORY;
  memcpy(&font_ptr[520],&fontPatch,24);
  OS.chbas=0x34;
  
  // TODO: come back here and set some colors
  // OS.color0=0x12;
  // OS.color1=0x34;
  // OS.color2=0x56;
  // OS.color3=0x78;

  OS.sdmctl=0x22; // Turn on DMA, normal playfield, no P/M.

  clear_screen();
  print_string(0,0,(char *)title);
  print_string(0,21,(char *)scan);
}

/**
 * Do the WiFi Scan
 */
unsigned char sio_wifi_scan(unsigned char c)
{
  OS.dcb.ddevic=0x70;             // Network Device
  OS.dcb.dunit=1;                 //
  OS.dcb.dcomnd='#';              // Network Scan
  OS.dcb.dstats=0x40;             // Read from peripheral
  OS.dcb.dbuf=&ssidInfo.rawData;  // The scan results buffer.
  OS.dcb.dtimlo=0x20;             // Timeout
  OS.dcb.dbyt=34;                 // 330 byte payload
  OS.dcb.daux=c;                  // aux1=the continue flag

  asm("CLI");
  asm("JSR $E459");
  
  return OS.dcb.dstats;  // Return command status
}

/**
 * Print the ssid at index i
 */
void print_ssid(unsigned char i)
{
  char out[3]="0:";
  out[0]=i+0x30; // Turn number into printable number.
  print_string(1,i+3,out);
  print_string(4,i+3,(char *)ssidInfo.ssid);
}


/**
 * Print the RSSI at index i
 */
void print_rssi(unsigned char i)
{
  char out[4]={0x20,0x20,0x20};

  if (ssidInfo.rssi>-40)
    {
      out[0]=0x01;
      out[1]=0x02;
      out[2]=0x03;
    }
  else if (ssidInfo.rssi>-60)
    {
      out[0]=0x01;
      out[1]=0x02;
    }
  else
    {
      out[0]=0x01;
    }

  print_string(35,i+3,out);
}

/**
 * Print the available networks
 */
void print_networks(void)
{
  unsigned char done;
  unsigned char i=0;
  do 
    {
      if (ssidInfo.ssid!=0x00) // Only print if not empty.
	{
	  print_ssid(i);
	  print_rssi(i);
	}

      if (ssidInfo.more==0)
	done=1;
      else
	{
	  i++;
	  sio_wifi_scan(1);
	}
      
    } while (done==0);
    
  print_string(0,21,"  SCAN SUCCESSFUL!  ");
}

/**
 * Print error
 */
void print_error(unsigned char s)
{
  if (s==138)
    {
      print_string(0,21,"  NO FUJINET FOUND  ");
    }
  else if (s==139)
    {
      print_string(0,21,"    FUJINET NAK!    ");
    }
}

/**
 * Sit and wait
 */
void sit_and_spin(void)
{
  for (;;) { }
}

/**
 * main program
 */
void main(void)
{
  unsigned char s; // status
  setup();

  SEI();
  
  s=sio_wifi_scan(0);

  if (s==1)
    {
      print_networks();
    }
  else
    {
      print_error(s);
    }

  sit_and_spin();
}
