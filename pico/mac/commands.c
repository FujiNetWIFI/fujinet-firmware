/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

//#define FLOPPY
#undef FLOPPY
#undef DCD
//#define DCD

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/clocks.h"
#include "hardware/claim.h"
#include "hardware/dma.h"

#include "hardware/pio.h"
#include "hardware/pio_instructions.h"
#include "hardware/regs/pio.h"
#include "hardware/regs/addressmap.h"

#include "commands.pio.h"
#include "echo.pio.h"
#include "enand.pio.h"
#include "latch.pio.h"
#include "mux.pio.h"

#include "dcd_commands.pio.h"
#include "dcd_read.pio.h"
#include "dcd_write.pio.h"

// define GPIO pins
#define UART_TX_PIN 4
#define UART_RX_PIN 5
#define EN0
#define EN1
#define ENABLE      11 // was 7 - swap order of CA and EN
#define MCI_CA0     6  // was 8 - move CA down to EN
#define MCI_WR      15
#define ECHO_IN     21
#define TACH_OUT    21
#define ECHO_OUT    19
#define MUX_OUT     18
#define LATCH_OUT   18

/**
 * HERE STARTS PIO DEFINITIONS AND HEADERS
*/

PIO pioblk_read_only = pio0;
#define SM_FPY_CMD 0
#define SM_DCD_CMD 1
#define SM_DCD_READ 2

PIO pioblk_rw = pio1;
#define SM_DCD_WRITE 0
#define SM_FPY_ECHO 1
#define SM_LATCH 2
#define SM_MUX 3

uint pio_read_offset;
uint pio_write_offset;
uint pio_mux_offset;
uint pio_floppy_cmd_offset;
uint pio_dcd_cmd_offset;

void pio_commands(PIO pio, uint sm, uint offset, uint pin);
void pio_echo(PIO pio, uint sm, uint offset, uint in_pin, uint out_pin, uint num_pins);
// void pio_latch(PIO pio, uint sm, uint offset, uint in_pin, uint out_pin);
void pio_mux(PIO pio, uint sm, uint offset, uint in_pin, uint mux_pin);
void pio_enand(PIO pio, uint sm, uint offset, uint in_pin, uint out_pin);
void pio_dcd_commands(PIO pio, uint sm, uint offset, uint pin);
void pio_dcd_read(PIO pio, uint sm, uint offset, uint pin);
void pio_dcd_write(PIO pio, uint sm, uint offset, uint pin);
void set_num_dcd();

/**
 * HERE IS UART SETUP
*/
#define UART_ID uart1
#define BAUD_RATE 2000000 //230400 //115200
#define DATA_BITS 8
#define STOP_BITS 1
#define PARITY UART_PARITY_NONE

void setup_esp_uart()
{
    uart_init(UART_ID, BAUD_RATE);

    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    uart_set_hw_flow(UART_ID, false, false);
    uart_set_format(UART_ID, DATA_BITS, STOP_BITS, PARITY);
    uart_set_fifo_enabled(UART_ID, true);
}

/**
 * DATA NEEDED FOR OPERATION
*/
uint32_t a;
uint32_t b[12];
    char c;
    char current_track;
uint64_t last_time = 0;
uint32_t olda;
uint32_t active_disk_number;
uint num_dcd_drives;

/**
 * LATCH INFORMATION AND CODE
*/

__aligned(32) bool latch_lut[24];

// latches stored at bool LUT's. Floppy is 16 bits and dcd is 8 bits,
// represented by bool in array. Array is addressed via DMA using following addresses:

// 0b0xxxx   0..15 is floppy
// 0b10xxx  16..24 is dcd

// the dcd latch is the same for all the dcd drives - no unique info
// floppy latches have many unique settings (e.g., CSTIN, TK0, SIDES)
// so if we add the internal floppy, we need another 16 values.
// if we add another address bit using the internal ENABLE line
// then we can expand the lut to 24+16 = 40 bools (aligned to 64 bytes)
// floppy is 16 bits and dcd is 8 bits
// 0b00xxxx   0..15 is internal floppy
// 0b01xxxx  16..31 is unassigned
// 0b10xxxx  32..47 is external floppy
// 0b110xxx  48..55 is dcd
// 0b111xxx  56..63 is unassigned 
//
// this means putting the internal ENABLE on pin DISKFLAG+1 = 13
// or rather move CA0 down to 6 and put the two ENABLES above DISKFLAG
//
// not sure if there's enough SM instructions left to force a floppy type when 
// the internal ENABLE is activated

/**
 * 800 KB GCR Drive
CA2	    CA1	    CA0	    SEL	    RD Output       PIO             addr
Low	    Low	    Low	    Low	    !DIRTN          latch           0
Low	    Low	    Low	    High	!CSTIN          latch           1
Low	    Low	    High	Low	    !STEP           latch           2
Low	    Low	    High	High	!WRPROT         latch           3
Low	    High	Low	    Low	    !MOTORON        latch           4
Low	    High    Low     High    !TK0            latch           5
Low	    High	High	Low	    SWITCHED        latch           6
Low	    High	High	High	!TACH           tach            7
High	Low	    Low	    Low	    RDDATA0         echo            8
High	Low	    Low	    High	RDDATA1         echo            9
High	Low	    High	Low	    SUPERDRIVE      latch           a
High	Low	    High	High	+               latch           b
High	High	Low	    Low	    SIDES           latch           c
High	High	Low	    High	!READY          latch           d
High	High	High	Low	    !DRVIN          latch           e
High	High	High	High	REVISED         latch           f
+ TODO

Signal Descriptions
Signal Name	Description
!DIRTN	Step direction; low=toward center (+), high=toward rim (-)
!CSTIN	Low when disk is present
!STEP	Low when track step has been requested
!WRPROT	Low when disk is write protected or not inserted
!MOTORON	Low when drive motor is on
!TK0	Low when head is over track 0 (outermost track)
SWITCHED	High when disk has been changed since signal was last cleared
!TACH	Tachometer; frequency reflects drive speed in RPM
INDEX	Pulses high for ~2 ms once per rotation
RDDATA0	Signal from bottom head; falling edge indicates flux transition
RDDATA1	Signal from top head; falling edge indicates flux transition
SUPERDRIVE	High when a Superdrive (FDHD) is present
MFMMODE	High when drive is in MFM mode
SIDES	High when drive has a top head in addition to a bottom head
!READY	Low when motor is at proper speed and head is ready to step
!DRVIN	Low when drive is installed
REVISED	High for double-sided double-density drives, low for single-sided double-density drives
PRESENT/!HD	High when a double-density (not high-density) disk is present on a high-density drive
DCDDATA	Communication channel from DCD device to Macintosh
!HSHK	Low when DCD device is ready to receive or wishes to send

*/

