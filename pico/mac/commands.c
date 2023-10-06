/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

//#define FLOPPY
#undef FLOPPY
//#undef DCD
#define DCD

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/clocks.h"

#include "hardware/pio.h"

#include "commands.pio.h"
#include "echo.pio.h"
#include "latch.pio.h"
#include "mux.pio.h"

#include "dcd_latch.pio.h"
#include "dcd_commands.pio.h"
#include "dcd_mux.pio.h"
#include "dcd_read.pio.h"
#include "dcd_write.pio.h"

// #include "../../include/pinmap/mac_rev0.h"
#define UART_TX_PIN 4
#define UART_RX_PIN 5
#define ENABLE 7
#define MCI_CA0 8
#define MCI_WR   14
#define ECHO_IN 21
#define TACH_OUT 21
#define ECHO_OUT 18
#define LATCH_OUT 20

#define SM_CMD 0
#define SM_LATCH 1
#define SM_MUX 2
#define SM_ECHO 3

#define SM_DCD_LATCH 3
#define SM_DCD_CMD 1
#define SM_DCD_MUX 2
#define SM_DCD_READ 0
#define SM_DCD_WRITE 0


void pio_commands(PIO pio, uint sm, uint offset, uint pin);
void pio_echo(PIO pio, uint sm, uint offset, uint in_pin, uint out_pin, uint num_pins);
void pio_latch(PIO pio, uint sm, uint offset, uint in_pin, uint out_pin);
void pio_mux(PIO pio, uint sm, uint offset, uint in_pin, uint mux_pin);

void pio_dcd_latch(PIO pio, uint sm, uint offset, uint in_pin, uint out_pin);
void pio_dcd_commands(PIO pio, uint sm, uint offset, uint pin);
void pio_dcd_mux(PIO pio, uint sm, uint offset, uint pin);
void pio_dcd_read(PIO pio, uint sm, uint offset, uint pin);
void pio_dcd_write(PIO pio, uint sm, uint offset, uint pin);

#define UART_ID uart1
#define BAUD_RATE 2000000 //230400 //115200
#define DATA_BITS 8
#define STOP_BITS 1
#define PARITY UART_PARITY_NONE


const int tach_lut[5][3] = {{0, 15, 394},
                            {16, 31, 429},
                            {32, 47, 472},
                            {48, 63, 525},
                            {64, 79, 590}};

uint32_t a;
uint32_t b[12];
    char c;
uint32_t olda;
    
PIO pio_floppy = pio0;
PIO pio_dcd = pio1;
PIO pio_dcd_rw = pio0;
uint pio_read_offset;
uint pio_write_offset;

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

uint16_t latch;
uint8_t dcd_latch;

uint16_t get_latch() { return latch; }
uint8_t dcd_get_latch() { return dcd_latch; }

uint16_t set_latch(enum latch_bits s)
{
  latch |= (1u << s);
  return latch;
};

uint8_t dcd_set_latch(uint8_t s)
{
  dcd_latch |= (1u << s);
  return dcd_latch;
}

uint16_t clr_latch(enum latch_bits c)
{
    latch &= ~(1u << c);
    return latch;
};

uint8_t dcd_clr_latch(uint8_t c)
{
    dcd_latch &= ~(1u << c);
    return dcd_latch;
};

uint8_t dcd_assert_hshk()
{
  dcd_clr_latch(2);
  dcd_clr_latch(3);
  pio_sm_put_blocking(pio_dcd, SM_DCD_LATCH, dcd_get_latch()); // send the register word to the PIO
  return dcd_latch;
}

uint8_t dcd_deassert_hshk()
{
  dcd_set_latch(2);
  dcd_set_latch(3);
  pio_sm_put_blocking(pio_dcd, SM_DCD_LATCH, dcd_get_latch()); // send the register word to the PIO
  return dcd_latch;
}

// bool latch_val(enum latch_bits b)
// {
//     return (latch & (1u << b));
// }

void preset_latch()
{
    latch =0;
    clr_latch(DIRTN);
    set_latch(STEP);
    set_latch(MOTORON);
    clr_latch(EJECT);
    set_latch(SINGLESIDE);
    clr_latch(DRVIN);
    clr_latch(CSTIN); //set_latch(CSTIN);
    clr_latch(WRTPRT);
    set_latch(TKO);
    set_latch(READY);
    set_latch(REVISED);   // my mac plus revised looks set
    // for (int i=0; i<16; i++)
    //   printf("\nlatch bit %02d = %d",i, latch_val(i));
}

