/*
//                       PicoPAC MultiCART by Andrea Ottaviani 2024
//
//  VIDEOPAC  multicart based on Raspberry Pico board -
//
//  More info on https://github.com/aotta/ 
//
//   parts of code are directly from the A8PicoCart project by Robin Edwards 2023
//  
//   Needs to be a release NOT debug build for the cartridge emulation to work
// 
//   Edit myboard.h depending on the type of flash memory on the pico clone//
//
//   v. 1.0 2024-08-05 : Initial version for Pi Pico 
//
*/


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
/* FUJINET: set_sys_clock_khz() lives here; picked up implicitly on older SDKs. */
#include "hardware/clocks.h"


#include "tusb.h"
#include "ff.h"
#include "fatfs_disk.h"

/* FUJINET */
#if CONFIG_FUJINET
#include "fuji_cart.h"
#include "fuji_mailbox.h"
#include "fujiboot.h"
#include "fujinet.h"
#include "fujibus_usb.h"
#endif

// Pico pin usage definitions

#define A0_PIN    0
#define A1_PIN    1
#define A2_PIN    2
#define A3_PIN    3
#define A4_PIN    4
#define A5_PIN    5
#define A6_PIN    6
#define A7_PIN    7
#define A8_PIN    8
#define A9_PIN    9
#define A10_PIN  10
#define A11_PIN  11
#define P11_PIN  12
#define P10_PIN  13
#define D0_PIN   14
#define D1_PIN   15
#define D2_PIN   16
#define D3_PIN   17
#define D4_PIN   18
#define D5_PIN   19
#define D6_PIN   20
#define D7_PIN   21
#define PSEN_PIN 22
#define WR_PIN 23
#define CS_PIN 24   // P14
#define LED_PIN  25 
#define NOTCS_PIN  26 
#define TD_PIN  27 

// Pico pin usage masks
#define A0_PIN_MASK     0x00000001L //gpio 0
#define A1_PIN_MASK     0x00000002L
#define A2_PIN_MASK     0x00000004L
#define A3_PIN_MASK     0x00000008L
#define A4_PIN_MASK     0x00000010L
#define A5_PIN_MASK     0x00000020L
#define A6_PIN_MASK     0x00000040L
#define A7_PIN_MASK     0x00000080L
#define A8_PIN_MASK     0x00000100L
#define A9_PIN_MASK     0x00000200L
#define A10_PIN_MASK    0x00000400L
#define A11_PIN_MASK    0x00000800L
#define P11_PIN_MASK    0x00001000L  // P11 high bank select
#define P10_PIN_MASK    0x00002000L  // P10 low bank select
#define D0_PIN_MASK     0x00004000L //gpio 14
#define D1_PIN_MASK     0x00008000L
#define D2_PIN_MASK     0x00010000L
#define D3_PIN_MASK     0x00020000L
#define D4_PIN_MASK     0x00040000L
#define D5_PIN_MASK     0x00080000L  // gpio 19
#define D6_PIN_MASK     0x00100000L
#define D7_PIN_MASK     0x00200000L
#define PSEN_PIN_MASK   0x00400000L //gpio 22
#define WR_PIN_MASK     0x00800000L //gpio 23  
#define CS_PIN_MASK     0x01000000L //gpio 24   - P14  
#define LED_PIN_MASK    0x02000000L //gpio 25   
#define NOTCS_PIN_MASK  0x04000000L //gpio 26  
#define TD_PIN_MASK     0x08000000L //gpio 27 

// Aggregate Pico pin usage masks
#define ALL_GPIO_MASK  	0x0FFFFFFFL
#define BUS_PIN_MASK    0x00003FFFL
#define BANK_PIN_MASK   0x00003000L
#define DATA_PIN_MASK   0x003FC000L
#define FLAG_MASK       0x0FC00000L
#define ALWAYS_IN_MASK  (BUS_PIN_MASK | FLAG_MASK)
#define ALWAYS_OUT_MASK (DATA_PIN_MASK )

#define SET_DATA_MODE_OUT   gpio_set_dir_out_masked(DATA_PIN_MASK)
#define SET_DATA_MODE_IN    gpio_set_dir_in_masked(DATA_PIN_MASK)
#define SET_LED_ON    	gpio_init(PICO_DEFAULT_LED_PIN);(PICO_DEFAULT_LED_PIN,GPIO_OUT);gpio_put(PICO_DEFAULT_LED_PIN,true);
#define SET_LED_OFF    	gpio_init(PICO_DEFAULT_LED_PIN);gpio_set_dir(PICO_DEFAULT_LED_PIN,GPIO_OUT);gpio_put(PICO_DEFAULT_LED_PIN,false);