// info from Apple Computer Drawing Number 699-0452-A
enum latch_bits {
    DIRTN = 0,      // 0 !DIRTN	     This signal sets the direction of head motion. A zero sets direction toward the center of the disk and a one sets direction towards outer edge. When IENBL is high IDIRTN is set to zero. Change of !DIRTN command is not allowed during head movement nor head settlying time.
    STEP,           // 1 !STEP	     At the falling edge of this signal the destination track counter is counted up or down depending on the ID'IIRTN level. After the destination counter in the drive received the falling edge of !STEP, the drive sets !STEP to high.
    MOTORON,        // 2 !MOTORON	 When this signal is set to low, the disk motor is turned on. When IENBL is high, /MOTORON is set to high.
    EJECT,          // 3 EJECT       At the rising edge of the LSTRB, EJECT is set to high and the ejection operation starts. EJECT is set to low at rising edge of ICSTIN or 2 sec maximum after rising edge of EJECT. When power is turned on, EJECT is set to low.
    DATA0,          // 4             read data - not from latch but comes from RMT device
    na0101,         // 5             not assigned in latch
    SINGLESIDE,     // 6 !SINGLESIDE A status bit which is read as one for double sided drive.
    DRVIN,          // 7 !DRVIN      This status bit is read as a zero only if the selected drive is connected to the host computer.
    CSTIN,          // 8 !CSTIN      This status bit is read as a zero only when a diskette is in the drive or when the mechanism for ejection and insertion is at the disk-in position without diskette.
    WRTPRT,         // 9 !WRTPRT     This status bit is read as a zero only when a write protected diskette is in the drive or no diskette is inserted in the drive.
    TKO,            // a !TKO        This status bit is read as a zero when a head is on track 00 or outer position of track 00. NOTE: rrKO is an output signal of a latch whose status is decided by the track 00 sensor only while the drive is not in power save mode.
    TACH,           // b             tachometer - generated by the RP2040 clock device
    DATA1,          // c             read data - not from latch but comes from RMT device
    na1101,         // d             not assigned in latch
    READY,          // e !READY      This status line is used to indicate that the host system can read the recorded data on the disk or write data to the disk. IREADY is a zero when the head position is settled on disired track, motor is at the desired speed, and a diskette is in the drive.
    REVISED         // f REVISED     This status line is used to indicate that the interface definition of the connected external drive. When REVISED is a one, the drive Part No. will be 699-0326 or when REVISED is a zero, the drive Part No. will be 699-0285.
};

uint16_t get_latch() 
{
  int latch = 0;
  for (int i=0; i<16; i++)
    latch |= ((int)latch_lut[i] << i);
  return (uint16_t)(latch & 0xffff);
}

uint8_t dcd_get_latch() 
{
  int latch = 0;
  for (int i=0; i<8; i++)
    latch |= ((int)latch_lut[16 + i] << i);
  return (uint8_t)(latch & 0xff);
}

void set_latch(enum latch_bits s) { latch_lut[s] = true; }
void clr_latch(enum latch_bits c) { latch_lut[c] = false; }
bool latch_val(enum latch_bits s) { return latch_lut[s]; }

void dcd_set_latch(uint8_t s) { latch_lut[16 + s] = true; }
void dcd_clr_latch(uint8_t c) { latch_lut[16 + c] = false; }


void dcd_assert_hshk()
{                   // State	CA2	  CA1	  CA0	  HOST	HOFF	RESET	RD Function
  dcd_clr_latch(2); // 2	    Low	  High	Low	  Low	  Low	  Low	  !HSHK
  dcd_clr_latch(3); // 3	    Low	  High	High	High	Low	  Low	  !HSHK
  // pio_sm_put_blocking(pioblk_rw, SM_LATCH, dcd_get_latch()); // send the register word to the PIO
}

void dcd_deassert_hshk()
{                   // State	CA2	  CA1	  CA0	  HOST	HOFF	RESET	RD Function
  dcd_set_latch(2); // 2	    Low	  High	Low	  Low	  Low	  Low	  !HSHK
  dcd_set_latch(3); // 3	    Low	  High	High	High	Low	  Low	  !HSHK
  // pio_sm_put_blocking(pioblk_rw, SM_LATCH, dcd_get_latch()); // send the register word to the PIO
}

void preset_latch()
{
  for (int i = 0; i < 16; i++)
  {
    // latch_lut[i] = true;
    set_latch(i);
  }
  // set up like an empty floppy
    // latch =-1;
    // clr_latch(DIRTN);
    // set_latch(STEP);
    // set_latch(MOTORON);
    clr_latch(EJECT);
    // set_latch(na0101); // to contrast to DCD, but this is SUPERDRIVE
    clr_latch(SINGLESIDE); // clear it to start so in constrast with DCD if necessary
    clr_latch(DRVIN); // low because the drive is present
    // set_latch(CSTIN); // no disk in drive
    clr_latch(WRTPRT);
    // set_latch(TKO);
    // set_latch(READY);
    // set_latch(REVISED);   // my mac plus revised looks set
    // for (int i=0; i<16; i++)
    //   printf("\nlatch bit %02d = %d",i, latch_val(i));
    printf("\nFloppy Latch: %04x", get_latch());
}

void dcd_preset_latch()
{
  // dcd_latch = 0;
                    // State	CA2	  CA1	  CA0	  HOST	HOFF	RESET	RD Function
  dcd_clr_latch(0); // 0	    Low	  Low	  Low	  High	High	Low	  Data
  dcd_clr_latch(1); // 1	    Low	  Low	  High	High	Low	  Low	  Data
  dcd_set_latch(2); // 2	    Low	  High	Low	  Low	  Low	  Low	  !HSHK
  dcd_set_latch(3); // 3	    Low	  High	High	High	Low	  Low	  !HSHK
  dcd_set_latch(4); // 4	    High	Low	  Low	  Low	  Low	  High	--
  dcd_clr_latch(5); // 5	    High	Low	  High	Low	  Low	  Low	  Drive Low
  dcd_set_latch(6); // 6	    High	High	Low	  Low	  Low	  Low	  Drive High
  dcd_set_latch(7); // 7	    High	High	High	Low	  Low	  Low	  Drive High
  printf("\nDCD Latch: %02x", dcd_get_latch());
}