void dcd_preset_latch()
{
  dcd_latch = 0b11001100; //  DCD signature HHLx + no handshake HHxx
}

void set_tach_freq(char c)
{
  // To configure a clock, we need to know the following pieces of information:
  // The frequency of the clock source
  // The mux / aux mux position of the clock source
  // The desired output frequency
  // use 125 MHZ PLL as a source
  for (int i = 0; i < 5; i++)
  {
    if ((c >= tach_lut[i][0]) && (c <= tach_lut[i][1]))
      clock_gpio_init_int_frac(TACH_OUT, CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_CLK_SYS, 125 * MHZ / tach_lut[i][2], 0);
  }
}

void loop();
void dcd_loop();

void setup()
{
  uint offset;

    stdio_init_all();
    setup_default_uart();
    setup_esp_uart();

#ifdef FLOPPY
    set_tach_freq(0); // start TACH clock
    preset_latch();

    offset = pio_add_program(pio_floppy, &commands_program);
    printf("\nLoaded cmd program at %d\n", offset);
    pio_commands(pio_floppy, SM_CMD, offset, MCI_CA0); // read phases starting on pin 8
    
    offset = pio_add_program(pio_floppy, &echo_program);
    printf("Loaded echo program at %d\n", offset);
    pio_echo(pio_floppy, SM_ECHO, offset, ECHO_IN, ECHO_OUT, 2);
    
    offset = pio_add_program(pio_floppy, &latch_program);
    printf("Loaded latch program at %d\n", offset);
    pio_latch(pio_floppy, SM_LATCH, offset, MCI_CA0, LATCH_OUT);
    pio_sm_put_blocking(pio_floppy, SM_LATCH, get_latch()); // send the register word to the PIO         
    
    offset = pio_add_program(pio_floppy, &mux_program);
    printf("Loaded mux program at %d\n", offset);
    pio_mux(pio_floppy, SM_MUX, offset, MCI_CA0, ECHO_OUT);

#elif defined(DCD)

    dcd_preset_latch();

    offset = pio_add_program(pio_dcd, &dcd_latch_program);
    printf("\nLoaded DCD latch program at %d\n", offset);
    pio_dcd_latch(pio_dcd, SM_DCD_LATCH, offset, MCI_CA0, LATCH_OUT);
    pio_sm_put_blocking(pio_dcd, SM_DCD_LATCH, dcd_get_latch()); // send the register word to the PIO  

    offset = pio_add_program(pio_dcd, &dcd_commands_program);
    printf("Loaded DCD commands program at %d\n", offset);
    pio_dcd_commands(pio_dcd, SM_DCD_CMD, offset, MCI_CA0); 

    offset = pio_add_program(pio_dcd, &dcd_mux_program);
    printf("Loaded DCD mux program at %d\n", offset);
    pio_dcd_mux(pio_dcd, SM_DCD_MUX, offset, LATCH_OUT);

    pio_read_offset = pio_add_program(pio_dcd_rw, &dcd_read_program);
    printf("Loaded DCD read program at %d\n", pio_read_offset);
    pio_dcd_read(pio_dcd_rw, SM_DCD_READ, pio_read_offset, MCI_WR);

    pio_write_offset = pio_add_program(pio_dcd, &dcd_write_program);
    printf("Loaded DCD write program at %d\n", pio_write_offset);
    pio_dcd_write(pio_dcd, SM_DCD_WRITE, pio_write_offset, LATCH_OUT);

  // pio_sm_set_enabled(pio_dcd, SM_DCD_LATCH, false);
  // pio_sm_set_enabled(pio_dcd, SM_DCD_WRITE, true);  
  // pio_sm_put_blocking(pio_dcd, SM_DCD_WRITE, 0xaa<<24);
  // pio_sm_put_blocking(pio_dcd, SM_DCD_WRITE, 0x80<<24);
  // while(1)
  // ;
#endif // FLOPPY
}

int main()
{
    setup();
    while (true)
    {
#ifdef FLOPPY
      floppy_loop();
#elif defined(DCD)
      dcd_loop();
#endif
    }
}

bool step_state = false;

