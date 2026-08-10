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
#include "hardware/sync.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"
#include "pico/bootrom.h"


#include "rom.h"

#include "tusb.h"
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
// The local flash/FatFs ROM browser (directory scan, IntyMenu, LoadGame,
// load_file()/load_cfg() reading from the RP2040's internal flash filesystem)
// was removed along with flash_fs/fatfs_disk/msc_disk -- see HARDWARE.md in
// PiRTOII-Fuji and the design plan. ROMs now arrive only over the network
// FujiBus link (dbc_inbound_handler() / feed_rom_byte() below), which was
// always a separate, independent implementation from this removed code.
#pragma GCC push_options
#pragma GCC optimize ("O0")
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
////////////////////////////////////////////////////////////////////////////////////
//                     ROM boot receiver (FUJI_DEVICEID_DBC)
////////////////////////////////////////////////////////////////////////////////////
//
// The ESP32-S3's MediaTypeROM::mount() (lib/media/rs232/diskTypeROM.cpp in
// the main fujinet-firmware tree) pushes a ROM -- and, if one exists, a
// same-named .cfg sibling -- to us over the same USB-CDC link ordinary
// mailbox transactions use, addressed to FUJI_DEVICEID_DBC, WHILE the
// Intellivision's own MOUNT_IMAGE mailbox transaction is still outstanding
// in fuji_mailbox_service()'s fujibus_transact() call below. There is no
// separate boot command anywhere in this protocol -- MOUNT_IMAGE is
// forwarded to the ESP32 exactly like any other Fuji-device command, and
// the ROM push rides the SAME link, mid-transaction. dbc_inbound_handler()
// is registered as fujibus_transact()'s inbound-frame handler (see
// fujibus_set_inbound_handler() in Inty_cart_main()): it intercepts these
// DBC frames and ACKs each one individually -- the ESP32 side's
// sendCommand() is a per-frame round trip (one OPEN, then one WRITE per
// ~512-byte chunk, then CLOSE), not one call for the whole transfer.
//
// SCOPE NOTE, established the hard way: the original design staged the
// whole transfer into a buffer separate from ROM[], committing (copying
// into ROM[], updating the mapping tables, calling resetCart()) only once
// it fully succeeded -- so a network hiccup partway through would leave
// the still-running config program untouched and able to report the
// error. A real build against this board (a classic RP2040, 264KB SRAM)
// showed that plan doesn't fit: even a 64KB scratch buffer overflowed
// `.bss` by 54KB, meaning well under 16KB of SRAM is actually free once
// ROM[]/RAM[]/the existing FatFs and USB buffers/etc. are accounted for.
// So ROM data is instead decoded and written DIRECTLY into the live
// ROM[] as it streams in (feed_rom_byte()), the same way load_file()
// already does for the local-SD path. This means a transfer that fails
// or is interrupted partway leaves the console needing a power cycle,
// same as that existing local-SD path already accepts -- not the
// cleaner "config keeps running and shows an error" behavior originally
// intended. The one piece that IS still fully buffered before anything
// commits is the (small, <=2KB) .cfg sibling: a failed .cfg fetch just
// means "no cfg", handled before any ROM bytes have been touched.
//
// Mapping precedence, decided in apply_boot_mapping() once CLOSE arrives
// on the ROM stream: a received .cfg sibling's [mapping] segments
// (address-translation only -- no [memattr] RAM segments or `p`-prefixed
// HACK/MACRO lines; see parse_cfg_mapping()) if one was pushed, else
// whatever feed_rom_byte() detected and decoded live -- a self-describing
// non-bankswitched Intellicart/CC3 .rom header, or (if the first bytes
// didn't match one) a same-file-order size table for a bare .bin.
//
// NETCMD_OPEN's payload byte selects the stream: 1 = the .cfg sibling
// (pushed first, so its mapping is known before the ROM's own CLOSE
// triggers the boot), 0 = the ROM itself.

#define CFG_BUF_MAX 2048
static char cfg_buf[CFG_BUF_MAX];
static uint32_t cfg_wp = 0;
static bool cfg_open = false;
static bool have_cfg = false;