void set_tach_freq(char c, char wobble)
{
  const int tach_lut[5][3] = {{0, 15, 394},
                            {16, 31, 429},
                            {32, 47, 472},
                            {48, 63, 525},
                            {64, 79, 590}};
  
  // To configure a clock, we need to know the following pieces of information:
  // The frequency of the clock source
  // The mux / aux mux position of the clock source
  // The desired output frequency
  // use 125 MHZ PLL as a source
  
  for (int i = 0; i < 5; i++)
  {
    if ((c >= tach_lut[i][0]) && (c <= tach_lut[i][1]))
      clock_gpio_init_int_frac(TACH_OUT, CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_CLK_SYS, 125 * MHZ / (tach_lut[i][2]+wobble), 0);
  }
}

void switch_to_floppy()
{
  // commands
  while (gpio_get(LSTRB));  
  pio_sm_set_enabled(pioblk_read_only, SM_DCD_CMD, false); // stop the DCD command interpreter
  pio_commands(pioblk_read_only, SM_FPY_CMD, pio_floppy_cmd_offset, MCI_CA0); // read phases starting on pin 8
}

void switch_to_dcd()
{
  // commands
  pio_sm_set_enabled(pioblk_read_only, SM_FPY_CMD, false); // stop the floppy command interpreter
  pio_dcd_commands(pioblk_read_only, SM_DCD_CMD, pio_dcd_cmd_offset, MCI_CA0); // read phases starting on pin 8
}

void set_num_dcd()
{
      // need to set number in DCD mux PIO and reset the machine
      // pause the machine, change the instruction, move the PC, resume
      pio_sm_set_enabled(pioblk_rw, SM_MUX, false);
      uint32_t save = hw_claim_lock();
      pioblk_rw->instr_mem[pio_mux_offset] = pio_encode_set(pio_x, num_dcd_drives) | pio_encode_sideset_opt(3, 0);
      hw_claim_unlock(save);
      pio_mux(pioblk_rw, SM_MUX, pio_mux_offset, MCI_CA0, MUX_OUT);
}

int chan_latch_addr, chan_latch_data;
dma_channel_config cfg_latch_addr, cfg_latch_data;

void setup()
{
  uint offset;

    stdio_init_all();
    setup_default_uart();
    setup_esp_uart();

    // merged setup
    current_track = 0;
    set_tach_freq(current_track, 0); // start TACH clock
    preset_latch();
    dcd_preset_latch();

    /** 
     * put the read-only SM's in PIO0: floppy cmd, dcd cmd, dcd read
     * configure as if DCD is default ON
    */
    pio_floppy_cmd_offset = pio_add_program(pioblk_read_only, &commands_program);
    printf("\nLoaded Floppy cmd program at %d", pio_floppy_cmd_offset);
    // pio_commands(pioblk_read_only, SM_FPY_CMD, pio_floppy_cmd_offset, MCI_CA0); // read phases starting on pin 8
    
    pio_dcd_cmd_offset = pio_add_program(pioblk_read_only, &dcd_commands_program);
    printf("\nLoaded DCD commands program at %d", pio_dcd_cmd_offset);
    pio_dcd_commands(pioblk_read_only, SM_DCD_CMD, pio_dcd_cmd_offset, MCI_CA0); // read phases starting on pin 8

    pio_read_offset = pio_add_program(pioblk_read_only, &dcd_read_program);
    printf("\nLoaded DCD read program at %d\n", pio_read_offset);
    pio_dcd_read(pioblk_read_only, SM_DCD_READ, pio_read_offset, MCI_WR);

    /** 
     * put the output SM's in PIO1: echo, dcd write, common latch
     * 
     * from Section 3.5.6.1 in RP2040 datasheet:
     * For each GPIO, PIO collates the writes from all four state machines, 
     * and applies the write from the highest-numbered state machine. 
     * This occurs separately for output levels and output values — 
     * it is possible for a state machine to change both the level and 
     * direction of the same pin on the same cycle (e.g. via simultaneous
     * SET and side-set), or for one state  machine to change a GPIO’s 
     * direction while another changes that GPIO’s level.
    */
    offset = pio_add_program(pioblk_rw, &latch_program);
    printf("\nLoaded latch program at %d\n", offset);
    latch_program_init(pioblk_rw, SM_LATCH, offset, MCI_CA0, LATCH_OUT);
    pio_sm_put(pioblk_rw, SM_LATCH, (uintptr_t)latch_lut >> 5);
    pio_sm_exec_wait_blocking(pioblk_rw, SM_LATCH, pio_encode_pull(false, true));
    pio_sm_exec_wait_blocking(pioblk_rw, SM_LATCH, pio_encode_mov(pio_y, pio_osr));
    pio_sm_exec_wait_blocking(pioblk_rw, SM_LATCH, pio_encode_out(pio_null, 1)); 
    pio_sm_set_enabled(pioblk_rw, SM_LATCH, true);

    chan_latch_addr = dma_claim_unused_channel(true);
    cfg_latch_addr = dma_channel_get_default_config(chan_latch_addr);

    chan_latch_data = dma_claim_unused_channel(true);
    cfg_latch_data = dma_channel_get_default_config(chan_latch_data);
 
    channel_config_set_read_increment(&cfg_latch_data,false);
    channel_config_set_write_increment(&cfg_latch_data,false);
    channel_config_set_dreq(&cfg_latch_data, pio_get_dreq(pioblk_rw, SM_LATCH, true)); // mux PIO
    channel_config_set_chain_to(&cfg_latch_data, chan_latch_addr);
    channel_config_set_transfer_data_size(&cfg_latch_data, DMA_SIZE_8);
    channel_config_set_irq_quiet(&cfg_latch_data, true);
    channel_config_set_enable(&cfg_latch_data, true);
    dma_channel_configure(
        chan_latch_data,                          // Channel to be configured
        &cfg_latch_data,                        // The configuration we just created
        &pioblk_rw->txf[SM_LATCH],                   // The initial write address
        latch_lut,                      // The initial read address
        1,                                  // Number of transfers; in this case each is 1 byte.
        false                               // do not Start immediately.      
      );

    channel_config_set_read_increment(&cfg_latch_addr,false);
    channel_config_set_write_increment(&cfg_latch_addr,false);
    channel_config_set_dreq(&cfg_latch_addr, pio_get_dreq(pioblk_rw, SM_LATCH, false)); // mux PIO
    channel_config_set_chain_to(&cfg_latch_addr, chan_latch_data);
    channel_config_set_transfer_data_size(&cfg_latch_addr, DMA_SIZE_32);
    channel_config_set_irq_quiet(&cfg_latch_addr, true);
    channel_config_set_enable(&cfg_latch_addr, true);
    dma_channel_configure(
        chan_latch_addr,                          // Channel to be configured
        &cfg_latch_addr,                        // The configuration we just created
        &dma_channel_hw_addr(chan_latch_data)->read_addr,   // The initial write address
        &pioblk_rw->rxf[SM_LATCH],            // The initial read address
        1,                                  // Number of transfers; in this case each is 1 byte.
        true                               // do not Start immediately.      
      );

    offset = pio_add_program(pioblk_rw, &echo_program);
    printf("Loaded floppy echo program at %d\n", offset);
    pio_echo(pioblk_rw, SM_FPY_ECHO, offset, ECHO_IN, ECHO_OUT, 2);

    pio_write_offset = pio_add_program(pioblk_rw, &dcd_write_program);
    printf("Loaded DCD write program at %d\n", pio_write_offset);
    pio_dcd_write(pioblk_rw, SM_DCD_WRITE, pio_write_offset, LATCH_OUT);

    pio_mux_offset = pio_add_program(pioblk_rw, &mux_program);
    printf("Loaded mux program at %d\n", pio_mux_offset);
    pio_mux(pioblk_rw, SM_MUX, pio_mux_offset, MCI_CA0, MUX_OUT);
}

