/*
//                       PiRTO II Flash MultiCART by Andrea Ottaviani 2024
//
//  Intellivision flash multicart based on Raspberry Pico board -
//
//  More info on https://github.com/aotta/ 
//
//   parts of code are directly from the A8PicoCart project by Robin Edwards 2023
//  
//   Needs to be a release NOT debug build for the cartridge emulation to work
// 
//   Edit myboard.h depending on the type of flash memory on the pico clone//
//
//   v. 1.03 2025-09-04 : Added check maxfile 
//
*/

#define intydebug // for debug print on intyscreen

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/multicore.h"
#include "hardware/gpio.h"
#include "pico/platform.h"
#include "pico/stdlib.h"
#include "hardware/vreg.h"
#include "pico/divider.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"


#include "rom.h"

#include "tusb.h"
#include "ff.h"
#include "fatfs_disk.h"
#include "fuji_mailbox.h"
#include "fujibus_usb.h"


// Pico pin usage definitions

#define B0_PIN    0
#define B1_PIN    1
#define B2_PIN    2
#define B3_PIN    3
#define B4_PIN    4
#define B5_PIN    5
#define B6_PIN    6
#define B7_PIN    7
#define F0_PIN    8
#define F1_PIN    9
#define F2_PIN    10
#define F3_PIN    11
#define F4_PIN    12
#define F5_PIN    13
#define F6_PIN    14
#define F7_PIN    15
#define BDIR_PIN  16
#define BC2_PIN   17
#define BC1_PIN   18
#define MSYNC_PIN 19
#define RST_PIN   20
#define LED_PIN   25

// Pico pin usage masks

#define B0_PIN_MASK     0x00000001L // gpio 0
#define B1_PIN_MASK     0x00000002L
#define B2_PIN_MASK     0x00000004L
#define B3_PIN_MASK     0x00000008L
#define B4_PIN_MASK     0x00000010L
#define B5_PIN_MASK     0x00000020L
#define B6_PIN_MASK     0x00000040L
#define B7_PIN_MASK     0x00000080L

#define F0_PIN_MASK     0x00000100L
#define F1_PIN_MASK     0x00000200L
#define F2_PIN_MASK     0x00000400L
#define F3_PIN_MASK     0x00000800L
#define F4_PIN_MASK     0x00001000L
#define F5_PIN_MASK     0x00002000L
#define F6_PIN_MASK     0x00004000L
#define F7_PIN_MASK     0x00008000L  // gpio 15

#define BDIR_PIN_MASK   0x00010000L // gpio 16
#define BC2_PIN_MASK    0x00020000L // gpio 17
#define BC1_PIN_MASK    0x00040000L // gpio 18
#define MSYNC_PIN_MASK  0x00080000L // gpio 19
#define RST_PIN_MASK    0x00100000L // gpio 20
#define LED_PIN_MASK    0x02000000L // gpio 25

// Aggregate Pico pin usage masks
#define BC1e2_PIN_MASK  0x00060000L
#define BX_PIN_MASK     0x000000FFL
#define FX_PIN_MASK     0x0000FF00L
#define DATA_PIN_MASK   0x0000FFFFL
#define BUS_STATE_MASK  0x00070000L
#define ALL_GPIO_MASK  	0x021FFFFFL
#define ALWAYS_IN_MASK  (BUS_STATE_MASK)
#define ALWAYS_OUT_MASK (LED_PIN_MASK)


#define SET_DATA_MODE_OUT   gpio_set_dir_out_masked(DATA_PIN_MASK)
#define SET_DATA_MODE_IN    gpio_set_dir_in_masked(DATA_PIN_MASK)

#define resetLow()  gpio_set_dir(RST_PIN,true); gpio_put(RST_PIN,true); //  Pirto to INTV BUS ; RST Output set to 0
#define resetHigh() gpio_set_dir(RST_PIN,true); gpio_put(RST_PIN,false); // RST is INPUT; B->A, INTV BUS to TEENSY

// Inty bus values (BC1+BC2+BDIR) GPIO 18-17-16

#define BUS_NACT  0b000  //0
#define BUS_BAR   0b001  //1
#define BUS_IAB   0b010  //2
#define BUS_DWS   0b011  //3
#define BUS_ADAR  0b100  //4
#define BUS_DW    0b101  //5
#define BUS_DTB   0b110  //6
#define BUS_INTAK 0b111  //7


unsigned char busLookup[8];

char RBLo,RBHi;
#define BINLENGTH  1024*64 //65536L
#define RAMSIZE  0x2000
uint16_t ROM[BINLENGTH];

uint16_t RAM[RAMSIZE];
#define maxHacks 32
uint16_t HACK[maxHacks];
uint16_t HACK_CODE[maxHacks];

char curPath[256] = "";
char path[256];
unsigned char files[256*64] = {0};
unsigned char nomefiles[32*25] = {0};
int fileda=0,filea=0;
volatile char cmd=0;
char errorBuf[40];
bool cmd_executing=false;

unsigned int parallelBus2;
  
unsigned int romLen;
unsigned int ramfrom = 0;
unsigned int ramto =   0;
unsigned int mapfrom[80];
unsigned int mapto[80];
unsigned int maprom[80];
int mapdelta[80];
unsigned int mapsize[80];
unsigned int addrto[80];
unsigned int RAMused = 0;
unsigned int tipo[80];  // 0-rom / 1-page / 2-ram
unsigned int page[80];  // page number

char substr[100];

int slot;
int hacks;

int base=0x17f;