#define BOOT_MAX_SEGS 8
static unsigned int boot_mapfrom[BOOT_MAX_SEGS];
static unsigned int boot_mapto[BOOT_MAX_SEGS];
static unsigned int boot_maprom[BOOT_MAX_SEGS];
static int boot_nsegs = 0;

// ROM-stream decode state -- see feed_rom_byte(). Format is detected from
// the first 3 bytes received; everything after that is interpreted
// incrementally, one byte at a time, writing words straight into their
// final ROM[] destination as soon as each is complete.
typedef enum { ROMFMT_UNKNOWN, ROMFMT_FLAT, ROMFMT_HDR, ROMFMT_REJECTED } romfmt_t;
static romfmt_t rom_fmt;
static uint8_t rom_hdrbuf[3];
static unsigned rom_hdrlen;
static uint32_t rom_total_bytes;
static bool rom_open = false;

// ROMFMT_FLAT: sequential big-endian word packing straight into ROM[0..),
// exactly like load_file() does for a local-SD .bin.
static uint32_t flat_wp;
static bool flat_have_hi;
static uint8_t flat_hi;

// ROMFMT_HDR: incremental Intellicart/CC3 .rom decode, one segment at a
// time. Reference: jzIntv's src/icart/icartrom.c icartrom_decode(), which
// this is a byte-for-byte port of the relevant (non-bankswitched) subset
// of. Layout per segment: 2 bytes (lo, hi) giving an inclusive page range
// (word address = page<<8, hi's page is the start of its last 256-word
// block), then 2*(wordcount) bytes of big-endian ROM data for that exact
// range, then a 2-byte CRC-16 (received but not validated).
typedef enum { RH_SEG_ADDR, RH_SEG_DATA, RH_SEG_CRC } rh_state_t;
static rh_state_t rh_state;
static uint8_t rh_nseg, rh_seg_i;
static uint8_t rh_addrbuf[2];
static unsigned rh_addrlen;
static unsigned int rh_lo, rh_hi;
static uint32_t rh_words_left;
static uint32_t rh_cur_addr;
static bool rh_have_hi;
static uint8_t rh_hi_byte;
static unsigned rh_crclen;

// Sends a bare ACK reply frame for one DBC command, over the same tud_cdc
// link fujibus_transact() itself uses to send requests. This runs from
// inside fujibus_transact()'s own RX wait (via the inbound-handler
// callback), not through fujibus_transact() itself, so it duplicates that
// function's small send loop rather than calling it.
static void dbc_send_frame(uint8_t command)
{
    uint8_t buf[16];
    size_t len = fujibus_build_request(FUJI_DEVICEID_DBC, command, NULL, 0, NULL, 0,
                                        buf, sizeof(buf));
    if (!len)
        return;
    size_t sent = 0;
    while (sent < len) {
        uint32_t w = tud_cdc_write(&buf[sent], (uint32_t)(len - sent));
        sent += w;
        tud_cdc_write_flush();
        tud_task();
    }
    tud_cdc_write_flush();
}

// segment_hits_mailbox: true if the console-visible range [from, from+len]
// (inclusive) overlaps the $8000-$9FFF mailbox/RAM window. Applied to
// every segment before a mapping is committed -- a game ROM segment can't
// be mapped on top of that window, since apply_boot_mapping() always
// re-reserves it as a RAM segment (same as the boot menu's own mapfrom[2])
// so a loaded game can still reach fuji_mailbox_service() post-boot.
static bool segment_hits_mailbox(unsigned int from, unsigned int len)
{
    unsigned int to = from + len;
    return !(to < 0x8000 || from > 0x9FFF);
}