void dcd_loop();
void floppy_loop();
void esp_loop();

enum disk_mode_t {
  DCD = 0,
  TO_FPY,
  FPY,
  TO_DCD
} disk_mode = DCD;

int main()
{
  setup();
  // switch_to_floppy();
  while (true)
  {
    esp_loop();
    switch (disk_mode)
    {
    case DCD:
      dcd_loop();
      break;
    case TO_FPY:
      switch_to_floppy();
      disk_mode = FPY;
      // fall thru
    case FPY:
      floppy_loop();
      break;
    case TO_DCD:
      switch_to_dcd();
      disk_mode = DCD;
    default:
      break;
    }
  }
}

void esp_loop()
{
  if (uart_is_readable(UART_ID))
  {
    c = uart_getc(UART_ID);
    // handle comms from ESP32
    switch (c)
    {
    case 'h': // harddisk is mounted/unmounted
      num_dcd_drives = uart_getc(UART_ID);
      printf("\nNumber of DCD's mounted: %d", num_dcd_drives);
      set_num_dcd(); // tell SM_MUX how many DCD's and restart it
      c = 0; // need to clear c so not picked up by floppy loop although it would never respond to 'h'
      break;
    case 's':
      // single sided disk is in the slot
      clr_latch(SINGLESIDE);
      clr_latch(CSTIN);
      clr_latch(WRTPRT); // everythign is write protected for now
      printf("\nSS disk mounted");
      break;
    case 'd':
      // double sided disk
      set_latch(SINGLESIDE);
      clr_latch(CSTIN);
      clr_latch(WRTPRT); // everythign is write protected for now
      printf("\nDS disk mounted");
      break;
    default:
      break;
    }
  }

  if (!pio_sm_is_rx_fifo_empty(pioblk_rw, SM_MUX))
  {
    int m = pio_sm_get_blocking(pioblk_rw, SM_MUX);
    // printf("m%dm",m);
    if (m != 0)
    {
      active_disk_number = num_dcd_drives + 'A' - m;
      printf("%c", active_disk_number);
      uart_putc_raw(UART_ID, active_disk_number);
    }
    else
    {
      // if (!latch_val(CSTIN))
      // pio_sm_put_blocking(pioblk_rw, SM_LATCH, get_latch()); // send the register word to the PIO
      if (disk_mode != FPY)
        disk_mode = TO_FPY;
    }
  }
}