uint32_t selectedfile_size;    // BIN file size
char longfilename[60];         // long file name (trunked) 
char mapfilename[60];          // map cfg file name
char testfilename[60];         // for check if directory
char rewindfilename[60];       // for rewind browsing
char rewindprev[60];       // for rewind browsing
int lenfilename; 
char tiposcelta[9];
bool pressed=false;


////////////////////////////////////////////////////////////////////////////////////
//                     RESET
////////////////////////////////////////////////////////////////////////////////////
void resetCart() {
  gpio_init(MSYNC_PIN);
  gpio_set_dir(MSYNC_PIN,false);
  gpio_set_pulls(MSYNC_PIN,false,true);
  gpio_put(LED_PIN,false);
  resetHigh();
   sleep_ms(30);  // was 20 for Model II; 30 works for both
   resetLow();
  //sleep_ms(3);  // was 2 for Model II; 3 works for both
 
  //while ((gpio_get(MSYNC_PIN)==1)) ;
    
  sleep_ms(1);  // was 1 for Model II; 
 resetHigh();
 gpio_put(LED_PIN,true);
   
}


/*
 Theory of Operation
 -------------------
 Inty sends command to mcu on cart by writing to 50000 (CMD), 50001 (parameter) and menu (50002-50641) 
 Inty must be running from RAM when it sends a command, since the mcu on the cart will
 go away at that point. Inty polls 50001 until it reads $1.
*/

void __not_in_flash_func(core1_main()) {

  unsigned int lastBusState, busState1;
  unsigned int busSt1, busSt2;
  unsigned int parallelBus;
  unsigned int dataOut, dataInd;
  unsigned int dataWrite=0;
  unsigned char busBit;
  bool deviceAddress = false; 
  unsigned int curPage=0;
  unsigned int checpage=0;
  int exitBUS;

  // core1 runs the cycle-exact CP-1610 bus loop below and must never take an
  // interrupt or an exception into flash-resident code. Mask everything at
  // the NVIC and at PRIMASK; this is deliberately blunt and permanent for
  // the lifetime of this core. (No multicore_lockout_victim_init() either --
  // that installs a flash-resident IRQ handler on this core's SIO FIFO IRQ,
  // which we don't want live regardless of masking.)
  irq_set_mask_enabled(0xFFFFFFFFu, false);
  __asm volatile ("cpsid i");

  busy_wait_ms(480);

  busState1 = BUS_NACT;
  lastBusState = BUS_NACT;
  
  dataOut=0;

    gpio_set_dir_in_masked(ALWAYS_IN_MASK);
    gpio_set_dir_out_masked(ALWAYS_OUT_MASK);
 
    // Initial conditions
    SET_DATA_MODE_IN;
   
while(1) {
    // Wait for the bus state to change
  
    do {
    } while (!((gpio_get_all() ^ lastBusState) & BUS_STATE_MASK));
    // We detected a change, but reread the bus state to make sure that all three pins have settled
     lastBusState = gpio_get_all();

    busState1 = ((lastBusState & BUS_STATE_MASK)>> BDIR_PIN); //if gpio9    
   
    busBit = busLookup[busState1];
    // Avoiding switch statements here because timing is critical and needs to be deterministic
    if (!busBit)
    {
      // -----------------------
      // DTB
      // -----------------------
      // DTB needs to come first since its timing is critical.  The CP-1600 expects data to be
      // placed on the bus early in the bus cycle (i.e. we need to get data on the bus quickly!)
	    if (deviceAddress)
      {
        // The data was prefetched during BAR/ADAR.  There isn't nearly enough time to fetch it here.
        // We can just output it.
        SET_DATA_MODE_OUT;
        gpio_put_masked(DATA_PIN_MASK,dataOut);
        asm inline ("nop;nop;nop;nop;");
       // while ((gpio_get_all() & BC1_PIN_MASK)); // wait while bc1 & bc2 are high... it's enough test BC1
        while(((gpio_get_all() & BC1e2_PIN_MASK)>>BC2_PIN)==3);
        //asm inline (delWR); //150ns
        
              
       SET_DATA_MODE_IN;
      }
     }
    else
    {
      busBit >>= 1;
      if (!busBit)
      {
        // -----------------------
        // BAR, ADAR
        // -----------------------
	   if (busState1==BUS_ADAR) {
	     if (deviceAddress)  
      		{
        // The data was prefetched during BAR/ADAR.  There isn't nearly enough time to fetch it here.
        // We can just output it.
       	SET_DATA_MODE_OUT;
        gpio_put_masked(DATA_PIN_MASK,dataOut);
        
        while((gpio_get_all() & BC1_PIN_MASK)>>BC1_PIN); //wait BC1 go down 
        //asm inline (delWR); //150ns

         SET_DATA_MODE_IN;
        
      		}
	   }
      /// ELSE is BAR   
        // Prefetch data here because there won't be enough time to get it during DTB.
        // However, we can't take forever because of all the time we had to wait for
        // the address to appear on the bus.
        SET_DATA_MODE_IN;
       // We have to wait until the address is stable on the bus
        // waiting bus is stable 66 nop at 200mhz is ok/85 at 240
    
       while(((parallelBus=gpio_get_all()) & BDIR_PIN_MASK));  // wait DIR go low for finish BAR cycle 
       //asm inline (delRD); //150ns
    
       parallelBus = gpio_get_all()& 0xFFFF; 

       deviceAddress = false;
         
       // Load data for DTB here to save time
           for (int8_t i=0; i <= slot; i++) {
            if ((parallelBus - maprom[i]) <= mapsize[i]) {
              if (tipo[i]==0) {
                dataOut=ROM[(parallelBus - mapdelta[i])];
                deviceAddress = true;
                break;
              }
              if (tipo[i]==1) {
                if (page[i]==curPage) {
                  dataOut=ROM[(parallelBus - mapdelta[i])];
                  deviceAddress = true;
                  break;
                }
                if ((parallelBus & 0xfff)==0xfff) {
                  checpage=1;
                   deviceAddress = true;
                  break;
                }
              }
              if (tipo[i]==2) {
                dataOut=RAM[parallelBus - ramfrom];
                deviceAddress = true;
                break;
              } 
            }    
          }
  
        if (hacks>0) {
          for (int i=0; i<maxHacks;i++) {
            if (parallelBus==HACK[i]) {
              dataOut=HACK_CODE[i];
              deviceAddress = true;
	          }
            break;
          }
        } 
      }
      else
      {
         busBit >>= 1;
        if (!busBit)
        {
          // -----------------------
          // DWS
          // -----------------------
       
          if (deviceAddress) {

            SET_DATA_MODE_IN;
           
            dataWrite = gpio_get_all() & 0xFFFF; 
        
            if (RAMused == 1) {
             RAM[parallelBus-ramfrom]=dataWrite & 0xFF;
           } 
           if ((checpage == 1) && (((dataWrite >> 4) & 0xff) == 0xA5)) {
              curPage=dataWrite & 0xf;
              checpage=0;
           }
           
        }
        else
        {
         // -----------------------
         // NACT, IAB, DW, INTAK
         // -----------------------
         // reconnect to bus
           parallelBus2=parallelBus;
           SET_DATA_MODE_IN;
        }
        
        }
      } 
    }    
  }
} 