// parse_cfg_mapping: minimal in-memory .cfg parser covering [mapping]
// lines only (`$xxxx - $yyyy = $zzzz`, whitespace-tolerant). Unlike
// load_cfg() (the PiRTO menu's own parser, for local flash via a FatFs FIL
// handle), this does NOT support [memattr] RAM segments or `p`-prefixed
// HACK/MACRO lines -- a network-pushed mapping is limited to plain ROM
// address-translation segments for now. Operates on the in-memory cfg_buf
// filled by dbc_inbound_handler(), not a file handle.
static bool parse_cfg_mapping(void)
{
    boot_nsegs = 0;
    const char *p = cfg_buf;
    const char *end = cfg_buf + cfg_wp;

    while (p < end && boot_nsegs < BOOT_MAX_SEGS) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        const char *line_end = nl ? nl : end;
        size_t linelen = (size_t)(line_end - p);

        if (linelen > 0 && p[0] == '$') {
            unsigned int from = 0, to = 0, rom = 0;
            char tmp[80];
            size_t copylen = linelen < sizeof(tmp) - 1 ? linelen : sizeof(tmp) - 1;
            memcpy(tmp, p, copylen);
            tmp[copylen] = 0;
            if (sscanf(tmp, " $%x - $%x = $%x", &from, &to, &rom) == 3) {
                boot_mapfrom[boot_nsegs] = from;
                boot_mapto[boot_nsegs] = to;
                boot_maprom[boot_nsegs] = rom;
                boot_nsegs++;
            }
        }

        p = nl ? nl + 1 : end;
    }

    return boot_nsegs > 0;
}

// rom_start_segment_slot: called the instant a ROMFMT_HDR segment's
// address range is known, registering it as a boot_* mapping entry right
// away -- there's no separate buffer holding decoded segments to batch
// this from later.
static void rom_start_segment_slot(void)
{
    if (boot_nsegs < BOOT_MAX_SEGS) {
        boot_mapfrom[boot_nsegs] = rh_lo;
        boot_mapto[boot_nsegs] = rh_hi - 1;
        boot_maprom[boot_nsegs] = rh_lo; // direct-mapped: identity
        boot_nsegs++;
    }
}

// feed_rom_byte: the incremental ROM-stream decoder. Called once per byte
// of every NETCMD_WRITE payload on the ROM stream (stream 0). See the
// section banner for the RAM-budget reasoning behind writing straight
// into ROM[] rather than a scratch buffer.
//
// KNOWN LIMITATION: bank-switched .rom images (which encode their live
// page tables in the 48-byte attribute/fine-address table following the
// segments) are not handled -- decode succeeds and the direct-mapped
// segments boot, but a title that relies on bank-switching to reach code
// outside its listed segments will not run correctly.
static void feed_rom_byte(uint8_t b)
{
    rom_total_bytes++;

    if (rom_fmt == ROMFMT_UNKNOWN) {
        rom_hdrbuf[rom_hdrlen++] = b;
        if (rom_hdrlen < 3)
            return;
        if ((rom_hdrbuf[0] == 0xA8 || rom_hdrbuf[0] == 0x41 || rom_hdrbuf[0] == 0x61) &&
            rom_hdrbuf[1] > 0 && (uint8_t)(rom_hdrbuf[1] ^ rom_hdrbuf[2]) == 0xFF) {
            rom_fmt = ROMFMT_HDR;
            rh_nseg = rom_hdrbuf[1];
            rh_seg_i = 0;
            rh_state = RH_SEG_ADDR;
            rh_addrlen = 0;
            boot_nsegs = 0;
        } else {
            rom_fmt = ROMFMT_FLAT;
            flat_wp = 0;
            flat_have_hi = false;
            // The 3 header-probe bytes are real file data in flat mode --
            // replay them now that the format is known. All 3 bytes were
            // already counted by the natural calls that got us here, and
            // each replay call will count itself again, so back out all 3
            // (not just this call's own +1) first.
            rom_total_bytes -= 3;
            feed_rom_byte(rom_hdrbuf[0]);
            feed_rom_byte(rom_hdrbuf[1]);
            feed_rom_byte(rom_hdrbuf[2]);
        }
        return;
    }

    if (rom_fmt == ROMFMT_REJECTED)
        return;

    if (rom_fmt == ROMFMT_FLAT) {
        if (!flat_have_hi) {
            flat_hi = b;
            flat_have_hi = true;
        } else {
            if (flat_wp < (uint32_t)BINLENGTH)
                ROM[flat_wp++] = ((uint16_t)flat_hi << 8) | b;
            flat_have_hi = false;
        }
        return;
    }

    // ROMFMT_HDR
    if (rh_seg_i >= rh_nseg)
        return; // trailing attribute/fine-address table -- not needed, ignored

    switch (rh_state) {
    case RH_SEG_ADDR:
        rh_addrbuf[rh_addrlen++] = b;
        if (rh_addrlen < 2)
            return;
        rh_lo = ((unsigned int)rh_addrbuf[0]) << 8;
        rh_hi = (((unsigned int)rh_addrbuf[1]) << 8) + 0x100;
        rh_addrlen = 0;
        if (rh_hi <= rh_lo || rh_hi > (unsigned int)BINLENGTH) {
            rom_fmt = ROMFMT_REJECTED;
            return;
        }
        rom_start_segment_slot();
        rh_words_left = rh_hi - rh_lo;
        rh_cur_addr = rh_lo;
        rh_have_hi = false;
        rh_state = RH_SEG_DATA;
        return;
    case RH_SEG_DATA:
        if (!rh_have_hi) {
            rh_hi_byte = b;
            rh_have_hi = true;
        } else {
            ROM[rh_cur_addr++] = ((uint16_t)rh_hi_byte << 8) | b;
            rh_have_hi = false;
            rh_words_left--;
            if (rh_words_left == 0) {
                rh_state = RH_SEG_CRC;
                rh_crclen = 0;
            }
        }
        return;
    case RH_SEG_CRC:
        rh_crclen++; // received but not validated
        if (rh_crclen < 2)
            return;
        rh_seg_i++;
        rh_state = RH_SEG_ADDR;
        rh_addrlen = 0;
        return;
    }
}