void floppy_loop()
{
  static bool step_state = false;

  // I settled on a flutter cycle period of 640 ms and a flutter amplitude of about 0.25%.
  // tach ranges approx 400-600 Hz, so that's about 1.0-1.5 Hz. Call it 1 Hz.
  static char wobble = 1;

  if (gpio_get(ENABLE) && (num_dcd_drives > 0))
  {
    disk_mode = TO_DCD;
    return;
  }

  if (!pio_sm_is_rx_fifo_empty(pioblk_read_only, SM_FPY_CMD))
  {
    a = pio_sm_get_blocking(pioblk_read_only, SM_FPY_CMD);
    // printf("%d",a);
    if (latch_val(CSTIN))
      return;
    switch (a)
    {
      // !READY
      // This status line is used to indicate that the host system can read the recorded data on the disk or write data to the disk.
      // !READY is a zero when
      //      the head position is settled on disired track,
      //      motor is at the desired speed,
      //      and a diskette is in the drive.
    case 0:
      // !DIRTN
      // This signal sets the direction of head motion.
      // A zero sets direction toward the center of the disk and a one sets direction towards outer edge.
      // When !ENBL is high !DIRTN is set to zero.
      // Change of !DIRTN command is not allowed during head movement nor head settlying time.
      // set direction to increase track number
      clr_latch(DIRTN);
      break;
    case 4:
      set_latch(DIRTN);
      break;
    case 1:
      // !STEP
      // At the falling edge of this signal the destination track counter is counted up or down depending on the !DIRTN level.
      // After the destination counter in the drive received the falling edge of !STEP, the drive sets !STEP to high.
      // step the head
      clr_latch(STEP);
      set_latch(READY);
      step_state = true;
      break;
    case 2:
      // !MOTORON
      // When this signal is set to low, the disk motor is turned on.
      // When !ENBL is high, /MOTORON is set to high.
      // turn motor on
      clr_latch(MOTORON);
      set_latch(READY);
      break;
    case 6:
      // turn motor off
      set_latch(MOTORON);
      set_latch(READY);
      break;
    case 7:
      // EJECT
      // At the rising edge of the LSTRB, EJECT is set to high and the ejection operation starts.
      // EJECT is set to low at rising edge of !CSTIN or 2 sec maximum after rising edge of EJECT.
      // When power is turned on, EJECT is set to low.
      // eject
      set_latch(EJECT); // gets cleared when ESP responds with 'E'
      set_latch(READY);
      break;
    default:
      printf("\nUNKNOWN PHASE COMMAND");
      break;
      }
    uart_putc_raw(UART_ID, (char)(a + '0'));
    }

    // !STEP - a little state machine with !STEP required to make this work
    // At the falling edge of this signal the destination track counter is counted up or down depending on the !DIRTN level.
    // After the destination counter in the drive received the falling edge of !STEP, the drive sets !STEP to high.
    if (step_state)
    {
      set_latch(STEP);
      step_state = false;
    }

    if (c != 0)
    {

    // !READY
    // This status line is used to indicate that the host system can read the recorded data on the disk or write data to the disk.
    // !READY is a zero when
    //      the head position is settled on disired track,
    //      motor is at the desired speed,
    //      and a diskette is in the drive.

    // to do: figure out when to clear !READY
    if (c & 128)
    {
      current_track = c & 127;
      if (current_track == 0)
          clr_latch(TKO); // at track zero
      else
          set_latch(TKO);
      set_tach_freq(current_track, 0);
    }
    else
      switch (c)
      {
      case 'E':
        // EJECT
        //  At the rising edge of the LSTRB, EJECT is set to high and the ejection operation starts.
        //  EJECT is set to low at rising edge of !CSTIN or 2 sec maximum after rising edge of EJECT.
        //  When power is turned on, EJECT is set to low.
          set_latch(CSTIN);
          clr_latch(EJECT);
          printf("\nFloppy Ejected");
        break;
      case 'S':             // step complete (data copied to RMT buffer on ESP32)
          printf("\nStep sequence complete");
          clr_latch(READY); // hack - really should not set READY low until the 3 criteria are met
          break;
      case 'M':             // motor on
          printf("\nMotor is on");
          clr_latch(READY); // hack - really should not set READY low until the 3 criteria are met
          break;
      default:
          break;
      }
      // printf("latch %04x", get_latch());
      c = 0; // clear c because processed it and don't want infinite loop
    }
    // to do: read both enable lines and indicate which drive is active when sending single char to esp32

  if ((time_us_64() - last_time) > 640*1000)
    {
      last_time = time_us_64();
      set_tach_freq(current_track, wobble);
      wobble = -wobble;
    }

}

struct dcd_triad {
  uint8_t sync;
  uint8_t num_rx;
  uint8_t num_tx;
} cmd;

bool host = false;

// forward declarations
void dcd_process(uint8_t nrx, uint8_t ntx);

void dcd_loop()
{
  if (num_dcd_drives == 0)
  {
    disk_mode = TO_FPY;
    return;
  }

  // if (!pio_sm_is_rx_fifo_empty(pioblk_rw, SM_MUX))
  // {
  //   int m = pio_sm_get_blocking(pioblk_rw, SM_MUX);
  //   // printf("m%dm",m);
  //   if (m != 0)
  //   {
  //     active_disk_number = num_dcd_drives + 'A' - m;
  //     printf("%c", active_disk_number);
  //     uart_putc_raw(UART_ID, active_disk_number);
  //   }
  //   else
  //   {
  //     // if (!latch_val(CSTIN))
  //     // pio_sm_put_blocking(pioblk_rw, SM_LATCH, get_latch()); // send the register word to the PIO 
  //     disk_mode = TO_FPY;
  //     return;
  //   }
  // }

  if (!pio_sm_is_rx_fifo_empty(pioblk_read_only, SM_DCD_CMD))
  {
    olda = a;
    a = pio_sm_get_blocking(pioblk_read_only, SM_DCD_CMD);
    switch (a)
    {
      case 0: 
        host = true;
        break;
      case 1: // for now receiving a command
        host = true;
        // The Macintosh's data transfer begins with three bytes which are not encoded using the 7-to-8 encoding. 
        // The first is an 0xAA "sync" byte. 
        // The second indicates the number of 7-to-8-encoded groups contained in the transfer to follow, plus 0x80 (because the MSB must be set). 
        // The third indicates the number of 7-to-8-encoded groups that the Macintosh expects to receive in response, plus 0x80. 
        // These three bytes are followed by 7-to-8-encoded groups, the number of which was indicated by the second byte.
        cmd.sync = pio_sm_get_blocking(pioblk_read_only, SM_DCD_READ);
        assert(cmd.sync == 0xaa);
        cmd.num_rx = pio_sm_get_blocking(pioblk_read_only, SM_DCD_READ);
        cmd.num_tx = pio_sm_get_blocking(pioblk_read_only, SM_DCD_READ);
        dcd_process(cmd.num_rx & 0x7f, cmd.num_tx & 0x7f);
        break;
    case 2:
      host = false;
      // idle
      // if (olda == 6)
      // {
      //   dcd_assert_hshk();
      // }
      break;
    case 3: // handshake
      host = true;
      //  printf("\nHandshake\n");
      if (olda == 2)
      {
        pio_dcd_read(pioblk_read_only, SM_DCD_READ, pio_read_offset, MCI_WR); // re-init
        pio_sm_set_enabled(pioblk_read_only, SM_DCD_READ, true);
        dcd_assert_hshk();
      }
      break;
    case 4:
      host = false;
      dcd_deassert_hshk();
      pio_sm_set_enabled(pioblk_rw, SM_DCD_WRITE, false);
      pio_sm_set_enabled(pioblk_rw, SM_LATCH, true);
      pio_sm_exec(pioblk_rw, SM_MUX, pio_encode_set(pio_pindirs, 0)); // to do - does this do anything because out pins not set?
      break;
    default:
      host = false;
      dcd_deassert_hshk();
      break;
    }
    printf("%c", a + '0');
  }

}