void floppy_loop()
{
    if (!pio_sm_is_rx_fifo_empty(pio_floppy, SM_CMD))
    {
    a = pio_sm_get_blocking(pio_floppy, SM_CMD);
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
      // set_latch(EJECT); // to do - need to clr eject when a disk is inserted - so cheat for now
      // set_latch(READY);
      break;
    default:
      printf("\nUNKNOWN PHASE COMMAND");
      break;
    }
    pio_sm_put_blocking(pio_floppy, SM_LATCH, get_latch()); // send the register word to the PIO
    uart_putc_raw(UART_ID, (char)(a + '0'));
    }

    // !STEP
    // At the falling edge of this signal the destination track counter is counted up or down depending on the !DIRTN level.
    // After the destination counter in the drive received the falling edge of !STEP, the drive sets !STEP to high.
    if (step_state)
    {
      set_latch(STEP);
      step_state = false;
    }

    if (uart_is_readable(uart1))
    {
    // !READY
    // This status line is used to indicate that the host system can read the recorded data on the disk or write data to the disk.
    // !READY is a zero when
    //      the head position is settled on disired track,
    //      motor is at the desired speed,
    //      and a diskette is in the drive.
    c = uart_getc(UART_ID);
    // to do: figure out when to clear !READY
    if (c & 128)
    {
      if (c == 128)
          clr_latch(TKO); // at track zero
      // set_tach_freq(c & 127);
    }
    else
      switch (c)
      {
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
      case 'S':             // step complete (data copied to RMT buffer on ESP32)
          printf("\nStep sequence complete");
      case 'M':             // motor on
          printf("\nMotor is on");
          clr_latch(READY); // hack - really should not set READY low until the 3 criteria are met
      default:
          break;
      }
    // printf("latch %04x", get_latch());
    pio_sm_put_blocking(pio_floppy, SM_LATCH, get_latch()); // send the register word to the PIO
    }
    // to do: get response from ESP32 and update latch values (like READY, TACH), push LATCH value to PIO
    // to do: read both enable lines and indicate which drive is active when sending single char to esp32
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
  // latest thoughts:
  // this loop handshakes and receives the DCD command and fires off the command handler
  // todo: make dcd_mux.pio push the DCD volume # (or floppy state) to the input FIFO.

  // thoughts:
  // this is done by a SM: during boot sequence, need to look for STRB to deal with daisy chained DCD and floppy
  // if only one HD20, then after first STRB, READ should go hi-z.
  // then maybe we get a reset? the Reset should allow READ to go output when ENABLED
  //
  // need a state variable to track changes in the "command" phase settings
  // 
  // 
  if (!pio_sm_is_rx_fifo_empty(pio_dcd, SM_DCD_CMD))
  {
    olda = a;
    a = pio_sm_get_blocking(pio_dcd, SM_DCD_CMD);
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
        cmd.sync = pio_sm_get_blocking(pio_dcd_rw, SM_DCD_READ);
        assert(cmd.sync == 0xaa);
        cmd.num_rx = pio_sm_get_blocking(pio_dcd_rw, SM_DCD_READ);
        cmd.num_tx = pio_sm_get_blocking(pio_dcd_rw, SM_DCD_READ);
        dcd_process(cmd.num_rx & 0x7f, cmd.num_tx & 0x7f);
        break;
    case 2:
      host = false;
      // idle
      // if (olda == 3)
      // {
      //   dcd_assert_hshk();
      // }
      break;
    case 3: // handshake
      host = true;
      //  printf("\nHandshake\n");
      if (olda == 2)
      {
        //pio_sm_restart(pio_dcd_rw, SM_DCD_READ);
        pio_dcd_read(pio_dcd_rw, SM_DCD_READ, pio_read_offset, MCI_WR); // re-init
        pio_sm_set_enabled(pio_dcd_rw, SM_DCD_READ, true);
        dcd_assert_hshk();
      }
      break;
    case 4:
      host = false;
      // reset
      dcd_deassert_hshk();
      pio_sm_exec(pio_dcd, SM_DCD_MUX, 0xe081); // set pindirs 1
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
  pio_sm_put_blocking(pio_dcd, SM_DCD_WRITE, c << 24);
}

void handshake_before_send()
{
    dcd_assert_hshk();
    a = pio_sm_get_blocking(pio_dcd, SM_DCD_CMD);
  assert(a==3); // now back to idle and awaiting DCD response
    a = pio_sm_get_blocking(pio_dcd, SM_DCD_CMD);
  assert(a==1); // now back to idle and awaiting DCD response
}