// apply_boot_mapping: called once the ROM stream's CLOSE arrives.
// ROM[]'s final bytes are already in place by now (feed_rom_byte() wrote
// them there live); this only decides and validates the address-
// translation tables, then commits them into the SAME mapfrom/mapto/
// maprom/mapdelta/mapsize/tipo/page/slot/RAMused globals core1's live bus
// dispatch and load_cfg()/load_file() already use, and sets romLen
// exactly as load_file() does.
static bool apply_boot_mapping(void)
{
    if (rom_fmt == ROMFMT_UNKNOWN || rom_fmt == ROMFMT_REJECTED) {
        RAM[FUJI_MB_BOOT_ERR] = FUJI_BOOT_ERR_REJECTED;
        return false;
    }

    if (rom_fmt == ROMFMT_HDR) {
        if (rh_seg_i != rh_nseg || boot_nsegs == 0) {
            RAM[FUJI_MB_BOOT_ERR] = FUJI_BOOT_ERR_TRUNCATED;
            return false; // stream ended mid-segment or before any completed
        }
    } else if (have_cfg && parse_cfg_mapping()) {
        // boot_* already populated by parse_cfg_mapping().
    } else {
        uint32_t words = rom_total_bytes / 2;
        boot_nsegs = 1;
        boot_mapfrom[0] = 0;
        boot_mapto[0] = words > 0 ? words - 1 : 0;
        switch (rom_total_bytes) {
        case 4096:  boot_maprom[0] = 0x5000; break;
        case 8192:  boot_maprom[0] = 0x5000; break;
        case 12288: boot_maprom[0] = 0x5000; break;
        case 16384: boot_maprom[0] = 0x5000; break;
        case 24576:
            boot_nsegs = 2;
            boot_mapfrom[0] = 0;      boot_mapto[0] = 0x1FFF; boot_maprom[0] = 0x5000;
            boot_mapfrom[1] = 0x2000; boot_mapto[1] = 0x2FFF; boot_maprom[1] = 0xD000;
            break;
        case 32768:
            boot_nsegs = 3;
            boot_mapfrom[0] = 0;      boot_mapto[0] = 0x1FFF; boot_maprom[0] = 0x5000;
            boot_mapfrom[1] = 0x2000; boot_mapto[1] = 0x2FFF; boot_maprom[1] = 0xD000;
            boot_mapfrom[2] = 0x3000; boot_mapto[2] = 0x3FFF; boot_maprom[2] = 0xF000;
            break;
        default:
            RAM[FUJI_MB_BOOT_ERR] = FUJI_BOOT_ERR_NOMAP;
            return false; // no mapping source at all -- refuse to guess
        }
    }

    for (int i = 0; i < boot_nsegs; i++) {
        unsigned int len = boot_mapto[i] - boot_mapfrom[i];
        if (segment_hits_mailbox(boot_maprom[i], len)) {
            RAM[FUJI_MB_BOOT_ERR] = FUJI_BOOT_ERR_MAILBOX;
            return false;
        }
    }

    slot = 0;
    RAMused = 0;
    for (int i = 0; i < boot_nsegs; i++) {
        mapfrom[slot] = boot_mapfrom[i];
        mapto[slot] = boot_mapto[i];
        maprom[slot] = boot_maprom[i];
        tipo[slot] = 0; // plain ROM, direct-mapped -- see feed_rom_byte()'s bank-switch note
        page[slot] = 0;
        addrto[slot] = maprom[slot] + (mapto[slot] - mapfrom[slot]);
        mapdelta[slot] = maprom[slot] - mapfrom[slot];
        mapsize[slot] = mapto[slot] - mapfrom[slot];
        slot++;
    }

    // Re-establish the mailbox/RAM window ($8000-$9FFF) as its own segment,
    // same as the static boot-menu table (Inty_cart_main()'s mapfrom[2])
    // does. segment_hits_mailbox() already rejected any game ROM segment
    // that overlaps this range, so it's always free here. Without this, a
    // loaded game has no RAM segment at all covering $8000-$9FFF -- the
    // window is unreachable on the physical bus post-boot, so a game that
    // (like 5card) wants to talk to fuji_mailbox_service() after loading
    // can never see the 'F'/'N' magic bytes.
    mapfrom[slot] = 0x8000;
    mapto[slot] = 0x9fff;
    maprom[slot] = 0x8000;
    tipo[slot] = 2; // RAM
    page[slot] = 0;
    addrto[slot] = maprom[slot] + (mapto[slot] - mapfrom[slot]);
    mapdelta[slot] = maprom[slot] - mapfrom[slot];
    mapsize[slot] = mapto[slot] - mapfrom[slot];
    slot++;
    RAMused = 1;
    ramfrom = 0x8000;

    slot--; // core1_main() walks 0..slot inclusive, matching load_cfg()'s own convention
    hacks = 0;

    romLen = rom_total_bytes / 2;
    RAM[base + 202] = romLen;

    return true;
}