// We're going to erase and reprogram a region 256k from the start of flash.
// Once done, we can access this at XIP_BASE + 256k.


char RBLo,RBHi;
char gamelist[255][32];
#define BINLENGTH  1024*128+1
unsigned char rom_table[8][4096];
unsigned char new_rom_table[8][4096];
/* FUJINET: core1 serves through this pointer so a network-booted image can take
 * over with one atomic store instead of a 32KB memcpy racing the running
 * console. The cartridge cannot reset the Odyssey 2 -- there is no reset line on
 * the connector -- so the swap happens on the console's next fetch of the
 * cartridge reset vector, which is what pressing RESET produces. */
unsigned char (*fuji_rom)[4096] = rom_table;
volatile bool fuji_boot_armed = false;
unsigned char extROM[1024];
unsigned char RAM[1024];
unsigned char files[256*255] = {0};
unsigned char nomefiles[32*25] = {0};
char curPath[256] = "";
char path[256];
volatile char cmd=0;
char errorBuf[40];
bool cmd_executing;
volatile int bankswitch;
int bksw=0;
int romsize;
int lastpos;
volatile uint8_t bank_type=1;
volatile uint8_t new_bank_type=1;
volatile char gamechoosen=0;
volatile char newgame=0;
/* FUJINET: was extram[0xff] -- 255 entries, but indexed [addr & 0xff], so the
 * top address wrote and read one past the end. The magic test below reads
 * extram[0xff] specifically, so this was not hypothetical. */
char extram[0x100];


////////////////////////////////////////////////////////////////////////////////////
//                     Error(N)
////////////////////////////////////////////////////////////////////////////////////
void error(int numblink){
 	multicore_lockout_start_blocking();	
    sleep_ms(1000);
	gpio_init(PICO_DEFAULT_LED_PIN);
	gpio_set_dir(PICO_DEFAULT_LED_PIN,GPIO_OUT);
  while(1){

    for(int i=0;i<numblink;i++) {
      gpio_put(PICO_DEFAULT_LED_PIN,true);
      sleep_ms(600);
      gpio_put(PICO_DEFAULT_LED_PIN,false);
      sleep_ms(500);
    }
  sleep_ms(2000);
  }
}

/*
 Theory of Operation
 -------------------
 //
 //
 //
*/
#pragma GCC push_options
#pragma GCC optimize ("O3")