void handshake_after_send()
{
  a = pio_sm_get_blocking(pio_dcd, SM_DCD_CMD);
  assert(a==3);
  a = pio_sm_get_blocking(pio_dcd, SM_DCD_CMD);
  assert(a==2); // now back to idle and awaiting DCD response
}


void send_packet(uint8_t ntx)
{
  // send the response packet encoding along the way
  pio_sm_set_enabled(pio_dcd, SM_DCD_LATCH, false);
  pio_dcd_write(pio_dcd, SM_DCD_WRITE, pio_write_offset, LATCH_OUT);
  pio_sm_set_enabled(pio_dcd, SM_DCD_WRITE, true);
  send_byte(0xaa);
  // send_byte(ntx | 0x80); - NOT SENT - OOPS
  uint8_t *p = payload;
  for (int i=0; i<ntx; i++)
  {
    // first check for holdoff
    if (!pio_sm_is_rx_fifo_empty(pio_dcd, SM_DCD_CMD))
    {
      a = pio_sm_get_blocking(pio_dcd, SM_DCD_CMD);
      assert(a==0);
      a = pio_sm_get_blocking(pio_dcd, SM_DCD_CMD);
      assert(a==1);
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
  while (!pio_sm_is_tx_fifo_empty(pio_dcd, SM_DCD_WRITE))
    ;
  
  dcd_deassert_hshk();
  pio_sm_set_enabled(pio_dcd, SM_DCD_WRITE, false); // re-aquire the READ line for the LATCH function
  pio_sm_set_enabled(pio_dcd, SM_DCD_LATCH, true);

  handshake_after_send();

}

// void simulate_packet(uint8_t ntx)
// {
//   uint8_t encoded[538];
//   encoded[0] = 0xaa;
//   //encoded[1] = ntx | 0x80;
//   uint8_t *p = payload;
//   int k=2;
//   for (int i=0; i<ntx; i++)
//   {
//     uint8_t lsb = 0;
//     for (int j = 0; j < 7; j++)
//     {
//       // lsb <<= 1;
//       printf("%02x ", *p);
//       lsb |= ((*p) & 0x01) << j;
//       encoded[k] = (((*p) >> 1) | 0x80);
//       p++;
//       k++;
//     }
//     encoded[k++]=(lsb | 0x80);  
//   }

//   printf("\n");

//   for (int i=0; i<k; i++)
//     printf("%02x ", encoded[i]);
  
//   printf("\n");
// }

// void simulate_receive_packet(uint8_t nrx, uint8_t ntx)
// {
//   uint8_t encoded[538];

//   encoded[0] = 0xaa;
//   encoded[1] = nrx | 0x80;
//   encoded[2] = ntx | 0x80;
//   uint8_t *p = payload;
//   int k=3;
//   for (int i=0; i<nrx; i++)
//   {
//     uint8_t lsb = 0;
//     int k_lsb = k++;
//     for (int j = 0; j < 7; j++)
//     {
//       // lsb <<= 1;
//       printf("%02x ", *p);
//       lsb |= ((*p) & 0x01) << j;
//       encoded[k] = (((*p) >> 1) | 0x80);
//       p++;
//       k++;
//     }
//     encoded[k_lsb]=(lsb | 0x80);  
//   }

//   printf("\n");

//   for (int i=0; i<k; i++)
//     printf("%02x ", encoded[i]);
  
//   printf("\n");
// }

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
    printf("sending sector %06x in %d groups\n", sector, ntx);
    
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
    handshake_before_send();
    send_packet(ntx);

  }
}

void dcd_write(uint8_t ntx, bool cont)
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
  // while(uart_is_readable(UART_ID))
  //   uart_getc(UART_ID);

  //   printf("sending sector %06x in %d groups\n", sector, ntx);
    
  //   uart_putc_raw(UART_ID, 'R');
  //   uart_putc_raw(UART_ID, (sector >> 16) & 0xff);
  //   uart_putc_raw(UART_ID, (sector >> 8) & 0xff);
  //   uart_putc_raw(UART_ID, sector & 0xff);
  //   sector++;
    // uart_read_blocking(UART_ID, &payload[26], 512);
    // for (int x=0; x<512; x++)
    // {
    //   printf("%02x ", payload[26+x]);
    // }

    // response packet
    memset(payload, 0, sizeof(payload));
    payload[0] = 0x81;
    payload[1] = num_sectors;

    printf("\n");
    compute_checksum(6);
    handshake_before_send();
    send_packet(ntx);
}

