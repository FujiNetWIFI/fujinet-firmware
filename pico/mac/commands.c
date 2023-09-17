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

#define SM_DCD_LATCH 0
#define SM_DCD_CMD 1
#define SM_DCD_READ 2
#define SM_DCD_MUX 3

void pio_commands(PIO pio, uint sm, uint offset, uint pin);
void pio_echo(PIO pio, uint sm, uint offset, uint in_pin, uint out_pin, uint num_pins);
void pio_latch(PIO pio, uint sm, uint offset, uint in_pin, uint out_pin);
void pio_mux(PIO pio, uint sm, uint offset, uint in_pin, uint mux_pin);

void pio_dcd_latch(PIO pio, uint sm, uint offset, uint in_pin, uint out_pin);
void pio_dcd_commands(PIO pio, uint sm, uint offset, uint pin);
void pio_dcd_mux(PIO pio, uint sm, uint offset, uint pin);
void pio_dcd_read(PIO pio, uint sm, uint offset, uint pin);

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
uint32_t b;
    char c;
    
PIO pio_floppy = pio0;
PIO pio_dcd = pio1;

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
    printf("Loaded DCD latch program at %d\n", offset);
    pio_dcd_latch(pio_dcd, SM_DCD_LATCH, offset, MCI_CA0, LATCH_OUT);
    pio_sm_put_blocking(pio_dcd, SM_DCD_LATCH, dcd_get_latch()); // send the register word to the PIO  

    offset = pio_add_program(pio_dcd, &dcd_commands_program);
    printf("Loaded DCD commands program at %d\n", offset);
    pio_dcd_commands(pio_dcd, SM_DCD_CMD, offset, MCI_CA0); 

    offset = pio_add_program(pio_dcd, &dcd_mux_program);
    printf("Loaded DCD mux program at %d\n", offset);
    pio_dcd_mux(pio_dcd, SM_DCD_MUX, offset, LATCH_OUT);

    offset = pio_add_program(pio_floppy, &dcd_read_program);
    printf("Loaded DCD read program at %d\n", offset);
    pio_dcd_read(pio_floppy, SM_DCD_READ, offset, MCI_WR);

#endif // FLOPPY
}

int main()
{
    setup();
    while (true)
    {
#ifdef FLOPPY
      loop();
#elif defined(DCD)
      dcd_loop();
#endif
    }
}

bool step_state = false;

void loop()
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


void dcd_loop()
{
  // thoughts:
  // during boot sequence, need to look for STRB to deal with daisy chained DCD and floppy
  // if only one HD20, then after first STRB, READ should go hi-z.
  // then maybe we get a reset? the Reset should allow READ to go output when !ENABLED
  if (!pio_sm_is_rx_fifo_empty(pio_dcd, SM_DCD_CMD))
  {
    a = pio_sm_get_blocking(pio_dcd, SM_DCD_CMD);
    switch (a)
    {
      case 1: // for now receiving a command
      printf("\nReceived Command Sequence: ");
       while(1)
       {
        b = pio_sm_get_blocking(pio_floppy, SM_DCD_READ);
        printf("%02x ", b);
       }
    case 3: // handshake
      //  printf("\nHandshake\n");
      pio_sm_set_enabled(pio_floppy, SM_DCD_READ, true);
      dcd_assert_hshk();
      break;
    case 4:
      // reset
      dcd_deassert_hshk();
      pio_sm_exec(pio_dcd, SM_DCD_MUX, 0xe081); // set pindirs 1
    default:
    dcd_deassert_hshk();
      break;
    }
    printf("%c", a + '0');
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