uint8_t payload[539];

inline static void send_byte(uint8_t c)
{
  pio_sm_put_blocking(pioblk_rw, SM_DCD_WRITE, c << 24);
}

void handshake_before_send()
{
    dcd_assert_hshk();
    a = pio_sm_get_blocking(pioblk_read_only, SM_DCD_CMD);
  assert(a==3); // now back to idle and awaiting DCD response
    a = pio_sm_get_blocking(pioblk_read_only, SM_DCD_CMD);
  assert(a==1); // now back to idle and awaiting DCD response
  // to do: handshaking error recovery -
  // case 1: TNFS seek timeout and abort - need to capture on LogAn to see what's going on
}

void handshake_after_send()
{
  a = pio_sm_get_blocking(pioblk_read_only, SM_DCD_CMD);
  assert(a==3);
  a = pio_sm_get_blocking(pioblk_read_only, SM_DCD_CMD);
  assert(a==2); // now back to idle and awaiting DCD response
}


void send_packet(uint8_t ntx)
{

  handshake_before_send();

  // send the response packet encoding along the way
  pio_sm_set_enabled(pioblk_rw, SM_LATCH, false);
  pio_dcd_write(pioblk_rw, SM_DCD_WRITE, pio_write_offset, LATCH_OUT);
  pio_sm_set_enabled(pioblk_rw, SM_DCD_WRITE, true);
  send_byte(0xaa);
  // send_byte(ntx | 0x80); - NOT SENT - OOPS
  uint8_t *p = payload;
  for (int i=0; i<ntx; i++)
  {
    // first check for holdoff
    while (!pio_sm_is_tx_fifo_empty(pioblk_rw, SM_DCD_WRITE))
      ;
    if (!pio_sm_is_rx_fifo_empty(pioblk_read_only, SM_DCD_CMD))
    {
      a = pio_sm_get_blocking(pioblk_read_only, SM_DCD_CMD);
      assert(a == 0);
      a = pio_sm_get_blocking(pioblk_read_only, SM_DCD_CMD);
      assert(a == 1);
      send_byte(0xaa);
      }
    uint8_t lsb = 0;
    for (int j = 0; j < 7; j++)
    {
      // lsb <<= 1;
      lsb |= ((*p) & 0x01) << j;
      send_byte(((*p) >> 1) | 0x80);
      p++;
    }
    send_byte(lsb | 0x80); 
  }
  // printf("\nsent %d\n",ct);
  // send_byte(0xff); // send_byte(0x80);
  send_byte(0x00); // dummy data for a pause to allow the last byte to send 
  while (!pio_sm_is_tx_fifo_empty(pioblk_rw, SM_DCD_WRITE))
    ;
  
  dcd_deassert_hshk();
  pio_sm_set_enabled(pioblk_rw, SM_DCD_WRITE, false); // re-aquire the READ line for the LATCH function
  pio_sm_set_enabled(pioblk_rw, SM_LATCH, true);

  handshake_after_send();
}

void compute_checksum(int n)
{
  uint8_t sum = 0;
  for (int i = 0; i < n; i++)
    sum += payload[i];
  sum = ~sum;
  sum++;
  payload[n] = (uint8_t)(0xff & sum);
}


void dcd_read(uint8_t ntx)
{
  /*Macintosh:

  Offset	Value
  0	0x00
  1	Number of sectors to read
  2-4	Sector offset (big-endian)
  5	0x00
  6	Checksum
  DCD Device:

  Offset	Value
  0	0x80
  1	Number of sectors remaining to be read (including sector contained in this response)
  2-5	Status
  6-25	Tags of sector being read (20 bytes)
  26-537	Data of sector being read (512 bytes)
  538	Checksum

  The DCD device then repeats this response for each sector the Macintosh has requested
  to be read with the value at offset 1 in the response counting down, beginning at the
  value at offset 1 in the command and ending at 0x01.
  */

//  printf("\nRead Command: ");
//   for (int i=0; i<7; i++)
//     printf("%02x ",payload[i]);
//   while(1);

  
  uint8_t num_sectors = payload[1];
  uint32_t sector = (payload[2] << 16) + (payload[3] << 8) + payload[4];
  // uint32_t sector_offset = ((uint32_t)payload[2] << 16) + ((uint32_t)payload[3] << 8) + (uint32_t)payload[4];
  printf("read %d sectors\n", num_sectors);

  // clear out UART buffer cause there was a residual byte
  while(uart_is_readable(UART_ID))
    uart_getc(UART_ID);

  for (uint8_t i=0; i<num_sectors; i++)
  {
    // printf("sending sector %06x in %d groups\n", sector, ntx);
    
    uart_putc_raw(UART_ID, 'R');
    uart_putc_raw(UART_ID, (sector >> 16) & 0xff);
    uart_putc_raw(UART_ID, (sector >> 8) & 0xff);
    uart_putc_raw(UART_ID, sector & 0xff);
    sector++;

    memset(payload, 0, sizeof(payload));
    payload[0] = 0x80;
    payload[1] = num_sectors-i;

    uart_read_blocking(UART_ID, &payload[26], 512);
    for (int x=0; x<16; x++)
    {
      printf("%02x ", payload[26+x]);
    }
    printf("\n");
    compute_checksum(538);

    send_packet(ntx);

  }
}