void dcd_status(uint8_t ntx)
{
  // const uint8_t s[] = {0x83, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0xe6, 0x00, 0x98, 0x35, 0x00,
  //                      0x45, 0x00, 0x01, 0x00, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  //                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  //                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  //                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  //                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  //                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  //                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  //                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  //                      0x00, 0x00, 0x00, 0x7f, 0xff, 0xff, 0xfe, 0x80, 0x00, 0x00, 0x01, 0x80, 0x00, 0x00, 0x01, 0x80,
  //                      0x00, 0x00, 0x01, 0x80, 0x00, 0x00, 0x01, 0x80, 0x00, 0x00, 0x01, 0x80, 0x00, 0x00, 0x01, 0x88,
  //                      0x00, 0x00, 0x01, 0x80, 0x00, 0x00, 0x01, 0x80, 0x00, 0x00, 0x01, 0x7f, 0xff, 0xff, 0xfe, 0x00,
  //                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  //                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  //                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  //                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  //                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  //                      0x00, 0x00, 0x00, 0x7f, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  //                      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  //                      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f, 0xff, 0xff, 0xfe, 0x00,
  //                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  //                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
  const uint8_t icon[] = {0b00000000, 0b00000001, 0b00000000, 0b00000000,
                          0b00000000, 0b00000001, 0b00000000, 0b00000000,
                          0b00000000, 0b00000001, 0b00000000, 0b00000000,
                          0b00000000, 0b00000111, 0b11000000, 0b00000000,
                          0b00000000, 0b00001111, 0b11100000, 0b00000000,
                          0b00000000, 0b10011111, 0b11110010, 0b00000000,
                          0b00000000, 0b10011111, 0b11110010, 0b00000000,
                          0b00000111, 0b11111111, 0b11111111, 0b11100000,
                          0b00000000, 0b10011111, 0b11110010, 0b00000000,
                          0b00000000, 0b10011111, 0b11110010, 0b00000000,
                          0b00000000, 0b10001111, 0b11100010, 0b00000000,
                          0b00000111, 0b11000111, 0b11000111, 0b11000000,
                          0b00001111, 0b11101100, 0b01101111, 0b11100000,
                          0b00011111, 0b11111000, 0b00111111, 0b11110000,
                          0b00011111, 0b11110000, 0b00011111, 0b11110000,
                          0b11111111, 0b11110000, 0b00011111, 0b11111000,
                          0b00011111, 0b11110000, 0b00011111, 0b11110000,
                          0b00011111, 0b11111000, 0b00111111, 0b11110000,
                          0b00001111, 0b11101100, 0b01101111, 0b11100000,
                          0b00000111, 0b11000111, 0b11000111, 0b11000000,
                          0b00000000, 0b10000001, 0b00001111, 0b11100000,
                          0b00000000, 0b10000001, 0b00011111, 0b11110000,
                          0b00000000, 0b10000001, 0b00011111, 0b11110000,
                          0b00000011, 0b11111111, 0b11111111, 0b11111100,
                          0b00000000, 0b10000001, 0b00011111, 0b11110000,
                          0b00000000, 0b10000001, 0b00011111, 0b11110000,
                          0b00000000, 0b10000001, 0b00001111, 0b11100000,
                          0b00000000, 0b00000001, 0b00000111, 0b11000000,
                          0b00000000, 0b00000001, 0b00000010, 0b00000000,
                          0b00000000, 0b00000001, 0b00000010, 0b00000000,
                          0b00000000, 0b00000111, 0b11111111, 0b11111111,
                          0b00000000, 0b00000001, 0b00000010, 0b00000000};

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

    The device characteristics bit field is defined as follows:
    Value	Meaning
    0x80	Mountable
    0x40	Readable
    0x20	Writable
    0x10	Ejectable (see below)
    0x08	Write protected
    0x04	Icon included
    0x02	Disk in place (see below)

  */
  printf("status\n");
  // memcpy(payload,s,sizeof(s));
  memset(payload, 0, sizeof(payload));
  payload[0] = 0x83;
  payload[7] = 1;
  payload[9] = 1;
  payload[10] =  0xe6; //8 | 0x40 | 0x80; // 0xe6;
  payload[12] = 0xB0;
  memcpy(&payload[70], icon, sizeof(icon));
  memset(&payload[198],0xff,128);
  // payload[326] = 7;
  // payload[327] = 'F';
  // payload[328] = 'u';
  // payload[329] = 'j';
  // payload[330] = 'i';
  // payload[331] = 'N';
  // payload[332] = 'e';
  // payload[333] = 't';
  compute_checksum(342);

  handshake_before_send();

  send_packet(ntx);
  // simulate_packet(ntx);
  // assert(false);
}