// dbc_inbound_handler: registered with fujibus_set_inbound_handler().
// Returns true (frame consumed, keep waiting for the real MOUNT_IMAGE
// reply) for every FUJI_DEVICEID_DBC frame; false (treat as the actual
// reply) for anything else, which normally never happens mid-MOUNT_IMAGE
// but keeps this safe to register unconditionally.
static bool dbc_inbound_handler(const fb_reply_t *req)
{
    if (req->device != FUJI_DEVICEID_DBC)
        return false;

    if (req->command == NETCMD_OPEN) {
        unsigned stream = (req->data_len > 0) ? req->data[0] : 0;
        if (stream == 1) {
            cfg_wp = 0;
            cfg_open = true;
            have_cfg = false;
            RAM[FUJI_MB_BOOT_STATE] = FUJI_BOOT_OPENING;
        } else {
            rom_fmt = ROMFMT_UNKNOWN;
            rom_hdrlen = 0;
            rom_total_bytes = 0;
            rom_open = true;
            RAM[FUJI_MB_BOOT_STATE] = FUJI_BOOT_XFER;
            RAM[FUJI_MB_BOOT_PCT] = 0;
            RAM[FUJI_MB_BOOT_ERR] = 0;
        }
        dbc_send_frame(FUJICMD_ACK);
        return true;
    }

    if (req->command == NETCMD_WRITE) {
        if (cfg_open) {
            uint32_t room = CFG_BUF_MAX - cfg_wp;
            uint32_t n = req->data_len < room ? req->data_len : room;
            memcpy(cfg_buf + cfg_wp, req->data, n);
            cfg_wp += n;
        } else if (rom_open) {
            for (uint16_t i = 0; i < req->data_len; i++)
                feed_rom_byte(req->data[i]);
            // Coarse progress: total size isn't known in advance, so this
            // saturates near 100% rather than reaching it exactly until
            // CLOSE -- good enough for a moving progress bar.
            uint32_t pct = (rom_total_bytes * 100) / 32768;
            RAM[FUJI_MB_BOOT_PCT] = pct > 99 ? 99 : pct;
        }
        dbc_send_frame(FUJICMD_ACK);
        return true;
    }

    if (req->command == NETCMD_CLOSE) {
        if (cfg_open) {
            cfg_open = false;
            have_cfg = (cfg_wp > 0);
            dbc_send_frame(FUJICMD_ACK);
        } else if (rom_open) {
            rom_open = false;
            RAM[FUJI_MB_BOOT_STATE] = FUJI_BOOT_MAPPING;
            if (apply_boot_mapping()) {
                RAM[FUJI_MB_BOOT_PCT] = 100;
                dbc_send_frame(FUJICMD_ACK); // must go out before resetCart() tears down the link
                gpio_put(LED_PIN, false);
                fuji_wait_ms_pumped(200);
                resetCart();
                fuji_wait_ms_pumped(200);
                resetCart();
                memset(RAM, 0, sizeof(RAM));
                // The memset above wipes the mailbox header cells this same
                // window holds ($9800-$9802, re-mapped in as RAM by
                // apply_boot_mapping() above) along with the rest of RAM --
                // restore them so a mailbox-aware game (e.g. 5card) can
                // still find its 'F'/'N' magic once it starts running.
                RAM[FUJI_MB_MAGIC0] = 'F';
                RAM[FUJI_MB_MAGIC1] = 'N';
                RAM[FUJI_MB_PROTO_VER] = 1;
                // Return (frame consumed) instead of halting here: the real
                // MOUNT_IMAGE reply still hasn't arrived from the ESP32, so
                // fujibus_transact()'s wait loop needs control back to keep
                // reading -- and once that completes, Inty_cart_main()'s own
                // while(1) resumes, which is what keeps tud_task() and
                // fuji_mailbox_service() alive for the loaded game to use.
                // (A plain ROM that never bumps FUJI_MB_SEQ again just never
                // exercises that path -- no different from before.)
                RAM[FUJI_MB_BOOT_STATE] = FUJI_BOOT_IDLE;
                return true;
            }
            // apply_boot_mapping() already set FUJI_MB_BOOT_ERR to a
            // specific reason code. NAK (not ACK) so push_stream() on the
            // ESP32 side sees the CLOSE fail and MediaTypeROM::mount()
            // reports failure up to fujicore_mount_disk_image_success(),
            // instead of the ESP32 believing the push succeeded.
            RAM[FUJI_MB_BOOT_STATE] = FUJI_BOOT_FAILED;
            dbc_send_frame(FUJICMD_NAK);
        } else {
            dbc_send_frame(FUJICMD_ACK);
        }
        return true;
    }

    // Unrecognized DBC command -- consume it anyway (NAK) rather than
    // falling through and being mistaken for the MOUNT_IMAGE reply.
    dbc_send_frame(FUJICMD_NAK);
    return true;
}