void dcd_write(uint8_t ntx, bool cont, bool verf)
{
  /*Macintosh:
    Offset	Value
    0	0x01
    1	Number of sectors remaining to be written (including sector contained in this response)
    2-4	Sector offset (big-endian)
    5	0x00
    6-25	Tags of sector to be written (20 bytes)
    26-537	Data of sector to be written (512 bytes)
    538	Checksum

OR

    Macintosh (if more than one sector is to be written):
    Offset	Value
    0	0x41
    1	Number of sectors remaining to be written (including sector contained in this response)
    2-5	0x00 0x00 0x00 0x00
    6-25	Tags of sector to be written (20 bytes)
    26-537	Data of sector to be written (512 bytes)
    538	Checksum

  Response:
  DCD Device:
  Offset	Value
    0	0x81
    1	Number of sectors remaining to be written (including sector contained in last command)
    2-5	Status
    6	Checksum

  The Macintosh then repeats this command (and the DCD device repeats its response above)
  for each sector the Macintosh has requested to be written with the value at offset 1 in
  the command counting down, ending at 0x01.

  Write and Verify Sectors
  This command is identical to Write Sectors, except that the first byte of the initial 
  command from the Macintosh is 0x02, the first byte of the DCD's response is 0x82, 
  and the first byte of subsequent continuations of the command from the Macintosh is 0x42.

  */

  //  printf("\nRead Command: ");
  //   for (int i=0; i<7; i++)
  //     printf("%02x ",payload[i]);
  //   while(1);

  uint8_t num_sectors = payload[1];
  static uint32_t sector = 0;
  if (cont)
  {
    sector++;
  }
  else
  {
    sector = (payload[2] << 16) + (payload[3] << 8) + payload[4];
    printf("write %06x sectors\n", sector);
  }

  ///  TODO FROM HERE CHANGE FROM READ TO WRITE

  // clear out UART buffer cause there was a residual byte
  while(uart_is_readable(UART_ID))
    uart_getc(UART_ID);

  // printf("writing sector %06x in %d groups\n", sector, ntx);

  uart_putc_raw(UART_ID, 'W');
  uart_putc_raw(UART_ID, (sector >> 16) & 0xff);
  uart_putc_raw(UART_ID, (sector >> 8) & 0xff);
  uart_putc_raw(UART_ID, sector & 0xff);
  sector++;
  uart_write_blocking(UART_ID, &payload[26], 512);
  // for (int x=0; x<512; x++)
  // {
  //   printf("%02x ", payload[26+x]);
  // }
  while (!uart_is_readable(UART_ID))
    ;
  c = uart_getc(UART_ID);
  if (c=='e')
    printf("\nMac WROTE TO READONLY DISK!\n");
  // assert(c=='w'); // error handling?
  // response packet
  memset(payload, 0, sizeof(payload));
  payload[0] = (!verf) ? 0x81 : 0x82;
  payload[1] = num_sectors;

  printf("\n");
  compute_checksum(6);

  send_packet(ntx);
}

void dcd_status(uint8_t ntx)
{
   /*
  DCD Device:
    Offset	Value	Sample Value from HD20
    0	0x83
    1	0x00
    2-5	Status
    6-7	Device type	0x0001
    8-9	Device manufacturer	0x0001
    10	Device characteristics bit field (see below)	0xE6
    11-13	Number of blocks	0x009835
    14-15	Number of spare blocks	0x0045
    16-17	Number of bad blocks	0x0001
    18-69	Manufacturer reserved
    70-197	Icon (see below)
    198-325	Icon mask (see below)
    326	Device location string length
    327-341	Device location string
    342	Checksum

  */
  printf("status\n");
  // memcpy(payload,s,sizeof(s));
  memset(payload, 0, sizeof(payload));
  payload[0] = 0x83;
  
  // clear out UART buffer cause there was a residual byte
  while (uart_is_readable(UART_ID))
    uart_getc(UART_ID);

  uart_putc_raw(UART_ID, 'T');

  uart_read_blocking(UART_ID, &payload[6], 336);

  for (int x = 0; x < 16; x++)
  {
    printf("%02x ", payload[6 + x]);
  }
  printf("\n");

  compute_checksum(342);

  send_packet(ntx);
}

void dcd_id(uint8_t ntx)
{
 // to do  - move generation of the ID response packet to the ESP32 so the capacity can be generated
  /*
  DCD Device:
    Offset	Value	Sample Value from HD20
    0	0x84
    1	0x00
    2-5	Status
    6-18	Name string	"Rene-1 RM MH "
    19-21	Device type	0x000210
    22-23	Firmware revision	0x3372
    24-26	Capacity (blocks)	0x009835
    27-28	Bytes per block	0x0214
    29-30	Number of cylinders	0x0131
    31	Number of heads	0x04
    32	Number of sectors	0x20
    33-35	Number of possible spare blocks	0x00004C
    36-38	Number of spare blocks (?)	0x110100 (?)
    39-41	Number of bad blocks (?)	0x000000 (?)
    42-47	0x00 0x00 0x00 0x00 0x00 0x00
    48	Checksum
  */
  printf("id\n");
  // memcpy(payload,s,sizeof(s));
  memset(payload, 0, sizeof(payload));
  payload[0] = 0x84;
  strcpy("FujiNet DCD", &payload[6]);
  payload[20]=0x02;
  payload[21]=0x10;
  payload[22]=0x33;
  payload[23]=0x72;
  payload[25]=0x98;
  payload[26]=0x35;
  payload[27]=0x02;
  payload[28]=0x14;
  payload[29]=0x01;
  payload[30]=0x31;
  payload[31]=0x04;
  payload[32]=0x20;
  compute_checksum(48);

  send_packet(ntx);
  // simulate_packet(ntx);
  // assert(false);
}

// void dcd_unknown(uint8_t ntx)
// {
//   /*
//   DCD Device:
//     Offset	Value	Sample Value from HD20
//     0	0x83	
//     1	0x00	
//     2-5	Status	
//     6 checksum
//   */
//   printf("sending0x22 ");
//   memset(payload, 0, sizeof(payload));
//   payload[0] = 0x80 | 0x22;
//   compute_checksum(6);
//   assert(ntx==1);

//   send_packet(ntx);
//   // simulate_packet(ntx);
//   // assert(false);
// }

void dcd_format(uint8_t ntx)
{
  /*
  DCD Device:
    Offset	Value	Sample Value from HD20
    0	0x83	
    1	0x00	
    2-5	Status	
    6 checksum
  */
  memset(payload, 0, sizeof(payload));
  payload[0] = 0x80 + 0x19;
  compute_checksum(6);
  assert(ntx==1);
  printf("format\n");

  send_packet(ntx);
  // simulate_packet(ntx);
  // assert(false);
}

void dcd_verify(uint8_t ntx)
{
  /*
  Verify Format
  The meaning of this command is guessed from its use in the Erase Disk command in operation.

  The observed status from this command on an actual HD20 was 0x0000008A, 
  however, returning a status of 0x00000000 does not appear to interrupt 
  the Erase Disk operation.

  DCD Device:
    Offset	
    0	0x9a	
    1	0x00	
    2-5	Status	
    6 checksum
  */
  memset(payload, 0, sizeof(payload));
  payload[0] = 0x80 + 0x1a;
  compute_checksum(6);
  assert(ntx==1);
  printf("verify format\n");

  send_packet(ntx);
  // simulate_packet(ntx);
  // assert(false);
}