void __not_in_flash_func(core1_main()) {
    uint32_t addr;
    char dataWrite=0;
    uint32_t pins;
    uint8_t bank=0;
	uint8_t romlatch=0;

	multicore_lockout_victim_init();	
   
    //gpio_set_dir_in_masked(ALWAYS_IN_MASK);
	
   newgame=0;
   gamechoosen=0;

    // Initial conditions
    //SET_DATA_MODE_IN;
	
	while(newgame==0) {
	//while(1) {
		 	pins=gpio_get_all();
	        addr = (pins & 0b0111111111111);  
			bank=3-((gpio_get(P10_PIN)+(gpio_get(P11_PIN)*2)));
	   		if (gpio_get(PSEN_PIN)==0) {
#if CONFIG_FUJINET
				/* FUJINET: a fetch of the cartridge reset vector with a staged
				 * image waiting means the console has just restarted. Swap
				 * before serving the byte, so the restart reads the new image
				 * from its very first fetch rather than a torn mixture. */
				if (fuji_boot_armed && addr == 0x400) {
					fuji_rom = new_rom_table;
					fuji_boot_armed = false;
				}
#endif
				SET_DATA_MODE_OUT;
    			gpio_put_masked(DATA_PIN_MASK,(fuji_rom[bank][addr])<<D0_PIN);
			} 
		  	if((gpio_get(CS_PIN)==0) && (gpio_get(WR_PIN)==0)) {
				   uint8_t wa = addr & 0xff;
				   uint8_t wd = ((pins & DATA_PIN_MASK)>>D0_PIN);
				   extram[wa]=wd;
#if CONFIG_FUJINET
				   /* FUJINET: $E0-$E3 is the mailbox register file. Recorded
				    * here and handed to core0 -- this loop must stay a poll.
				    * That range is the only part of this window The Voice does
				    * not decode as a sound trigger or a bank select. */
				   if (wa >= FN_W_REGSEL && wa <= FN_W_SEQ) {
					   fuji_cart_note_write(wa, wd);
				   }
#endif
				   if (extram[0xff]==0xaa) {
			         gamechoosen=extram[0xfe];
				   }
			} 
			
		 SET_DATA_MODE_IN;
		}

	SET_DATA_MODE_IN;
	    
	switch (new_bank_type) {
	  case 0:  // standard 2k / 4k
        while(1) {	
	    	pins=gpio_get_all();
	        addr = (pins & 0b0111111111111);  
			if (gpio_get(P10_PIN)) {
				bank=0;
			} else {
				bank=1;
			}
			if (gpio_get(PSEN_PIN)==0) {
				SET_DATA_MODE_OUT;
    			gpio_put_masked(DATA_PIN_MASK,(rom_table[bank][addr])<<D0_PIN);
			} 
		 SET_DATA_MODE_IN;
		}
		break;
	  case 1:  // standard 8k
        while(1) {	
	     	pins=gpio_get_all();
	        addr = (pins & 0b0111111111111);  
			bank=3-((gpio_get(P10_PIN)+(gpio_get(P11_PIN)*2)));
	   	  	
			if (gpio_get(PSEN_PIN)==0) {
				SET_DATA_MODE_OUT;
    			gpio_put_masked(DATA_PIN_MASK,(rom_table[bank][addr])<<D0_PIN); 
		  }
		 SET_DATA_MODE_IN;
		}
		break;
	  case 2:   // XROM
        while(1) {	
			pins=gpio_get_all();
	        addr = (pins & 0b0111111111111); // for all cart but xrom
	  		if ((gpio_get(P11_PIN)==1 && gpio_get(NOTCS_PIN)==1)) {
					SET_DATA_MODE_OUT;
    				gpio_put_masked(DATA_PIN_MASK,(extROM[addr&0x3ff])<<D0_PIN);
			} else {
				if (gpio_get(PSEN_PIN)==0) {
					SET_DATA_MODE_OUT;
    				gpio_put_masked(DATA_PIN_MASK,(rom_table[0][addr])<<D0_PIN);	
				} 
			} 	
			
		 SET_DATA_MODE_IN;
		}
		break;
	  case 3:   // 12k/16k
        while(1) {	
	    	pins=gpio_get_all();
	        addr = (pins & 0b0111111111111); // for all cart but xrom
			if((gpio_get(CS_PIN)==0) && (gpio_get(WR_PIN)==0)) {
				if ((addr & 0xff)>=0x80) {
					romlatch=((~(pins & DATA_PIN_MASK)>>D0_PIN)&7);
				}	
			} else {
			if (gpio_get(P10_PIN)==0) {
				bank=romlatch;
			} else {
				bank=0;
			}
				if (gpio_get(PSEN_PIN)==0) {
					SET_DATA_MODE_OUT;
    				gpio_put_masked(DATA_PIN_MASK,(rom_table[bank][addr])<<D0_PIN);
				}
			} 
		 	SET_DATA_MODE_IN;
		  //}
		}
		break;
		case 4:  // flat 12k (new KTAA)
        while(1) {	
	    	pins=gpio_get_all();
	        addr = (pins & 0b0111111111111);  
			bank=3-((gpio_get(P10_PIN)+(gpio_get(P11_PIN)*2)));
	   		
			if (gpio_get(PSEN_PIN)==0) {
				SET_DATA_MODE_OUT;
    			gpio_put_masked(DATA_PIN_MASK,(rom_table[bank][addr])<<D0_PIN);
			} 
		 SET_DATA_MODE_IN;
		}
		break;
	}
}
#pragma GCC pop_options
////////////////////////////////////////////////////////////////////////////////////
//                     MENU Reset
////////////////////////////////////////////////////////////////////////////////////    