// Services at most one FujiNet transaction per call: if the Intellivision
// has bumped FUJI_MB_SEQ since our last reply, run it and publish the
// result by writing FUJI_MB_ACKSEQ = seq last. See fuji_mailbox.h for the
// full interlock rationale.
static void fuji_mailbox_service(void)
{
    static uint8_t rxbuf[FUJI_MB_RX_MAX];
    static uint8_t txbuf[FUJI_MB_TX_MAX];

    // BOOTSEL doorbell -- see fuji_mailbox.h. Checked every call, independent
    // of the SEQ/ACKSEQ interlock below. reset_usb_boot() is noreturn: it
    // reboots straight into BOOTSEL/PICOBOOT over the same internal USB link
    // the ESP32-S3 already uses, so the cart hangs (from the Inty's point of
    // view) until either reflashed or power-cycled -- expected for a
    // deliberate "go flash me" request.
    if ((uint8_t)RAM[FUJI_MB_BOOTSEL_DOORBELL] == FUJI_MB_BOOTSEL_MAGIC)
        reset_usb_boot(1u << LED_PIN, 0);

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

    // MOUNT_IMAGE can trigger a ROM(+.cfg) push over the same link mid-
    // transaction (see the "ROM boot receiver" section above) -- a real
    // transfer over TNFS can easily run past the ordinary 5s budget every
    // other command gets.
    uint32_t timeout_ms = 5000;
    if (device == FUJI_DEVICEID_FUJINET && command == FUJICMD_MOUNT_IMAGE)
        timeout_ms = 60000;

    fb_reply_t reply;
    fb_status_t st = fujibus_transact(device, command, nparam ? params : NULL, nparam,
                                       txlen ? txbuf : NULL, txlen,
                                       rxbuf, sizeof(rxbuf), &reply, timeout_ms);

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

  fujibus_set_inbound_handler(dbc_inbound_handler);

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

  slot=2; // 3 slots per splash

  //  [mapping] (fujinet-config/intv/config.cfg)
  // $0000-$0FFF = $5000, $1000-$1B14 = $6000 -- contiguous in the .bin
  // (word offset == cart addr - $5000 throughout that span), so one
  // segment covers both .cfg lines, same as the old 5cardstud layout did.
  //$0000 - $1B14 = $5000
  mapfrom[0]=0x0;
  mapto[0]=0x1b14;
  maprom[0]=0x5000;
  tipo[0]=0;
  page[0]=0;
  addrto[0]=0x6b14;
  mapdelta[0]=maprom[0] - mapfrom[0];
  mapsize[0]=mapto[0] - mapfrom[0];

  // config.bas's st_boot.bas pushed the compiled size past the first
  // $5000-$6FFF segment, so config.bas gives it an explicit second ASM ORG
  // segment (see config.bas's comment) -- config.cfg's third [mapping]
  // line, a separate non-contiguous ROM segment.
  //$1B15 - $2058 = $D000
  mapfrom[1]=0x1b15;
  mapto[1]=0x2058;
  maprom[1]=0xd000;
  tipo[1]=0;
  page[1]=0;
  addrto[1]=0xd543;
  mapdelta[1]=maprom[1] - mapfrom[1];
  mapsize[1]=mapto[1] - mapfrom[1];

 //[memattr]
 //$8000 - $9FFF = RAM 16
 // On real hardware there is no separate mailbox device -- core1_main()'s
 // BAR/ADAR/DWS bus decode ONLY responds to an address if it falls inside
 // one of these mapped segments (see the mapfrom/maprom/mapsize loop a
 // few hundred lines below); fuji_mailbox_service() is a pure software
 // read/write against the shared RAM[] array with no bus-decode awareness
 // of its own. So the mailbox window ($9800-$9FFF, FUJI_MB_BASE) MUST be
 // included in this segment or the Inty-side CPU can never see it on the
 // physical bus at all -- config.cfg's own MEMATTR only declaring RAM
 // through $97FF is a compile-time/jzIntv-emulation detail (jzIntv models
 // the mailbox as a separate peripheral object, so its .cfg-driven RAM
 // device must stop short to avoid conflicting with it) and does not
 // apply to this real-hardware static segment table.
  RAMused=1;
  ramfrom=0x8000;
  mapfrom[2]=0x8000;
  mapto[2]=0x9fff;
  maprom[2]=0x8000;
  tipo[2]=2;
  page[2]=0;
  addrto[2]=0x9fff;
  mapdelta[2]=maprom[2] - mapfrom[2];
  mapsize[2]=mapto[2] - mapfrom[2];
  
  //sleep_ms(200);
  //resetCart();
  fuji_wait_ms_pumped(200);
  resetCart();
  fuji_wait_ms_pumped(1200);

  // The legacy PiRTO II menu protocol (RAM[0x889] cmd / RAM[0x119] ack,
  // IntyMenu()/LoadGame()/DirUp() driving the local flash browser) was
  // removed along with flash_fs/fatfs -- see the removal note above. The
  // FujiNet mailbox (fuji_mailbox_service(), $9800-$9FFF) is the only
  // command channel now; core0's job past this point is just to keep
  // pumping it and the USB device stack.
  gpio_put(LED_PIN,true);

  printf("START\n");

  while (1) {
     tud_task();
     fuji_mailbox_service();
  }
}
 	
#pragma GCC pop_options