void dcd_process(uint8_t nrx, uint8_t ntx)
{
  // printf("\n\nEncoded data: aa %02x %02x ",nrx | 0x80, ntx | 0x80);
  uint8_t *p = payload;
  for (int i=0; i < nrx; i++)
  {
    // check for HOLDOFF, then handshake and wait for sync, then cont loop
    if (!pio_sm_is_rx_fifo_empty(pioblk_read_only, SM_DCD_CMD))
    {
      a = pio_sm_get_blocking(pioblk_read_only, SM_DCD_CMD);
      assert(a==0);
      while (gpio_get(MCI_WR))
        ; // WR needs to return to 0 (first sign of resume)
      a = pio_sm_get_blocking(pioblk_read_only, SM_DCD_CMD);
      assert(a==1); // resuming!
      uint8_t b = 0;
      while (b!=0xaa)
        b = pio_sm_get_blocking(pioblk_read_only, SM_DCD_READ);
      assert(b==0xaa); // should be a sync byte
    }
    uint8_t lsb = pio_sm_get_blocking(pioblk_read_only, SM_DCD_READ);
    // printf("%02x ",lsb);
    for (int j=0; j < 7; j++)
    {
      uint8_t b = pio_sm_get_blocking(pioblk_read_only, SM_DCD_READ);
      // printf("%02x ", b);
       *p = (b<<1) | (lsb & 0x01);
       lsb >>= 1;
       p++;
    }
  }
  pio_sm_set_enabled(pioblk_read_only, SM_DCD_READ, false); // stop the read state machine
  //
  // handshake
  //
  while (gpio_get(MCI_WR)); // WR needs to return to 0 (at least from a status command at boot)
  a = pio_sm_get_blocking(pioblk_read_only, SM_DCD_CMD);
  assert(a==3);
  //busy_wait_us_32(10);
  dcd_deassert_hshk();
  a = pio_sm_get_blocking(pioblk_read_only, SM_DCD_CMD);
  assert(a==2); // now back to idle and awaiting DCD response
  // busy_wait_us_32(3000);
  // dcd_assert_hshk();
  //   a = pio_sm_get_blocking(pio_dcd, SM_DCD_CMD);
  // assert(a==3); // now back to idle and awaiting DCD response
  //   a = pio_sm_get_blocking(pio_dcd, SM_DCD_CMD);
  // assert(a==1); // now back to idle and awaiting DCD response

  // //
  printf("\nPayload: ");
  for (uint8_t*ptr=payload; ptr<p; ptr++)
  {
    printf(" %02x",*ptr);
  }
  printf("\n");

  // simulate_receive_packet(nrx, ntx);
  // assert(false);

  switch (payload[0])
  {
  case 0x00:
    // read sectors
    // the boot read command is: aa 81 84 c1 81 c0 c1 80 80 80 fd
    // decoded: 00 82 00 00 00 00 7e
    // 0x82+0x7e=0x100 - check
    // going to read 0x82 (130) sectors
    // but it only asked for 4 groups back - hmmmmm 
    dcd_read(ntx);
    break;
  case 0x01:
    // write sectors
    dcd_write(ntx, false, false);
    break;
  case 0x02:
    // write sectors
    dcd_write(ntx, false, true);
    break;
  case 0x03:
    // status
    // during boot sequence:
    // Received Command Sequence: aa 81 b1 c1 81 80 80 80 80 80 fe
    // aa - sync
    // 81 - will send 1 x 7-to-8 group
    // b1 - want 0x31 (49) groups: 49*7 = 343 bytes
    // The 7-to-8 group should decode to 03 00 00 00 00 00 FD
    dcd_status(ntx);
    break;
  case 0x04:
    // Read ID
    dcd_id(ntx);
    break;
  case 0x19:
    dcd_format(ntx);
    break;
  case 0x1a:
    dcd_verify(ntx);
    break;
  // case 0x22:
  //   dcd_unknown(ntx);
  //   break;
  case 0x41:
    // cont to write sectors
    dcd_write(ntx, true, false);
    break;
  case 0x42:
    // cont to write sectors
    dcd_write(ntx, true, true);
    break;
  default:
    printf("\nnot implemented %02x\n",payload[0]);
    break;
  }
}

void pio_commands(PIO pio, uint sm, uint offset, uint pin) {
    commands_program_init(pio, sm, offset, pin);
    pio_sm_set_enabled(pio, sm, true);
}

void pio_echo(PIO pio, uint sm, uint offset, uint in_pin, uint out_pin, uint num_pins)
{
    echo_program_init(pio, sm, offset, in_pin, out_pin, num_pins);
    pio_sm_set_enabled(pio, sm, true);
}

void pio_enand(PIO pio, uint sm, uint offset, uint in_pin, uint out_pin)
{
  enand_program_init(pio, sm, offset, in_pin, out_pin);
  pio_sm_set_enabled(pio, sm, true);
}

void pio_mux(PIO pio, uint sm, uint offset, uint in_pin, uint mux_pin)
{
    mux_program_init(pio, sm, offset, in_pin, mux_pin);
    pio_sm_set_enabled(pio, sm, true);
}

void pio_dcd_commands(PIO pio, uint sm, uint offset, uint pin)
{
  dcd_commands_program_init(pio, sm, offset, pin);
  pio_sm_set_enabled(pio, sm, true);
}

void pio_dcd_read(PIO pio, uint sm, uint offset, uint pin)
{
  dcd_read_program_init(pio, sm, offset, pin);
  // set y, 0             side 0                 ; initial state is always 0
  // set x, 7             side 0                 ; bit counter
  pio_sm_exec_wait_blocking(pio, sm, pio_encode_set(pio_y, 0));
  pio_sm_exec_wait_blocking(pio, sm, pio_encode_set(pio_x, 7));
}

void pio_dcd_write(PIO pio, uint sm, uint offset, uint pin)
{
  dcd_write_program_init(pio, sm, offset, pin);
}