////////////////////////////////////////////////////////////////////////////////////
//                     Error(N)
////////////////////////////////////////////////////////////////////////////////////
void error(int numblink){
  while(1){
	gpio_set_dir(25,GPIO_OUT);
    
    for(int i=0;i<numblink;i++) {
      gpio_put(25,true);
      sleep_ms(600);
      gpio_put(25,false);
      sleep_ms(500);
    }
  sleep_ms(2000);
  }
}

/////////////////////////////////
// print on mfile on inty
////////////////////////////////
void printInty(char *temp) {
  for (int i=0;i<120;i++) RAM[0x17f+i]=0;
  for (int i=0;i<60;i++) {
    RAM[0x17f+i*2]=temp[i];
    if (temp[i]==0) {
      RAM[0x17f+i*2]='*';
      break;
    }
  }
  sleep_ms(1000);
} 

////////////////////////////////////////////////////////////////////////////////////
typedef struct {
	char isDir;
	char filename[13];
	char long_filename[32];
	char full_path[210];
} DIR_ENTRY;	// 256 bytes = 256 entries in 64k

int num_dir_entries = 0; // how many entries in the current directory

int entry_compare(const void* p1, const void* p2)
{
	DIR_ENTRY* e1 = (DIR_ENTRY*)p1;
	DIR_ENTRY* e2 = (DIR_ENTRY*)p2;
	if (e1->isDir && !e2->isDir) return -1;
	else if (!e1->isDir && e2->isDir) return 1;
	else return strcasecmp(e1->long_filename, e2->long_filename);
}

char *get_filename_ext(char *filename) {
    char *dot = strrchr(filename, '.');
    if(!dot || dot == filename) return "";
    return dot + 1;
}

int is_valid_file(char *filename) {
	char *ext = get_filename_ext(filename);
	if (strcasecmp(ext, "BIN") == 0 ) 
		return 1;
	return 0;
}

FILINFO fno;
char search_fname[FF_LFN_BUF + 1];

// polyfill :-)
char *stristr(const char *str, const char *strSearch) {
    char *sors, *subs, *res = NULL;
    if ((sors = strdup (str)) != NULL) {
        if ((subs = strdup (strSearch)) != NULL) {
            res = strstr (strlwr (sors), strlwr (subs));
            if (res != NULL)
                res = (char*)str + (res - sors);
            free (subs);
        }
        free (sors);
    }
    return res;
}

int scan_files(char *path, char *search)
{
    FRESULT res;
    DIR dir;
    UINT i;

	res = f_opendir(&dir, path);
	if (res == FR_OK) {
		for (;;) {
			if (num_dir_entries == 63) break;
			res = f_readdir(&dir, &fno);
			if (res != FR_OK || fno.fname[0] == 0) break;
			if (fno.fattrib & (AM_HID | AM_SYS)) continue;
			if (fno.fattrib & AM_DIR) {
				i = strlen(path);
				strcat(path, "/");
				if (fno.altname[0])	// no altname when lfn is 8.3
					strcat(path, fno.altname);
				else
					strcat(path, fno.fname);
				if (strlen(path) >= 210) continue;	// no more room for path in DIR_ENTRY
				res = scan_files(path, search);
				if (res != FR_OK) break;
				path[i] = 0;
			}
			else if (is_valid_file(fno.fname))
			{
				char *match = stristr(fno.fname, search);
				if (match) {
					DIR_ENTRY *dst = (DIR_ENTRY *)&files[0];
					dst += num_dir_entries;
					// fill out a record
					dst->isDir = (match == fno.fname) ? 1 : 0;	// use this for a "score"
					strncpy(dst->long_filename, fno.fname, 31);
					dst->long_filename[31] = 0;
					// 8.3 name
					if (fno.altname[0])
						strcpy(dst->filename, fno.altname);
					else {	// no altname when lfn is 8.3
						strncpy(dst->filename, fno.fname, 12);
						dst->filename[12] = 0;
					}
					// full path for search results
					strcpy(dst->full_path, path);

					num_dir_entries++;
				}
			}
		}
		f_closedir(&dir);
	}
	return res;
}