void reset() {
   
 //multicore_lockout_start_blocking();	

  while (gpio_get(PSEN_PIN)==0) {
  SET_DATA_MODE_OUT;
  gpio_put_masked(DATA_PIN_MASK,0x84<<D0_PIN);
 	}
  SET_DATA_MODE_IN;
 while (gpio_get(PSEN_PIN)==0) {
  SET_DATA_MODE_OUT;
  gpio_put_masked(DATA_PIN_MASK,0x00<<D0_PIN);
 	}
  SET_DATA_MODE_IN;
 
  sleep_ms(5);
 
  while (gpio_get(PSEN_PIN)==0) {
  SET_DATA_MODE_OUT;
  gpio_put_masked(DATA_PIN_MASK,0x84<<D0_PIN);
  }
  SET_DATA_MODE_IN;
  while (gpio_get(PSEN_PIN)==0) {
  SET_DATA_MODE_OUT;
  gpio_put_masked(DATA_PIN_MASK,0x00<<D0_PIN);
 	}
  SET_DATA_MODE_IN;
 
  while (gpio_get(PSEN_PIN)==0) {
  SET_DATA_MODE_OUT;
  gpio_put_masked(DATA_PIN_MASK,0x84<<D0_PIN);
  }
  SET_DATA_MODE_IN;
  while (gpio_get(PSEN_PIN)==0) {
  SET_DATA_MODE_OUT;
  gpio_put_masked(DATA_PIN_MASK,0x00<<D0_PIN);
 	}
  SET_DATA_MODE_IN;

}       

////////////////////////////////////////////////////////////////////////////////////

static const unsigned char chartable [128] = 
  { 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, // 0..9
    0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, // 10..19
    0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, // 20..29
    0x3F, 0x3F, 0x0C, 0x3F, 0x3F, 0x3F, 0x0B, 0x3E, 0x10, 0x3F, // 30..39
    0x2E, 0x3B, 0x29, 0x10, 0x27, 0x28, 0x27, 0x2A, 0x00, 0x01, // 40..49 
    0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0A, // 50..59
    0x3F, 0x2B, 0x3F, 0x0D, 0x3F, 0x20, 0x25, 0x23, 0x1A, 0x12, // 60..69
    0x1B, 0x1C, 0x1d, 0x16, 0x1E, 0x1F, 0x0E, 0x26, 0x2D, 0x17, // 70..79
    0x0F, 0x18, 0x13, 0x19, 0x14, 0x15, 0x24, 0x11, 0x22, 0x2C, // 80..89
    0x21, 0x3F, 0x3B, 0x3F, 0x3F, 0x28, 0x3F, 0x20, 0x25, 0x23, // 90..99
    0x1A, 0x12, 0x1B, 0x1C, 0x1d, 0x16, 0x1E, 0x1F, 0x0E, 0x26, // 100..109
    0x2D, 0x17, 0x0F, 0x18, 0x13, 0x19, 0x14, 0x15, 0x24, 0x11, // 110..119
    0x22, 0x2C, 0x21, 0x2E, 0x2E, 0x3B, 0x3F, 0x3F};            // 120..127
    
char ASCII_to_VP(char c){
   if (c <128) {
      return chartable[c];
   } else {
      return 0x3F;
   }
  
}