void dcd_unknown(uint8_t ntx)
{
  /*
  DCD Device:
    Offset	Value	Sample Value from HD20
    0	0x83	
    1	0x00	
    2-5	Status	
    6 checksum
  */
  printf("sending0x22 ");
  memset(payload, 0, sizeof(payload));
  payload[0] = 0x80 | 0x22;
  compute_checksum(6);
  assert(ntx==1);

  handshake_before_send();

  send_packet(ntx);
  // simulate_packet(ntx);
  // assert(false);
}

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

  handshake_before_send();

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
    if (!pio_sm_is_rx_fifo_empty(pio_dcd, SM_DCD_CMD))
    {
      a = pio_sm_get_blocking(pio_dcd, SM_DCD_CMD);
      assert(a==0);
      while (gpio_get(MCI_WR))
        ; // WR needs to return to 0 (first sign of resume)
      a = pio_sm_get_blocking(pio_dcd, SM_DCD_CMD);
      assert(a==1); // resuming!
      uint8_t b = pio_sm_get_blocking(pio_dcd_rw, SM_DCD_READ);
      assert(b==0xaa); // should be a sync byte
    }
    uint8_t lsb = pio_sm_get_blocking(pio_dcd_rw, SM_DCD_READ);
    // printf("%02x ",lsb);
    for (int j=0; j < 7; j++)
    {
      uint8_t b = pio_sm_get_blocking(pio_dcd_rw, SM_DCD_READ);
      // printf("%02x ", b);
       *p = (b<<1) | (lsb & 0x01);
       lsb >>= 1;
       p++;
    }
  }
  pio_sm_set_enabled(pio_dcd_rw, SM_DCD_READ, false); // stop the read state machine
  //
  // handshake
  //
  while (gpio_get(MCI_WR)); // WR needs to return to 0 (at least from a status command at boot)
  a = pio_sm_get_blocking(pio_dcd, SM_DCD_CMD);
  assert(a==3);
  //busy_wait_us_32(10);
  dcd_deassert_hshk();
  a = pio_sm_get_blocking(pio_dcd, SM_DCD_CMD);
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
    dcd_write(ntx, false);
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
  case 0x19:
    dcd_format(ntx);
    break;
  case 0x22:
    dcd_unknown(ntx);
    break;
  case 0x41:

    // cont to write sectors
    dcd_write(ntx, true);
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

void pio_latch(PIO pio, uint sm, uint offset, uint in_pin, uint out_pin)
{
    latch_program_init(pio, sm, offset, in_pin, out_pin);
    pio_sm_set_enabled(pio, sm, true);
}

void pio_mux(PIO pio, uint sm, uint offset, uint in_pin, uint mux_pin)
{
    mux_program_init(pio, sm, offset, in_pin, mux_pin);
    pio_sm_set_enabled(pio, sm, true);
}

void pio_dcd_latch(PIO pio, uint sm, uint offset, uint in_pin, uint out_pin)
{
    dcd_latch_program_init(pio, sm, offset, in_pin, out_pin);
    pio_sm_set_enabled(pio, sm, true);
}

void pio_dcd_commands(PIO pio, uint sm, uint offset, uint pin)
{
  dcd_commands_program_init(pio, sm, offset, pin);
  pio_sm_set_enabled(pio, sm, true);
}

void pio_dcd_mux(PIO pio, uint sm, uint offset, uint pin)
{
  dcd_mux_program_init(pio, sm, offset, pin);
  pio_sm_set_enabled(pio, sm, true);
}

void pio_dcd_read(PIO pio, uint sm, uint offset, uint pin)
{
  dcd_read_program_init(pio, sm, offset, pin);
}

void pio_dcd_write(PIO pio, uint sm, uint offset, uint pin)
{
  dcd_write_program_init(pio, sm, offset, pin);
}