int search_directory(char *path, char *search) {
	char pathBuf[256];
	strcpy(pathBuf, path);
	num_dir_entries = 0;
	int i;
	FATFS FatFs;
	if (f_mount(&FatFs, "", 1) == FR_OK) {
		if (scan_files(pathBuf, search) == FR_OK) {
			// sort by score, name
			qsort((DIR_ENTRY *)&files[0], num_dir_entries, sizeof(DIR_ENTRY), entry_compare);
			DIR_ENTRY *dst = (DIR_ENTRY *)&files[0];
			// re-set the pointer back to 0
			for (i=0; i<num_dir_entries; i++)
				dst[i].isDir = 0;
			return 1;
		}
	}
	strcpy(errorBuf, "Problem searching flash");
	return 0;
}

int read_directory(char *path) {
	int ret = 0;
	num_dir_entries = 0;
	DIR_ENTRY *dst = (DIR_ENTRY *)&files[0];

    if (!fatfs_is_mounted())
       mount_fatfs_disk();

	FATFS FatFs;
	if (f_mount(&FatFs, "", 1) == FR_OK) {
		DIR dir;
		if (f_opendir(&dir, path) == FR_OK) {
			while (num_dir_entries < 64) {
				if (f_readdir(&dir, &fno) != FR_OK || fno.fname[0] == 0)
					break;
				if (fno.fattrib & (AM_HID | AM_SYS))
					continue;
				dst->isDir = fno.fattrib & AM_DIR ? 1 : 0;
				if (!dst->isDir)
					if (!is_valid_file(fno.fname)) continue;
				// copy file record to first ram block
				// long file name
				strncpy(dst->long_filename, fno.fname, 31);
				dst->long_filename[31] = 0;
				// 8.3 name
				if (fno.altname[0])
		            strcpy(dst->filename, fno.altname);
				else {	// no altname when lfn is 8.3
					strncpy(dst->filename, fno.fname, 12);
					dst->filename[12] = 0;
				}
				dst->full_path[0] = 0; // path only for search results
	            dst++;
				num_dir_entries++;
			}
			f_closedir(&dir);
		}
		else
			strcpy(errorBuf, "Can't read directory");
		f_mount(0, "", 1);
		qsort((DIR_ENTRY *)&files[0], num_dir_entries, sizeof(DIR_ENTRY), entry_compare);
		ret = 1;
	}
	else
		strcpy(errorBuf, "Can't read flash memory");
	return ret;
}

/////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////* load file in  ROM */////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////

int load_file(char *filename) {
	FATFS FatFs;
	int car_file = 0;
	UINT br, size = 0;
	unsigned char byteread[2];
	
  //printInty("loadfile");
  //printInty(filename);
  if (f_mount(&FatFs, "", 1) != FR_OK) {
		strcpy(errorBuf, "Can't read flash memory");
    error(1);
		return 0;
	}
	FIL fil;
 
 
	if (f_open(&fil, filename, FA_READ) != FR_OK) {
		strcpy(errorBuf, "Can't open file");
    error(2);
		goto cleanup;
	}

	
	int bytes_to_read = 2;
	// read the file to SRAM
 
 //printInty("inizio");
 size=0;
 while (!(f_eof(&fil))) {
   
  	f_read(&fil, byteread, bytes_to_read, &br);
    RBHi = byteread[0];
    //f_read(&fil, dst, bytes_to_read, &br);
    RBLo = byteread[1];
   
    ROM[size]= RBLo | (RBHi << 8);
	  size++;
   
  }

closefile:
  romLen=size;
  RAM[base+202]=romLen;
  //printInty("romLen");
 	f_close(&fil);

cleanup:
	f_mount(0, "", 1);

	return br;
}

/////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////* load .cfg file */////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////