///////////////////////
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
	if (strcasecmp(ext, "BIN") == 0 || strcasecmp(ext, "ROM") == 0  || strcasecmp(ext, "CV") )
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
			if (num_dir_entries == 255) break;
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
			while (num_dir_entries < 255) {
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


int filesize(char *filename) {
    if (!fatfs_is_mounted())
       mount_fatfs_disk();

	FATFS FatFs;
	int car_file = 0;
	UINT br = 0;
	if (f_mount(&FatFs, "", 1) != FR_OK) {
		error(1);
		return 0;
	}

	FIL fil;
	if (f_open(&fil, filename, FA_READ) != FR_OK) {
		sleep_ms(100);
		error(2);
		goto cleanup;
	}



	int byteslong = f_size(&fil);
	romsize=byteslong;

closefile:
	f_close(&fil);
cleanup:
	f_mount(0, "", 1);

	return byteslong;
}



/* load file in  ROM (rom_table) */

int load_file(char *filename) {
	FATFS FatFs;
	UINT br = 0;
	UINT bw = 0;
    int l,nb;
    
			memset(rom_table,0,1024*8*4);
	
	l=filesize(filename);
	
    
	
	if (f_mount(&FatFs, "", 1) != FR_OK) {
		error(1);
	}

	nb = l/2048;   // nb = number of banks, l=file size)
	
	bank_type=0;
	if (nb==4) bank_type=1;
	if (nb>4) bank_type=3;
	
	FIL fil;
	if (f_open(&fil, filename, FA_READ) != FR_OK) {
		error(6);
	}
	

	// read the file to flash RAM
	
		for (int i = nb - 1; i >= 0; i--) {
        	if (f_read(&fil,&rom_table[i][1024], 2048, &br)!= FR_OK) {
				error(9);
			}
	    	memcpy(&rom_table[i][3072], &rom_table[i][2048], 1024); /* simulate missing A10 */
    	}
            // mirror ROM in higher banks
    if (nb<2) memcpy(&rom_table[1],&rom_table[0],4096);
    if (nb<4) memcpy(&rom_table[2],&rom_table[0],8192);

closefile:
	f_close(&fil);
	    
cleanup:
	f_mount(0, "", 1);

	return br;
}

int load_newfile(char *filename) {
	FATFS FatFs;
	UINT br = 0;
	UINT bw = 0;
    int l,nb;
    
	memset(new_rom_table,0,1024*8*4);
	
	l=filesize(filename);
	
	if (f_mount(&FatFs, "", 1) != FR_OK) {
		error(1);
	}

	FIL fil;
	if (f_open(&fil, filename, FA_READ) != FR_OK) {
		error(4);
	}
    
	nb = l/2048;   // nb = number of banks, l=file size)
		
    if ((strcmp(filename,"vp_40.bin")==0)||((strcmp(filename,"vp_31.bin")==0))||((strcmp(filename,"4inarow.bin")==0))) {  // 3k games
	        new_bank_type=2;
			if (f_read(&fil, &extROM[0], 1024, &br) != FR_OK) {
              // error(5);
            }
            if (f_read(&fil, &new_rom_table[0][1024], 3072, &br) != FR_OK) {
               // error(7);
            } 	
	} else if ((strstr(filename,"ktaa")!=0)&&(nb==6)) {
	//	SET_LED_ON;
         new_bank_type=4;
		 nb=l/3072;
		 for (int i = nb - 1; i >= 0; i--) {
        	if (f_read(&fil,&new_rom_table[i][1024], 3072, &br)!= FR_OK) {
				error(7);
			}
	    }
	} else {
		new_bank_type=0;
		if (nb==4) new_bank_type=1;
		if (nb>4) new_bank_type=3;

		for (int i = nb - 1; i >= 0; i--) {
        	if (f_read(&fil,&new_rom_table[i][1024], 2048, &br)!= FR_OK) {
				error(7);
			}
	    	memcpy(&new_rom_table[i][3072], &new_rom_table[i][2048], 1024); /* simulate missing A10 */
    	}
	}
	    // mirror ROM in higher banks
    if (nb<2) memcpy(&new_rom_table[1],&new_rom_table[0],4096);
   	if (nb<4) memcpy(&new_rom_table[2],&new_rom_table[0],8192);
	
	
closefile:
	f_close(&fil);
	    
cleanup:
	f_mount(0, "", 1);

	return br;
}

void convert_ascii_file_to_VP(char* dest, char* source){
  dest[0]=ASCII_to_VP(' ');
  for (int i=1; i<16; i++){
    if (i-1 < strlen(source))
      dest[i]=ASCII_to_VP(source[i-1]);
    else
      dest[i] = ASCII_to_VP(' ');
  }
}

////////////////////////////////////////////////////////////////////////////////////
//                     filelist
////////////////////////////////////////////////////////////////////////////////////

void filelist(DIR_ENTRY* en,int da, int a)
{
  char longfilename[32];
  char tmp[32];
  char VPfile[16];
  unsigned long int pos;
  char block[0x80];
  uint8_t riga=0;
  uint8_t blocco=0;
  uint8_t selectori[1024*16];

  FATFS FatFs;
  UINT br = 0;
  UINT bw = 0;
  uint numfiles=0;

  const int block_addr[] = // andrea
  {
  0x1c00, 0x1d00, 0x1e00, 0x1f00, 0x1100, 0x1200, 0x1300, 0x1400, // 7
  0x1500, 0x1600, 0x1700, 0x0900, 0x0a00, 0x0b00, 0x0c00, 0x0d00, //15
  0x0e00, 0x0f00, 0x0100, 0x0200, 0x0300, 0x0400, 0x0500, 0x0600, //23
  0x0700, 0x0800, 0x1c00, 0x1c00, 0x1c00, 0x1c00, 0x1c00, 0x1c00,
  }; 
 	
	if (f_mount(&FatFs, "", 1) != FR_OK) {
		error(1);
	}
FIL fil,fil2;
 
   //open file "Selectgame.$$$" for input/output:
	
	if (f_open(&fil, "/selectgame.$$$", FA_READ) != FR_OK) {
		sleep_ms(100);
		error(2);
		goto cleanup;
	}
	
	for (int i=0;i<8;i++) {
		f_lseek(&fil,1024*i);
         if (f_read(&fil, &selectori[1024*i], 1024, &br)!= FR_OK) { 	error(i); 	}
	}
  
  	    	
    f_close(&fil);
	
	

    // open file "Selectgame.bin" for input/output:
	if (f_open(&fil, "/selectgame.bin", FA_READ|FA_WRITE) != FR_OK) {
		sleep_ms(100);
		error(3);
		goto cleanup;
	}
	     f_write(&fil,&selectori,4*1024,&bw);
		f_lseek(&fil,0);
		
    memset(block,10,sizeof(block));
	
	
    for(int n = 0;n<a;n++) {
		memset(longfilename,0,32);
		
	 	if (en[n].isDir) {
			//	dir not supported
	 	} else {
			strcpy(longfilename, en[n].long_filename);
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
    	  
		  convert_ascii_file_to_VP(VPfile,(char*)&longfilename);
		  for (int i=0;i<16;i++) {
			block[(riga*16)+i]=VPfile[i];
		  }
		
		  strcpy(gamelist[numfiles],en[n].long_filename);

	      if (strcmp(tmp,"selectgame")) {
			riga++;
			numfiles++;	
			}
		  

		  if (riga==8) {
		    int res = f_lseek(&fil, block_addr[blocco]+0x80);
            blocco++;
		    f_write(&fil,&block,0x80,&bw);
			memset(block,0x28,sizeof(block));
			riga=0;
		  }	
		}
		if (riga!=0) {
		    int res = f_lseek(&fil, block_addr[blocco]+0x80);
            blocco++;
		    f_write(&fil,&block,0x80,&bw);
			memset(block,0,sizeof(block));
			riga=0;
		}
            numfiles--; 
	   	    int res = f_lseek(&fil, 0x1aef);
		    f_write(&fil,&numfiles,1,&bw);
		
		
    
closefile:
	f_close(&fil);
	    
cleanup:
	f_mount(0, "", 1);
  }

////////////////////////////////////////////////////////////////////////////////////
//                     Launch Menu
////////////////////////////////////////////////////////////////////////////////////

void videopacMenu() {  
  int numfile=0;
  int maxfile=0;
  int ret=0;
  int rootpos[255];
  int fileda=0,filea=0;

          ret = read_directory(curPath);
		  if (!(ret)) error(1);
		  maxfile=25*8;
		  if (maxfile>num_dir_entries) maxfile=num_dir_entries;
		  filea=fileda+maxfile;
		  filelist((DIR_ENTRY *)&files[0],fileda,filea);
		  //sleep_ms(1400);

 }

////////////////////////////////////////////////////////////////////////////////////
//                     PicoPAC Cart Main
////////////////////////////////////////////////////////////////////////////////////

void picopac_cart_main()
{
    uint32_t pins;
    uint32_t addr;
    uint32_t dataOut=0;
    uint16_t dataWrite=0;
	 int ret=0;

    int l, nb;
   

    gpio_init_mask(ALL_GPIO_MASK);
  
    
    stdio_init_all();   // for serial output, via printf()
    printf("Start\n");

 
  sleep_ms(400);
  memset(gamelist,'_',sizeof(gamelist));
  multicore_launch_core1(core1_main);
#if CONFIG_FUJINET
  /* FUJINET: the client is baked into this firmware, so there is no menu to
   * build and no selectgame.bin to load. */
  fuji_config_boot();
#else
  videopacMenu();
  sleep_ms(160);
  
  load_file("/selectgame.bin");
#endif

 
  	// overclocking isn't necessary for most functions - but XEGS carts weren't working without it
	// I guess we might as well have it on all the time.
    
	set_sys_clock_khz(270000, true);
    //set_sys_clock_khz(170000, true);


  memset(extram,0,0xff);
 
    
  // Initial conditions 
  curPath[0]=0;
#if CONFIG_FUJINET
  /* FUJINET: core0's whole job is the USB link and the mailbox. Booting a game
   * is core1's doing -- it swaps the ROM pointer on the console's next reset --
   * so nothing here waits on it. */
  while (1) {
	 tud_task();
	 fuji_mailbox_service();
  }
#endif
  while (1) {
	 cmd_executing=false;
   
    if ((gamechoosen>=1)) {

	sleep_ms(1400);

	load_newfile(gamelist[gamechoosen-1]);
	//reset();
	memcpy(rom_table,new_rom_table,1024*32);
	
	newgame=1;
	
   }
  }
}

 	