int load_cfg(char *filename) {
	FATFS FatFs;
	int car_file = 0;
	UINT br, size = 0;
  unsigned char byteread=0;
  char riga[80];
  char tmp[80];
  int linepos;     

  memset(tmp,0,sizeof(tmp));
  int j=40;
  int dot=0;
  while ((filename[j]!='.')&&(j>0)) {
    dot=j;
    j--;
  }
  for (int i=0;i<j;i++) tmp[i]=filename[i];
  tmp[j]=0;
  
   strcat(tmp,".cfg");
  
   memcpy(filename,tmp,sizeof(tmp));
   
	  
  if (f_mount(&FatFs, "", 1) != FR_OK) {
		strcpy(errorBuf, "Can't read flash memory");
		error(8);
    return 0;
	}
	FIL fil;
  
   
	if (f_open(&fil, filename, FA_READ) != FR_OK) {
		strcpy(errorBuf, "cfg not found");
    //printInty("using 0.cfg");
    filename="/0.cfg";  
    if (f_open(&fil, filename, FA_READ) != FR_OK) {
		  strcpy(errorBuf, "Can't find 0.cfg");
      error(3);
    }
  } else {
    //printInty("cfg found");
  }
 
	unsigned char *dst = &byteread;
	int bytes_to_read = 1;
	// read the file to SRAM
  #ifdef debug
  int slot1=0;  // poi TOGLIERE DOPO IL DEBUG -----------------------
  #else
    slot=0;  // poi rimettere dopo il debug!!! ------------------------------
  #endif

  hacks=0;
  #ifndef debug
  RAMused=0; // poi rimettere dopo il debug!!!  --------------------------------
  #endif

  while (!(f_eof(&fil))) {
    memset(riga,0,sizeof(riga));
    f_gets(riga,79,&fil);
    //printInty(riga);
    if (riga[0]>=32) {
      //printInty("riga0>32");
      memset(tmp,0,sizeof(tmp));
      memcpy(tmp,riga,9);
  #ifdef debug
      if (slot1==0) {
  #else
      if (slot==0) {
  #endif      
        if (!(strcmp(tmp,"[mapping]"))) {
          memset(riga,0,sizeof(riga));
          f_gets(riga,79,&fil);
          //printInty(riga);
         } else {
          printf("[mapping] not found");
           error(4); // 3 error [mapping] section not found
        }
      }
      if (!(strcmp(tmp,"[memattr]"))) {
        memset(riga,0,sizeof(riga));
        f_gets(riga,79,&fil);
        memset(tmp,0,sizeof(tmp)); 
        memcpy(tmp,riga+1,5);
        ramfrom=strtoul(tmp,NULL,16);
        #ifndef debug 
        mapfrom[slot]=ramfrom;
        #endif
        memset(tmp,0,sizeof(tmp));
        memcpy(tmp,riga+9,5);
        #ifndef debug 
          ramto=strtoul(tmp,NULL,16);
          mapto[slot]=ramto; 
          maprom[slot]=ramfrom;
          addrto[slot]=maprom[slot]+(mapto[slot]-mapfrom[slot]);
        #else
          ramto=strtoul(tmp,NULL,16);
          mapto[slot1]=ramto; 
          maprom[slot1]=ramfrom;
          addrto[slot1]=maprom[slot1]+(mapto[slot1]-mapfrom[slot1]);
        #endif
        #ifndef debug
        tipo[slot]=2; // RAM
        RAMused=1;
        mapdelta[slot]=maprom[slot] - mapfrom[slot]; //poi rimettere  --------------------------------
        mapsize[slot]=mapto[slot] - mapfrom[slot];  //poi rimettere  --------------------------------
        //printInty("slot++");
        slot++;
        #else
        slot1++;
        #endif  
      } else {   
        memset(tmp,0,sizeof(tmp));
        memcpy(tmp,riga,1);
        if (!strcmp(tmp,"p")) {
          // [MACRO]
          memset(tmp,0,sizeof(tmp));
          memcpy(tmp,riga+2,4);
          HACK[hacks]=strtoul(tmp,NULL,16);
          memset(tmp,0,sizeof(tmp));
          memcpy(tmp,riga+7,4);
          HACK_CODE[hacks]=strtoul(tmp,NULL,16);
          hacks++;
        } else {
          //mapping
          linepos=strcspn(riga,"-");
          if ((linepos>=0) && (riga[linepos]=='-')) {
            //printInty("line>0");
            memset(tmp,0,sizeof(tmp));
            memcpy(tmp,riga+1,4);
            #ifndef debu
            mapfrom[slot]=strtoul(tmp,NULL,16);  // poi rimettere --------------------------------
            #endif
            if (linepos==6) {
              memset(tmp,0,sizeof(tmp));
              memcpy(tmp,riga+(linepos+3),4);
              #ifndef debug
              mapto[slot]=strtoul(tmp,NULL,16);  // poi rimettere --------------------------------
              #endif
              memset(tmp,0,sizeof(tmp));
              memcpy(tmp,riga+(linepos+11),4);  
              #ifndef debug
              maprom[slot]=strtoul(tmp,NULL,16);  // poi rimettere --------------------------------
              #endif
            } else {
              memset(tmp,0,sizeof(tmp));
              memcpy(tmp,riga+(linepos+3),5);
              #ifndef debug
              mapto[slot]=strtoul(tmp,NULL,16); // poi rimettere --------------------------------
              #endif
              memset(tmp,0,sizeof(tmp));
              memcpy(tmp,riga+(linepos+12),5);  
              #ifndef debug     
              maprom[slot]=strtoul(tmp,NULL,16);  // poi rimettere ---------------------------
              #endif
              }   
            #ifndef debug
            addrto[slot]=maprom[slot]+(mapto[slot]-mapfrom[slot]); // poi rimettere ------------------
            #endif
            linepos=strcspn(riga,"P");
            if ((linepos>0)&&(riga[linepos]!=0)) {
              #ifndef debug
              tipo[slot]=1;   //poi rimettere -------------------------------- 
              #endif
              memset(tmp,0,sizeof(tmp));
              memcpy(tmp,riga+(linepos+5),2);
              #ifndef debug
              page[slot]=strtoul(tmp,NULL,16); //poi rimettere  --------------------------------
              #endif

            } else {
              #ifndef debug
              tipo[slot]=0;  // poi rimettere -------------------------
              #endif
            }
            #ifndef debug
            slot++;
            #else
            slot1++;
            #endif
            //printInty("slot++");
          } 
        }
      }
      #ifndef debug
      mapdelta[slot-1]=maprom[slot-1] - mapfrom[slot-1]; //poi rimettere  --------------------------------
      mapsize[slot-1]=mapto[slot-1] - mapfrom[slot-1];  //poi rimettere  --------------------------------
      #endif
    }
  }
    slot=slot-1;

closefile:
	f_close(&fil);
  
cleanup:
	f_mount(0, "", 1);

	return br;
}


////////////////////////////////////////////////////////////////////////////////////
//                     filelist
////////////////////////////////////////////////////////////////////////////////////

void filelist(DIR_ENTRY* en,int da, int a)
{
  char longfilename[32];
  char tmp[32];

  int base=0x17f;
  for(int i=0;i<20*20;i++) RAM[base+i*2]=0;
    for(int n = 0;n<(a-da);n++) {
		memset(longfilename,0,32);
	
	 	if (en[n+da].isDir) {
			//strcpy(longfilename,"DIR->");
			RAM[0x1000+n]=1;
			strcat(longfilename, en[n+da].long_filename);
	 	} else {
			RAM[0x1000+n]=0;
			strcpy(longfilename, en[n+da].long_filename);
      /// rimuovo il .bin
      memset(tmp,0,sizeof(tmp));
      int j=32;
      int dot=0;
      while ((longfilename[j]!='.')&&(j>0)) {
      dot=j;
      j--;
      }
      for (int i=0;i<j;i++) tmp[i]=longfilename[i];
      tmp[j]=0;      
      memcpy(longfilename,tmp,sizeof(tmp));
	 	}
  
	 	for(int i=0;i<20;i++) {
      		RAM[base+i*2+(n*40)]=longfilename[i];
	  		if ((RAM[base+i*2+(n*40)])<=20) RAM[base+i*2+(n*40)]=32;
     	}
		strcpy((char*)&nomefiles[40*n], longfilename);
	}
	RAM[0x1030]=da;RAM[0x1031]=a;RAM[0x1032]=num_dir_entries;
  }

////////////////////////////////////////////////////////////////////////////////////
//                     IntyMenu
////////////////////////////////////////////////////////////////////////////////////
void IntyMenu(int tipo) { // 1=start,2=next page, 3=prev page, 4=dir up
  int numfile=0;
  int maxfile=0;
  int ret=0;
  int rootpos[255];
  int lastpos;
  	
  switch (tipo) {
    case 1:
    /////////////////// TIPO 1 /////////////////// 
      ret = read_directory(curPath);
		  if (!(ret)) error(1);
		  maxfile=10;
		  fileda=0;
		  if (maxfile>num_dir_entries) maxfile=num_dir_entries;
		  filea=fileda+maxfile;
		  filelist((DIR_ENTRY *)&files[0],fileda,filea);
		  //sleep_ms(1400);
      break;
    case 2:
    /////////////////// TIPO 2 /////////////////// 
   	  if (filea<num_dir_entries) {
        maxfile=10;
		    if ((filea+maxfile)>num_dir_entries) maxfile=num_dir_entries-filea;
   	    fileda=filea;
		    filea=fileda+maxfile;
    		filelist((DIR_ENTRY *)&files[0],fileda,filea); 
      }
      break;
    case 3:    
    /////////////////// TIPO 3 /////////////////// 
   	  if (fileda>=10) {
  	    fileda=fileda-10;
		    filea=fileda+10;
		    filelist((DIR_ENTRY *)&files[0],fileda,filea);	
	    }
    break;
  }
   

}
////////////////////////////////////////////////////////////////////////////////////
//                     Directory Up
////////////////////////////////////////////////////////////////////////////////////
void DirUp() {
	int len = strlen(curPath);
	if (len>0) {
		while (len && curPath[--len] != '/');
		curPath[len] = 0;
	}
}
////////////////////////////////////////////////////////////////////////////////////
//                     LOAD Game
////////////////////////////////////////////////////////////////////////////////////
#pragma GCC push_options
#pragma GCC optimize ("O0")
void LoadGame(){ 
  int numfile=0;
  int numErr=0;
  char longfilename[32];
  char firstbyte=0x0;

   numfile=RAM[0x899]+fileda-1;
   //numfile=3;  //poivia
   
   DIR_ENTRY *entry = (DIR_ENTRY *)&files[0];
   strcpy(longfilename,entry[numfile].long_filename);
  
  if (entry[numfile].isDir)
	{	// directory
    strcat(curPath, "/");
		strcat(curPath, entry[numfile].filename);
		IntyMenu(1);
	} else {
		memset(path,0,sizeof(path));
		strcat(path,curPath);
		strcat(path, "/");
		strcat(path,longfilename);
  
    char savepath[256];
    memcpy(savepath,path,sizeof(path)); // preserve path
  	load_cfg(path);
    
    
    load_file(savepath);  // load rom in files[]
    
    
  gpio_put(LED_PIN,false);
   	
    sleep_ms(200);
    resetCart(); // inizia con il gioco!
    sleep_ms(200);
    resetCart(); // inizia con il gioco!
    memset(RAM,0,sizeof(RAM));
    while(1) {
        gpio_put(LED_PIN,true);
        sleep_ms(2000);
        gpio_put(LED_PIN,false);
        sleep_ms(2000);
    }
  }
}
////////////////////////////////////////////////////////////////////////////////////
//                     FujiNet mailbox
////////////////////////////////////////////////////////////////////////////////////

// Same delay as sleep_ms(), but keeps USB alive during it. The stock
// PiRTO II menu holds RAM[0x119]=1 (its own ack) for 800ms after every
// command as a crude debounce; without pumping tud_task() during that
// window the FujiNet USB link would stall in lockstep with the menu.
static void fuji_wait_ms_pumped(uint32_t ms)
{
    absolute_time_t deadline = make_timeout_time_ms(ms);
    while (!time_reached(deadline))
        tud_task();
}

// Services at most one FujiNet transaction per call: if the Intellivision
// has bumped FUJI_MB_SEQ since our last reply, run it and publish the
// result by writing FUJI_MB_ACKSEQ = seq last. See fuji_mailbox.h for the
// full interlock rationale.
static void fuji_mailbox_service(void)
{
    static uint8_t rxbuf[FUJI_MB_RX_MAX];
    static uint8_t txbuf[FUJI_MB_TX_MAX];

    RAM[FUJI_MB_LINK] = tud_cdc_connected() ? 1 : 0;

    uint8_t seq = (uint8_t)RAM[FUJI_MB_SEQ];
    if (seq == (uint8_t)RAM[FUJI_MB_ACKSEQ])
        return; // nothing new since the last reply we published

    RAM[FUJI_MB_STATUS] = FUJI_MB_STATUS_BUSY;

    // The Intellivision boots (and so bumps FUJI_MB_SEQ) far faster than the
    // ESP32-S3 boots and enumerates over USB -- observed ~650ms from ESP32
    // power-on to RS232 setup completing, vs. a couple of seconds at most
    // for fujitest.bas to reach this point. fujibus_transact() only checks
    // tud_cdc_connected() once and fails immediately if it's not up yet, so
    // without this wait, the very first transaction after both power on
    // together would almost always see FB_ENOLINK even though the ESP32
    // would have been ready moments later -- and fuji_mailbox_service()
    // only runs once per SEQ bump, there's no retry above this.
    {
        absolute_time_t link_deadline = make_timeout_time_ms(3000);
        while (!tud_cdc_connected() && !time_reached(link_deadline))
            tud_task();
        RAM[FUJI_MB_LINK] = tud_cdc_connected() ? 1 : 0;
    }

    uint8_t device = (uint8_t)RAM[FUJI_MB_DEVICE];
    uint8_t command = (uint8_t)RAM[FUJI_MB_CMD];
    unsigned nparam = RAM[FUJI_MB_NPARAM];
    if (nparam > 8)
        nparam = 8;

    fb_param_t params[8];
    for (unsigned i = 0; i < nparam; i++) {
        uint8_t size = (uint8_t)RAM[FUJI_MB_PARAM_SIZE + i];
        uint32_t val = 0;
        for (uint8_t b = 0; b < size && b < 4; b++)
            val |= ((uint32_t)(uint8_t)RAM[FUJI_MB_PARAM_VAL + i * 4 + b]) << (8 * b);
        params[i].size = size;
        params[i].value = val;
    }

    uint16_t txlen = (uint16_t)(RAM[FUJI_MB_TXLEN_LO] | (RAM[FUJI_MB_TXLEN_HI] << 8));
    if (txlen > FUJI_MB_TX_MAX)
        txlen = FUJI_MB_TX_MAX;
    for (uint16_t i = 0; i < txlen; i++)
        txbuf[i] = (uint8_t)RAM[FUJI_MB_TX + i];

    fb_reply_t reply;
    fb_status_t st = fujibus_transact(device, command, nparam ? params : NULL, nparam,
                                       txlen ? txbuf : NULL, txlen,
                                       rxbuf, sizeof(rxbuf), &reply, 5000);

    RAM[FUJI_MB_ERR] = (uint16_t)st;
    if (st == FB_OK) {
        uint16_t rxlen = reply.data_len; // already bounded by rx_cap == sizeof(rxbuf)
        for (uint16_t i = 0; i < rxlen; i++)
            RAM[FUJI_MB_RX + i] = reply.data[i];
        RAM[FUJI_MB_RXLEN_LO] = rxlen & 0xFF;
        RAM[FUJI_MB_RXLEN_HI] = (rxlen >> 8) & 0xFF;
        RAM[FUJI_MB_REPLY_CMD] = reply.command;
        RAM[FUJI_MB_STATUS] = (reply.command == FUJICMD_ACK) ? FUJI_MB_STATUS_OK : FUJI_MB_STATUS_ERR;
    } else {
        RAM[FUJI_MB_RXLEN_LO] = 0;
        RAM[FUJI_MB_RXLEN_HI] = 0;
        RAM[FUJI_MB_REPLY_CMD] = 0;
        RAM[FUJI_MB_STATUS] = FUJI_MB_STATUS_ERR;
    }

    RAM[FUJI_MB_ACKSEQ] = seq; // single publishing store, written last
}

////////////////////////////////////////////////////////////////////////////////////
//                     Inty Cart Main
////////////////////////////////////////////////////////////////////////////////////

void Inty_cart_main()
{
    uint32_t pins;
    uint32_t addr;
    uint32_t dataOut=0;
    uint16_t dataWrite=0;
 
 printf("Inty_cart_main\n");

	// overclocking isn't necessary for most functions - but XEGS carts weren't working without it
	// I guess we might as well have it on all the time.
  // Voltage must be raised before the clock speed increases, not after.
  vreg_set_voltage(VREG_VOLTAGE_1_10);
  set_sys_clock_khz(270000, true);
  // clk_peri (and so the UART baud divisor) tracks clk_sys; recompute it now
  // that the clock has changed, or the debug console drifts off 115200.
  uart_set_baudrate(uart_default, PICO_DEFAULT_UART_BAUD_RATE);
  multicore_launch_core1(core1_main);
  
  // Initialize the bus state variables

  busLookup[BUS_NACT]  = 4; // 100
  busLookup[BUS_BAR]   = 1; // 001
  busLookup[BUS_IAB]   = 4; // 100
  busLookup[BUS_DWS]   = 2; // 010   // test without dws handling
  busLookup[BUS_ADAR]  = 1; // 001
  busLookup[BUS_DW]    = 4; // 100
  busLookup[BUS_DTB]   = 0; // 000
  busLookup[BUS_INTAK] = 4; // 100


  gpio_init_mask(ALWAYS_OUT_MASK);
  gpio_init_mask(DATA_PIN_MASK);
  gpio_init_mask(BUS_STATE_MASK);
  gpio_set_dir_in_masked(ALWAYS_IN_MASK);
  gpio_set_dir_out_masked(ALWAYS_OUT_MASK);
  gpio_init(LED_PIN);
  gpio_put(LED_PIN,true); 
  gpio_init(RST_PIN);
  
  gpio_set_dir(MSYNC_PIN,GPIO_IN);
  gpio_pull_down(MSYNC_PIN);

  // Every sleep_ms() from here to the start of the command loop below is on
  // the USB enumeration's critical path: tusb_init()/tud_init() already ran
  // in main(), so the ESP32-S3 host may start sending SETUP packets at any
  // point from here on. TinyUSB only answers them from inside tud_task(),
  // so any multi-second stretch with no tud_task() calls reads to the host
  // as an unresponsive device -- its enumeration attempt fails, and because
  // no further physical attach event follows, it never retries. Use
  // fuji_wait_ms_pumped() instead of sleep_ms() everywhere in this
  // sequence, not just in the steady-state loop.
  fuji_wait_ms_pumped(800);
#ifdef intidebug
  RAM[0x163]=2; //1 for debug, 2 for debug with looping
#endif

  resetHigh();
  fuji_wait_ms_pumped(30);
  resetLow();
  //while (gpio_get(MSYNC_PIN)==1); // wait for Inty powerup
  printf("Inty Pow-ON");
  
  gpio_put(LED_PIN,true);
  memset(ROM,0,BINLENGTH);
  
  for (int i=0;i<(sizeof(_bootrom)/2);i++) {
   ROM[i]=_bootrom[(i*2)+1] | (_bootrom[i*2] << 8);
  }
  memset(RAM,0,sizeof(RAM));

  RAM[FUJI_MB_MAGIC0] = 'F';
  RAM[FUJI_MB_MAGIC1] = 'N';
  RAM[FUJI_MB_PROTO_VER] = 1;

 for (int i=0; i<maxHacks; i++) {
  HACK[i]=0;
  HACK_CODE[i]=0;
 }

  slot=1; // 2 slots per splash

  //  [mapping]
  //$0000 - $0dFF = $5000
  mapfrom[0]=0x0;
  mapto[0]=0xdff;
  maprom[0]=0x5000;
  tipo[0]=0;
  page[0]=0;
  addrto[0]=0x5dff;
  mapdelta[0]=maprom[0] - mapfrom[0];
  mapsize[0]=mapto[0] - mapfrom[0];
    
 //[memattr]
 //$8000 - $9FFF = RAM 16
  RAMused=1;
  ramfrom=0x8000;
  mapfrom[1]=0x8000;
  mapto[1]=0x9fff;
  maprom[1]=0x8000;
  tipo[1]=2;
  page[1]=0;
  addrto[1]=0x9fff;
  mapdelta[1]=maprom[1] - mapfrom[1];
  mapsize[1]=mapto[1] - mapfrom[1];
  
  //sleep_ms(200);
  //resetCart();
  fuji_wait_ms_pumped(200);
  resetCart();
  fuji_wait_ms_pumped(1200);

  RAM[0x889]=0;
  IntyMenu(1);
	RAM[0x119]=1;
  fuji_wait_ms_pumped(800);
	 	 
  RAM[0x119]=123;
   gpio_put(LED_PIN,true);
  
  // Initial conditions 
  curPath[0]=0;
  IntyMenu(1);

  printf("START %u\n",cmd);
  
  while (1) {
     tud_task();
     fuji_mailbox_service();

     cmd_executing=false;
     cmd=RAM[0x889];
     RAM[0x119]=0;

     if ((cmd>0)&&!(cmd_executing)) {

       printf("%u\n",cmd);

      switch (cmd) {
      case 1:  // read file list
        cmd_executing=true;
        RAM[0x889]=0;
    	  IntyMenu(1);
	 	    RAM[0x119]=1;
      	fuji_wait_ms_pumped(800);
      	break;
      case 2:  // run file list
        cmd_executing=true;
    	  RAM[0x889]=0;
	 	    LoadGame();
		    RAM[0x119]=1;
      	fuji_wait_ms_pumped(800);
      	break;
      case 3:  // next page
	      cmd_executing=true;
        RAM[0x889]=0;
        IntyMenu(2);
		    RAM[0x119]=1;
        fuji_wait_ms_pumped(800);
      	break;
      case 4:  // prev page
        cmd_executing=true;
        RAM[0x889]=0;
     	  IntyMenu(3);
		    RAM[0x119]=1;
        fuji_wait_ms_pumped(800);
	 	    break;
	    case 5:  // up dir
        cmd_executing=true;
        RAM[0x889]=0;
     	  DirUp();
		    IntyMenu(1);
		    RAM[0x119]=1;
        fuji_wait_ms_pumped(800);
	 	    break;
    }
   }
  }
}
 	
#pragma GCC pop_options
