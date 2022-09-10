#ifdef BUILD_APPLE
#include "iwm.h"
#include "fnSystem.h"
#include "fnFsTNFS.h"
#include <string.h>
#include "driver/timer.h" // contains the hardware timer register data structure
#include "../../include/debug.h"
#include "utils.h"
#include "led.h"

#include "../device/iwm/disk.h"
#include "../device/iwm/fuji.h"
#include "../device/iwm/cpm.h"

/******************************************************************************
Based on:
Apple //c Smartport Compact Flash adapter
Written by Robert Justice  email: rjustice(at)internode.on.net
Ported to Arduino UNO with SD Card adapter by Andrea Ottaviani email: andrea.ottaviani.69(at)gmail.com
SD FAT support added by Katherine Stark at https://gitlab.com/nyankat/smartportsd/
 *****************************************************************************
 * Written for FujiNet ESP32 by @jeffpiep 
 * search for "todo" to find things to work on
*/

/* pin assignments for Arduino UNO 
from  http://www.users.on.net/~rjustice/SmartportCFA/SmartportSD.htm
IDC20 Disk II 20-pin pins based on
https://www.bigmessowires.com/2015/04/09/more-fun-with-apple-iigs-disks/
*/

// hardware timer parameters for bit-banging I/O
#define TIMER_DIVIDER         (2)  //  Hardware timer clock divider
#define TIMER_SCALE           (TIMER_BASE_CLK / TIMER_DIVIDER)  // convert counter value to seconds
#define TIMER_USEC_FACTOR     (TIMER_SCALE / 1000000)
#define TIMER_100NS_FACTOR    (TIMER_SCALE / 10000000)
#define TIMER_ADJUST          0 // substract this value to adjust for overhead

//#define IWM_BIT_CELL          4 // microseconds - 2 us for fast mode
//#define IWM_TX_PW             1 // microseconds - 1/2 us for fast mode

#undef VERBOSE_IWM
//#define VERBOSE_IWM

//------------------------------------------------------------------------------
//#ifdef DEBUG
//*****************************************************************************
// Function: print_packet
// Parameters: pointer to data, number of bytes to be printed
// Returns: none
//
// Description: prints packet data for debug purposes to the serial port
//*****************************************************************************
void print_packet (uint8_t* data, int bytes)
{
  int row;
  char tbs[8];
  char xx;

  Debug_printf(("\r\n"));
  for (int count = 0; count < bytes; count = count + 16) 
  {
    sprintf(tbs, ("%04X: "), count);
    Debug_print(tbs);
    for (row = 0; row < 16; row++) {
      if (count + row >= bytes)
        Debug_print(("   "));
      else {
        Debug_printf("%02x ",data[count + row]);
      }
    }
    Debug_print(("-"));
    for (row = 0; row < 16; row++) {
      if ((data[count + row] > 31) && (count + row < bytes) && (data[count + row] < 129))
      {
        xx = data[count + row];
        Debug_print(xx);
      }
      else
        Debug_print(("."));
    }
    Debug_printf(("\r\n"));
  }
}

void print_packet(uint8_t* data)
{
  Debug_printf("\r\n");
  for (int i = 0; i < 40; i++)
  {
    if (data[i]!=0 || i==0)
      Debug_printf("%02x ", data[i]);
    else
      break;
  }
  // Debug_printf("\r\n");
}

void print_packet_wave(uint8_t* data, int bytes)
{
  int row;
  char tbs[8];

  Debug_printf(("\r\n"));
  for (int count = 0; count < bytes; count = count + 12) 
  {
    sprintf(tbs, ("%04X: "), count);
    Debug_print(tbs);
    for (row = 0; row < 12; row++) {
      if (count + row >= bytes)
        Debug_print(("         "));
      else {
        uint8_t b = data[count + row];
        for (int bnum=0; bnum<8; bnum++)
        {
          if (b & 0x80)
            Debug_print("#");
            else
            Debug_print("_");
            b <<= 1;
        }
        Debug_print(".");
         }
    }
    Debug_printf(("\r\n"));
  }
}

//#endif

//------------------------------------------------------------------------------

uint8_t iwmDevice::packet_buffer[BLOCK_PACKET_LEN] = { 0 };
uint16_t iwmDevice::packet_len = 0;
uint16_t iwmDevice::num_decoded = 0;

void iwmBus::timer_config()
{
  // configure the hardware timer for regulating bit-banging smartport i/o
  // use the idf library to get it set up
  // have own helper functions that do direct register read/write for speed

  timer_config_t config;
  config.divider = TIMER_DIVIDER; // default clock source is APB
  config.counter_dir = TIMER_COUNT_UP;
  config.counter_en = TIMER_PAUSE;
  config.alarm_en = TIMER_ALARM_DIS;

  timer_init(TIMER_GROUP_1, TIMER_1, &config);
  timer_set_counter_value(TIMER_GROUP_1, TIMER_1, 0);
  timer_start(TIMER_GROUP_1, TIMER_1);
}

inline void iwmBus::iwm_timer_latch()
{
  TIMERG1.hw_timer[1].update = 0;
}

inline void iwmBus::iwm_timer_read()
{
  iwm_timer.t0 = TIMERG1.hw_timer[1].cnt_low;
}

inline void iwmBus::iwm_timer_alarm_set(int s)
{
  iwm_timer.tn = iwm_timer.t0 + s * TIMER_100NS_FACTOR - TIMER_ADJUST;
}

inline void iwmBus::iwm_timer_alarm_snooze(int s)
{
  iwm_timer.tn += s * TIMER_100NS_FACTOR - TIMER_ADJUST; // 3 microseconds

}

inline void iwmBus::iwm_timer_wait()
{
  do
  {
    iwm_timer_latch();
    iwm_timer_read();
  } while (iwm_timer.t0 < iwm_timer.tn);
}

inline void iwmBus::iwm_timer_reset()
{
  TIMERG1.hw_timer[1].load_low = 0;
  TIMERG1.hw_timer[1].reload = 0;
}

inline void iwmBus::iwm_rddata_set()
{
  GPIO.out_w1ts = ((uint32_t)1 << SP_RDDATA);
}

inline void iwmBus::iwm_rddata_clr()
{
  GPIO.out_w1tc = ((uint32_t)1 << SP_RDDATA);
}

inline void iwmBus::iwm_rddata_enable()
{
#ifdef USE_BIT_BANG_TX
  GPIO.enable_w1ts = ((uint32_t)0x01 << SP_RDDATA);  
#endif
}

inline void iwmBus::iwm_rddata_disable()
{
#ifdef USE_BIT_BANG_TX
  GPIO.enable_w1tc = ((uint32_t)0x01 << SP_RDDATA);
#endif
}

inline bool iwmBus::iwm_wrdata_val()
{
#ifdef PINMAP_A2_REV0
  return (GPIO.in & ((uint32_t)0x01 << (SP_WRDATA)));
#else
  return (GPIO.in1.val & ((uint32_t)0x01 << (SP_WRDATA - 32)));
#endif
}

inline bool iwmBus::iwm_req_val()
{
  return (GPIO.in1.val & (0x01 << (SP_REQ-32)));
}

inline void iwmBus::iwm_extra_set()
{
#ifdef EXTRA
#ifdef PINMAP_A2_REV0
  GPIO.out_w1ts = ((uint32_t)1 << SP_EXTRA);
#else
  GPIO.out1_w1ts.data = ((uint32_t)0x01 << (SP_EXTRA - 32));
#endif
#endif
}

inline void iwmBus::iwm_extra_clr()
{
#ifdef EXTRA
#ifdef PINMAP_A2_REV0
  GPIO.out_w1tc = ((uint32_t)1 << SP_EXTRA);
#else
  GPIO.out1_w1tc.data = ((uint32_t)0x01 << (SP_EXTRA - 32));  
#endif
#endif
}

inline bool iwmBus::iwm_enable_val()
{
#ifdef PINMAP_A2_REV0
  return true;
#else
  return (GPIO.in1.val & ((uint32_t)0x01 << (SP_ENABLE - 32)));
#endif
}

//------------------------------------------------------
/** ACK and REQ
 * how ACK works, my interpretation of the iigs firmware reference.
 * ACK is normally high-Z when device is ready to receive commands.
 * host will send REQ high to make a request and send a command.
 * device responds after command is received by sending ACK low.
 * host completes command handshake by sending REQ low.
 * device signals its ready for the next step (receive/send/status)
 * by sending ACK back high.
 * 
 * how ACK works with multiple devices on bus:
 * ACK is normally high-Z (pulled up?)
 * when a device receives a command addressed to it, and it is ready
 * to respond, it'll send ACK low. (To me, this seems like a perfect
 * scenario for open collector output but I think it's a 3-state line)
 * 
 * possible circuits:
 * Disk II physical interface - ACK uses the WPROT line, which is a tri-state ls125 buffer on the
 * Disk II analog card. There's no pull up/down/load resistor. This line drives the /SR input of the 
 * ls323 on the bus interface card. I surmise that WPROT goes low or is hi-z, which doesn't 
 * reset the ls125.  
 */
inline void iwmBus::iwm_ack_clr()
{
  GPIO.out_w1tc = ((uint32_t)0x01 << SP_ACK);
}

inline void iwmBus::iwm_ack_set()
{
  GPIO.out_w1ts = ((uint32_t)0x01 << SP_ACK);
}

inline void iwmBus::iwm_ack_enable()
{
  GPIO.enable_w1ts = ((uint32_t)0x01 << SP_ACK);  
}

inline void iwmBus::iwm_ack_disable()
{
  GPIO.enable_w1tc = ((uint32_t)0x01 << SP_ACK);
}

//------------------------------------------------------

bool iwmBus::iwm_phase_val(uint8_t p)
{
#ifdef PINMAP_A2_REV0
  uint8_t phases = (uint8_t)(GPIO.in1.val & (uint32_t)0b1111);
  if (p < 4)
    return (phases >> p) & 0x01;
#else
  switch (p)
  {
  case 0:
    return (GPIO.in1.val & (0x01 << (SP_PHI0-32)));
  case 1:
    return (GPIO.in & (0x01 << SP_PHI1));
  case 2:
    return (GPIO.in1.val & (0x01 << (SP_PHI2-32)));
  case 3:
    return (GPIO.in & (0x01 << SP_PHI3));
  default: 
    break; // drop out to error message
  }
#endif
  Debug_printf("\r\nphase number out of range");
  return false;
}

iwmBus::iwm_phases_t iwmBus::iwm_phases()
{ 
  iwm_phases_t phasestate = iwm_phases_t::idle;
  // phase lines for smartport bus reset
  // ph3=0 ph2=1 ph1=0 ph0=1
  // phase lines for smartport bus enable
  // ph3=1 ph2=x ph1=1 ph0=x
  if (iwm_phase_val(1) && iwm_phase_val(3) && !iwm_phase_val(2))
    phasestate = iwm_phases_t::enable;
  else if (iwm_phase_val(0) && iwm_phase_val(2) && !iwm_phase_val(1) && !iwm_phase_val(3))
    phasestate = iwm_phases_t::reset;

#ifdef DEBUG
  if (phasestate != oldphase)
  {
    //Debug_printf("\r\n%d%d%d%d",iwm_phase_val(0),iwm_phase_val(1),iwm_phase_val(2),iwm_phase_val(3));
    switch (phasestate)
    {
    case iwm_phases_t::idle:
      Debug_printf("\r\nidle");
      break;
    case iwm_phases_t::reset:
      Debug_printf("\r\nreset");
      break;
    case iwm_phases_t::enable:
      Debug_printf("\r\nenable");
    }
    oldphase=phasestate;
  }
#endif

  return phasestate;
}

//------------------------------------------------------
bool iwmBus::spirx_get_next_sample()
{
  // int spirx_byte_ctr;
  // int spirx_bit_ctr;
  // uint8_t spirx_temp;
  if (spirx_bit_ctr > 7)
  {
    spirx_bit_ctr = 0;
    spirx_byte_ctr++;
  }
  return (((spi_buffer[spirx_byte_ctr] << spirx_bit_ctr++) & 0x80) == 0x80);
}

int iwmBus::iwm_read_packet_spi(uint8_t *a, int n) 
{ // read data stream using SPI
  iwm_timer_reset();
   
  // signal the logic analyzer
  iwm_extra_clr();
  iwm_extra_set();
  iwm_extra_clr();
  iwm_extra_set();
  iwm_extra_clr();

 


#ifdef TEXT_RX_SPI

  int pulsewidth = ((f_nyquist * f_over) * 4) / 1000000; 
  int halfwidth = pulsewidth / 2; // maybe need to account for even or odd
  int numsamples = pulsewidth * (n + 2) * 8;
  spi_len = numsamples / 8 + 1;
  // set up a test - see if i can read the buffer as written by DMA
  
  //esp_err_t ret;
  transptr = &rxtrans;
  memset(transptr, 0, sizeof(spi_transaction_t));
  memset(spi_buffer, 0xff , sizeof(spi_buffer));
  rxtrans.rx_buffer = spi_buffer; // finally send the line data
  rxtrans.rxlength = spi_len * 8;   // Data length, in bits
  rxtrans.length = spi_len * 8;   // Data length, in bits
  rxtrans.flags = 0;              

  //ret = 
  //spi_device_transmit(spirx, &trans);
  // todo: can we create a wait for req with timout function to use elsewhere?
  // it woudl return bool false when REQ does its thing or true when timeout.

     // setup a timeout counter to wait for REQ response
  iwm_timer_latch();        // latch highspeed timer value
  iwm_timer_read();      //  grab timer low word
  iwm_timer_alarm_set(1000); // logic analyzer says 40 usec


  while ( !iwm_req_val() )  
  {
    iwm_timer_latch();   // latch highspeed timer value
    iwm_timer_read(); // grab timer low word
    if (iwm_timer.t0 > iwm_timer.tn)                      // test for timeout
    { // timeout!
#ifdef VERBOSE_IWM
      // timeout
      Debug_print("t");
#endif
      //Debug_printf("\r\nREQ timeout before read");
      iwm_extra_set();
      iwm_extra_clr();
      // portENABLE_INTERRUPTS();
      return 1;
    }
  };
  spi_device_queue_trans(spirx, &rxtrans,portMAX_DELAY);
#ifdef VERBOSE_IWM
  memcpy(spi_buffer2,spi_buffer,spi_len);
  print_packet_wave(spi_buffer2,spi_len);
  print_packet_wave(spi_buffer,spi_len);
  // test print
  spirx_byte_ctr = 0; // initialize the SPI buffer sampler
  spirx_bit_ctr = 0;
  for (int i = 0; i < numsamples; i++)
  {
    Debug_print(spirx_get_next_sample());
  }
  Debug_println("end");
#endif
  // decode the packet here
  spirx_byte_ctr = 0; // initialize the SPI buffer sampler
  spirx_bit_ctr = 0;

  bool have_data = true;
  bool synced = false;
  int idx = 0;             // index into *a
  bool bit = false; // = 0;        // logical bit value
  //bool prev_bit = true;
  uint8_t rxbyte = 0;      // r23 received byte being built bit by bit
  int numbits = 8;             // number of bits left to read into the rxbyte

  bool prev_level = true;
  bool current_level; // level is signal value (fast time), bits are decoded data values (slow time)
 
  
  fnSystem.delay_microseconds(60); // wait for first sync byte
    //iwm_timer_reset();
    iwm_timer_latch();        // latch highspeed timer value
    iwm_timer_read();      //  grab timer low word
    iwm_timer_alarm_set(1); // 32 us 
  do // have_data
  {
    iwm_timer_wait();
    //iwm_timer_reset();
    iwm_timer_latch();        // latch highspeed timer value
    iwm_timer_read();      //  grab timer low word
    iwm_timer_alarm_set(synced ? 311 : 390); // 31 us for regular byte, 39 us for 10-bit sync bytes 
    //fnSystem.delay_microseconds(12); // tweaked based on execution time - could use iwm_timer to pace it off the clock
    // beginning of the byte
    // delay 2 us until middle of 4-us bit
    // spirx: iwm_timer_alarm_set(16);  // 2 usec
    // spirx: just start reading data because the SPI setup takes a while anyway
    do
    {
      // spirx: iwm_extra_clr(); // signal to LA we're in the nested loop
      // spirx: iwm_timer_wait();
      // spirx: current_level = iwm_wrdata_val();       // nxtbit:   sbic _SFR_IO_ADDR(PIND),7           ;2   ;2    ;1  ;1      ;1/2 now read a bit, cycle time is 4us
      
      // spirx: iwm_timer_alarm_set(38); // 4 usec
      bit = false; // assume no edge in this next bit
#ifdef VERBOSE_IWM
      Debug_printf("\r\npulsewidth = %d, halfwidth = %d",pulsewidth,halfwidth);
      Debug_printf("\r\nspibyte spibit intctr sampval preval rxbit rxbyte");
#endif
      int i = 0;
      while (i < pulsewidth)
      {
        iwm_extra_set();
        current_level = spirx_get_next_sample();
      // spirx: iwm_extra_set(); // signal to logic analyzer we just read the WR value
#ifdef VERBOSE_IWM
        Debug_printf("\r\n%7d %6d %6d %7d %6d %5d %6d", spirx_byte_ctr, spirx_bit_ctr, i, current_level, prev_level, bit, rxbyte);
#endif
        // sprix:
        // loop through 4 usec worth of samples looking for an edge
        // if found, jump forward 2 usec and set bit = 1;
        // otherwise, bit = 0;
        if ((prev_level != current_level))
        {
          i = halfwidth; // resync the receiver - must be halfway through 4-us period at an edge
          bit = true;
        }
        prev_level = current_level;
        i++;
        iwm_extra_clr(); // signal to LA we're in the nested loop
      }
      rxbyte <<= 1;
      rxbyte |= bit;
      //prev_bit = bit;
      
      if ((--numbits) == 0)
        break; // end of byte
    } while(true); // shouldn't this just be "while(--numbits>0)"   ?????
    if ((rxbyte == 0xc3) && (!synced))
    {
// #ifdef VERBOSE_IWM
      //Debug_printf("\r\nSYNCED!"); // This can make the guru meditate
// #endif
      synced = true;
      idx = 5;
    }
    //if (idx<n)
    //{
      a[idx++] = rxbyte;  // havebyte: st   x+,r23                         ;17                    ;2   save byte in buffer
      //if (rxbyte == 0xc8) // woohoo end of packet!
      //  return 0;
    //}
    //else
    //{
    //#ifdef VERBOSE_IWM
      // Debug_printf("\r\nRead Packet: too many bytes %d", idx); // This can make the guru meditate
    // #endif
      //iwm_extra_clr();
      // portENABLE_INTERRUPTS();
     // print_packet(a);
     // return 1;
    //}
      // wait for leading edge of next byte or timeout for end of packet
      int timeout_ctr = f_nyquist * f_over * 19 / 1000000; 
      // sprix: iwm_timer_alarm_snooze(190); // 19 usec from smartportsd assy routine
//#ifdef VERBOSE_IWM
      // Debug_printf("%02x ", rxbyte);
//#endif
      // now wait for leading edge of next byte
      do // return (GPIO.in1.val >> (pin - 32)) & 0x1;
      {
        // sprix: iwm_timer_latch();
        // sprix: iwm_timer_read();
        // sprix: if (iwm_timer.t0 > iwm_timer.tn)
        if (--timeout_ctr < 1)
        {
          // end of packet
// #ifdef VERBOSE_IWM
          //Debug_printf("\r\nEND OF PACKET!"); // This can make the guru meditate
//#endif
          have_data = false;
          break;
        }
      } while (spirx_get_next_sample() == prev_level); // while (iwm_wrdata_val() == prev_level);
      numbits = 8;                                     // ;1   8bits to read
      // } // endif
  } while (have_data); //(have_data); // while have_data
  // print_packet(a);
#endif // TEXT_RX_SPI
  return 0;
}

int IRAM_ATTR iwmBus:: iwm_read_packet(uint8_t *a, int n) 
{
  //*****************************************************************************
  // Function: iwm_read_packet
  // Parameters: packet_buffer pointer
  // Returns: status (1 = timeout, 0 = OK)
  //
  // Description: This handles the ACK and REQ lines and reads a packet into the
  // packet_buffer
  //
  //*****************************************************************************
  
  bool have_data = true;
  bool synced = false;
  int idx = 0;             // index into *a
  bool bit = 0;        // logical bit value
  bool prev_level = true; // ((uint32_t)0x01 << (SP_WRDATA - 32)); // previous value of WRDATA line
  bool current_level;  // current value of WRDATA line
  uint8_t rxbyte = 0;      // r23 received byte being built bit by bit
  int numbits = 8;             // number of bits left to read into the rxbyte

  /**
 * @brief Handle ACK and REQ lines and read a packet into packet_buffer
 * 
 * @param packet_buffer pointer
 * 
 * @returns 
 *    1 for timeout error
 *    0 all else
 * 
 * @details This function reads a packet from the SmartPort (SP) bus. 
 * The algorithm originated from the SmartPortSD Arduino project as
 * Atmel AVR assembly. It went through a near literal conversion and then
 * some changes for readibility. The algorithm is time critical as it
 * reads 250kbit/sec serial data. The SP serial data are encoded such that
 * and edge means 1 and no edge means 0. Or, the logical meaning of the 
 * current bit depends on the signal level of the previous bit. This can also
 * be interpreted as over-lapping two-bit sequences. Byte framing is done by
 * ensuring the first bit (really bit 7) of the next byte is always the 
 * opposite signal level of the last bit (bit 0) of the current byte. The
 * algorithm waits for the transition and then starts the new byte. Or,
 * that edge is the logic 1 value that all smartport bytes have to have ($80)
 */


  // 'a' is the receive buffer pointer
 // portDISABLE_INTERRUPTS(); // probably put the critical section inside the read packet function?

  iwm_timer_reset();
   
  // signal the logic analyzer
  iwm_extra_clr();
  iwm_extra_set();
  iwm_extra_clr();
  iwm_extra_set();
  iwm_extra_clr();

    // setup a timeout counter to wait for REQ response
  iwm_timer_latch();        // latch highspeed timer value
  iwm_timer_read();      //  grab timer low word
  iwm_timer_alarm_set(1000); // logic analyzer says 40 usec

  // todo: can we create a wait for req with timout function to use elsewhere?
  // it woudl return bool false when REQ does its thing or true when timeout.
  while ( !iwm_req_val() )  
  {
    iwm_timer_latch();   // latch highspeed timer value
    iwm_timer_read(); // grab timer low word
    if (iwm_timer.t0 > iwm_timer.tn)                      // test for timeout
    { // timeout!
#ifdef VERBOSE_IWM
      // timeout
      Debug_print("t");
#endif
      iwm_extra_set();
      iwm_extra_clr();
      // portENABLE_INTERRUPTS();
      return 1;
    }
  };

#ifdef VERBOSE_IWM
  // REQ received!
  Debug_print("R");
#endif


//   // we might want to just block on writedata without timout so there's
//   // faster control passed to the do-loop
//   // setup a timeout counter to wait for WRDATA to be ready response
  iwm_timer_latch();                    // latch highspeed timer value
  iwm_timer_read();    //  grab timer low word
  iwm_timer_alarm_set(320); // 32 usec - 1 byte
  while (iwm_wrdata_val())
  {
    iwm_timer_latch();   // latch highspeed timer value
    iwm_timer_read(); // grab timer low word
    if (iwm_timer.t0 > iwm_timer.tn)     // test for timeout
    {                // timeout!
#ifdef VERBOSE_IWM
      // timeout
      Debug_print("t");
#endif
      iwm_extra_set();
      iwm_extra_clr();
      // portENABLE_INTERRUPTS();
      return 1;
    }
  };

  do // have_data
  {
    // beginning of the byte
    // delay 2 us until middle of 4-us bit
    iwm_timer_alarm_set(16);  // 2 usec
    do
    {
      iwm_extra_clr(); // signal to LA we're in the nested loop
      iwm_timer_wait();

      // logic table:
      //  prev_level  current_level   decoded bit
      //  0           0               0
      //  0           1               1
      //  1           0               1
      //  1           1               0
      // this is an exclusive OR operation
      current_level = iwm_wrdata_val();       // nxtbit:   sbic _SFR_IO_ADDR(PIND),7           ;2   ;2    ;1  ;1      ;1/2 now read a bit, cycle time is 4us
      iwm_extra_set(); // signal to logic analyzer we just read the WR value
      iwm_timer_alarm_set(38); // 4 usec
      bit = prev_level ^ current_level; // could be a != because we're looking for an edge
      rxbyte <<= 1;
      rxbyte |= bit;
      prev_level = current_level;
      
      if ((--numbits) == 0)
        break; // end of byte
    } while(true);
    if ((rxbyte == 0xc3) && (!synced))
    {
      synced = true;
      idx = 5;
    }
    if (idx<n)
      a[idx++] = rxbyte; // havebyte: st   x+,r23                         ;17                    ;2   save byte in buffer
    else
    {
      //Debug_printf("\r\nRead Packet: too many bytes %d", idx); // This can make the guru meditate
      iwm_extra_clr();
      // portENABLE_INTERRUPTS();
      return 1;
    }
    // attempt to utilize 0xc8 end-packet code
    // but SP didn't end up liking it when I ack too early
    // if (rxbyte == 0xc8)
    // {
    //   have_data = false; // end of packet
    // }                    // to do - if rxbyte == 0xc8, then end of packet and can get out of here
    // else 
    // {
      // wait for leading edge of next byte or timeout for end of packet
      iwm_timer_alarm_snooze(190); // 19 usec from smartportsd assy routine
#ifdef VERBOSE_IWM
      Debug_printf("%02x", rxbyte);
#endif
      // now wait for leading edge of next byte
      do // return (GPIO.in1.val >> (pin - 32)) & 0x1;
      {
        iwm_timer_latch();
        iwm_timer_read();
        if (iwm_timer.t0 > iwm_timer.tn)
        {
          // end of packet
          have_data = false; // todo also can look for 0xc8 i think
          break;
        }
      } while (iwm_wrdata_val() == prev_level);
      numbits = 8;       // ;1   8bits to read
    // } // endif
  } while (have_data); //(have_data); // while have_data
  //           rjmp nxtbyte                        ;46  ;47               ;2   get next byte
  while (a[--idx] != 0xc8) // search for end of packet
  {
    if (!idx)
    {
      //Debug_printf("\r\nRead Packet: no end of packet marker"); // This can make the guru meditate
      a[0] = 0;
      // portENABLE_INTERRUPTS();
      iwm_extra_clr();
      return 1;
    }
  }             // endpkt:   clr  r23
  a[++idx] = 0; //           st   x+,r23               ;save zero byte in buffer to mark end
  iwm_extra_clr();
  // portENABLE_INTERRUPTS();
  return (!synced); // take care of case witness on 2/6/22 where execution entered late in command packet and sync was missed.
}

int iwmBus::iwm_read_packet_timeout(int attempts, uint8_t *a, int n)
{
  // iwm_ack_set(); // todo - is set really needed?
  iwm_ack_disable();
  for (int i = 0; i < attempts; i++)
  {
#ifdef TEXT_RX_SPI
    if (!iwm_read_packet_spi(a, n))
    {
      iwm_ack_clr(); // todo - make ack functions public so devices can call them?
      iwm_ack_enable();
#ifdef DEBUG
      print_packet(a);
#endif
      spi_device_get_trans_result(spirx, &transptr, portMAX_DELAY);
      return 0;
    }
#else
    portDISABLE_INTERRUPTS();
    if (!iwm_read_packet(a, n))
    {
      iwm_ack_clr(); // todo - make ack functions public so devices can call them?
      iwm_ack_enable();
      portENABLE_INTERRUPTS();
#ifdef DEBUG
      print_packet(a);
#endif
      return 0;
    }
    portENABLE_INTERRUPTS();
#endif
  }
#ifdef DEBUG
  print_packet(a);
#endif
  return 1;
}

void iwmBus::encode_spi_packet(uint8_t *a)
{
  // clear out spi buffer
  memset(spi_buffer, 0, sizeof(spi_buffer));
  // loop through "l" bytes of the buffer "a"
  uint16_t i=0,j=0;
  while(a[i])
  {
    // Debug_printf("\r\nByte %02X: ",a[i]);
    // for each byte, loop through 4 x 2-bit pairs
    uint8_t mask = 0x80;
    for (int k = 0; k < 4; k++)
    {
      if (a[i] & mask)
      {
        spi_buffer[j] |= 0x40;
      }
      mask >>= 1;
      if (a[i] & mask)
      {
        spi_buffer[j] |= 0x04;
      }
      mask >>= 1;
      // Debug_printf("%02x",spi_buffer[j]);
      j++;
    }
    i++;
  }
  spi_len = --j;
}

int IRAM_ATTR iwmBus::iwm_send_packet(uint8_t *a)
{
  //*****************************************************************************
  // Function: iwm_send_packet
  // Parameters: packet_buffer pointer
  // Returns: status (not used yet, always returns 0)
  //
  // Description: This handles the ACK and REQ lines and sends the packet from the
  // pointer passed to it. (packet_buffer)
  //
  //*****************************************************************************

  int idx = 0;        // reg x, index into *a
  uint8_t txbyte; // r23 transmit byte being sent bit by bit
  int numbits = 8;        // r25 counter

// Disable interrupts
// https://esp32developer.com/programming-in-c-c/interrupts/interrupts-general
// You can suspend interrupts and context switches by calling  portDISABLE_INTERRUPTS
// and the interrupts on that core should stop firing, stopping task switches as well.
// Call portENABLE_INTERRUPTS after you're done
  portDISABLE_INTERRUPTS();

  // try to cache functions
  iwm_timer_reset();
  iwm_timer_latch();
  iwm_timer_read();
  iwm_timer_alarm_set(1);
  iwm_timer_wait();
  iwm_timer_alarm_snooze(1);
  iwm_timer_wait();

  iwm_rddata_enable();

 txbyte = a[idx++];

  //print_packet(a);
  // todo should this be ack disable or ack set?
  iwm_ack_set(); // ack is already enabled by the response to the command read


#ifndef TESTTX
  // 1:        sbic _SFR_IO_ADDR(PIND),2   ;wait for req line to go high
  // setup a timeout counter to wait for REQ response
  iwm_timer_latch();        // latch highspeed timer value
  iwm_timer_read();      //  grab timer low word
  iwm_timer_alarm_set(10000); // 1 millisecond per IIgs?

  // while (!fnSystem.digital_read(SP_REQ))
  while ( !iwm_req_val() ) //(GPIO.in1.val >> (pin - 32)) & 0x1
  {
    iwm_timer_latch();   // latch highspeed timer value
    iwm_timer_read(); // grab timer low word
    if (iwm_timer.t0 > iwm_timer.tn)                      // test for timeout
    {
      // timeout!
      Debug_printf("\r\nSendPacket timeout waiting for REQ");
      iwm_rddata_disable();
      //iwm_ack_disable(); // need to release the bus if we're quitting
      portENABLE_INTERRUPTS(); // takes 7 us to execute
      return 1;
    }
  };
// ;

#ifdef VERBOSE_IWM
  // REQ received!
  Debug_print("R");
#endif
#else // TESTTX
  iwm_timer_latch();
  iwm_timer_read();
#endif // TESTTX

  // CRITICAL TO HAVE 1 US BETWEEN req AND FIRST PULSE to put the falling edge 2 us after REQ
  iwm_timer_alarm_set(5);
  iwm_timer_wait();
  iwm_rddata_set(); // elongate first pulse because we always know first byte if 0xFF
  do // beware of entry into the loop and an extended first pulse ...
  {
    do
    {
      // send MSB first, then ROL byte for next bit
      iwm_timer_latch();
      if (txbyte & 0x80)
        iwm_rddata_set();
      else
        iwm_rddata_clr();
     
      iwm_timer_read();
      iwm_timer_alarm_snooze(12); // 1 microsecond - snooze to finish off 4 us period
      iwm_timer_wait();

      iwm_rddata_clr();
      iwm_timer_alarm_set(27); // 3 microseconds - set on falling edge of pulse

      // do some updating while in 3-us low period
      if ((--numbits) == 0)
        break;

      txbyte <<= 1; //           rol  r23  
      iwm_timer_wait();
    } while (1);

    txbyte = a[idx++]; // nxtsbyte: ld   r23,x+                 ;59               ;43         ;2   get first byte from buffer
    numbits = 8; //           ldi  r25,8                  ;62               ;46         ;1   8bits to read
    iwm_timer_wait(); // finish the 3 usec low period
  } while (txbyte); //           cpi  r23,0                  ;60               ;44         ;1   zero marks end of data

  iwm_ack_clr();
#ifndef TESTTX
  iwm_timer_latch();        // latch highspeed timer value
  iwm_timer_read();      //  grab timer low word
  iwm_timer_alarm_set(5000); // 1/2 millisecond

  // while (!fnSystem.digital_read(SP_REQ))
  while (iwm_req_val()) //(GPIO.in1.val >> (pin - 32)) & 0x1
  {
    iwm_timer_latch();   // latch highspeed timer value
    iwm_timer_read(); // grab timer low word
    if (iwm_timer.t0 > iwm_timer.tn)                      // test for timeout
    {
      iwm_rddata_disable();
     // iwm_ack_disable();       // need to release the bus
      portENABLE_INTERRUPTS(); // takes 7 us to execute
      return 1;
    }
  };
#endif // TESTTX
  iwm_rddata_disable();
#ifdef DEBUG
  //print_packet(a);
#endif
  portENABLE_INTERRUPTS(); // takes 7 us to execute
  //iwm_ack_disable();       // need to release the bus
  return 0;
}

int IRAM_ATTR iwmBus::iwm_send_packet_spi(uint8_t *a)
{
  //*****************************************************************************
  // Function: iwm_send_packet_spi
  // Parameters: packet_buffer pointer
  // Returns: status (not used yet, always returns 0)
  //
  // Description: This handles the ACK and REQ lines and sends the packet from the
  // pointer passed to it. (packet_buffer)
  //
  //*****************************************************************************

  print_packet((uint8_t *)a);
  encode_spi_packet((uint8_t *)a);

  // send data stream using SPI
  esp_err_t ret;
  spi_transaction_t trans;
  memset(&trans, 0, sizeof(spi_transaction_t));
  trans.tx_buffer = spi_buffer; // finally send the line data
  trans.length = spi_len * 8;   // Data length, in bits
  trans.flags = 0;              // undo SPI_TRANS_USE_TXDATA flag

  iwm_ack_set(); // ack is already enabled by the response to the command read

  // 1:        sbic _SFR_IO_ADDR(PIND),2   ;wait for req line to go high
  // setup a timeout counter to wait for REQ response
  iwm_timer_reset();
  iwm_timer_latch();        // latch highspeed timer value
  iwm_timer_read();      //  grab timer low word
  iwm_timer_alarm_set(300000); // increased to 30 millisecond per IIgs & gsos

  // while (!fnSystem.digital_read(SP_REQ))
  while ( !iwm_req_val() ) //(GPIO.in1.val >> (pin - 32)) & 0x1
  {
    iwm_timer_latch();   // latch highspeed timer value
    iwm_timer_read(); // grab timer low word
    if (iwm_timer.t0 > iwm_timer.tn)                      // test for timeout
    {
      // timeout!
      Debug_printf("\r\nSendPacket timeout waiting for REQ");
      iwm_rddata_disable();
      //iwm_ack_disable(); // need to release the bus if we're quitting
      portENABLE_INTERRUPTS(); // takes 7 us to execute
      return 1;
    }
  };
// ;

  iwm_rddata_enable();
  iwm_rddata_clr();
  ret = spi_device_polling_transmit(spi, &trans);
  iwm_rddata_set();
  iwm_ack_clr();
  assert(ret == ESP_OK);

  iwm_timer_reset();
  iwm_timer_latch();        // latch highspeed timer value
  iwm_timer_read();      //  grab timer low word
  iwm_timer_alarm_set(10000); // 1/2 millisecond

  // while (!fnSystem.digital_read(SP_REQ))
  while (iwm_req_val()) //(GPIO.in1.val >> (pin - 32)) & 0x1
  {
    iwm_timer_latch();   // latch highspeed timer value
    iwm_timer_read(); // grab timer low word
    if (iwm_timer.t0 > iwm_timer.tn)                      // test for timeout
    {
      iwm_rddata_disable();
      Debug_println("REQ timeout");
     // iwm_ack_disable();       // need to release the bus
      //portENABLE_INTERRUPTS(); // takes 7 us to execute
      return 1;
    }
  };
  iwm_rddata_disable();
  return 0;
}

void iwmBus::setup(void)
{
  Debug_printf(("\r\nIWM FujiNet based on SmartportSD v1.15\r\n"));

  timer_config();
  Debug_printf("\r\nIWM timer started");

#ifdef PINMAP_A2_FN10
  spi_bus_config_t bus_cfg = {
      .mosi_io_num = SP_RDDATA,
      .miso_io_num = -1,
      .sclk_io_num = -1,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = 4000};
  spi_bus_initialize(VSPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
#endif

  esp_err_t ret;
  spi_device_interface_config_t devcfg = {
      .mode = 0,                         // SPI mode 0
      .clock_speed_hz = 1 * 1000 * 1000, // Clock out at 1 MHz
      .spics_io_num = -1,                // CS pin
      .queue_size = 2                    // We want to be able to queue 7 transactions at a time
  };

#ifdef PINMAP_A2_REV0
    // use same SPI as SDCARD
    ret=spi_bus_add_device(HSPI_HOST, &devcfg, &spi);
#elif defined(PINMAP_A2_FN10)
    // use different SPI than SDCARD
    ret=spi_bus_add_device(VSPI_HOST, &devcfg, &spi);
#endif
  assert(ret == ESP_OK);

#ifdef TEXT_RX_SPI
// use different SPI than SDCARD
  spi_bus_config_t bus_cfg = {
      .mosi_io_num = -1, 
      .miso_io_num = SP_WRDATA,
      .sclk_io_num = -1,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = 5000 };
   spi_device_interface_config_t rxcfg = {
      .mode = 0,                         // SPI mode 0
      .clock_speed_hz = f_over * f_nyquist, // Clock at 500 kHz x oversampling factor
      .spics_io_num = -1,                // CS pin
      .queue_size = 2                    // We want to be able to queue 7 transactions at a time
  };
  spi_bus_initialize(VSPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
  ret=spi_bus_add_device(VSPI_HOST, &rxcfg, &spirx);
#endif

  fnSystem.set_pin_mode(SP_ACK, gpio_mode_t::GPIO_MODE_OUTPUT);
  fnSystem.digital_write(SP_ACK, DIGI_LOW); // set up ACK ahead of time to go LOW when enabled
  fnSystem.digital_write(SP_ACK, DIGI_HIGH); // ID ACK for Logic Analyzer
  fnSystem.digital_write(SP_ACK, DIGI_LOW); // set up ACK ahead of time to go LOW when enabled
  //set ack (hv) to input to avoid clashing with other devices when sp bus is not enabled
  fnSystem.set_pin_mode(SP_ACK, gpio_mode_t::GPIO_MODE_INPUT); //
  
  fnSystem.set_pin_mode(SP_PHI0, gpio_mode_t::GPIO_MODE_INPUT); // REQ line
  fnSystem.set_pin_mode(SP_PHI1, gpio_mode_t::GPIO_MODE_INPUT);
  fnSystem.set_pin_mode(SP_PHI2, gpio_mode_t::GPIO_MODE_INPUT);
  fnSystem.set_pin_mode(SP_PHI3, gpio_mode_t::GPIO_MODE_INPUT);

  fnSystem.set_pin_mode(SP_WRDATA, gpio_mode_t::GPIO_MODE_INPUT);

#ifdef USE_BIT_BANG_TX
  fnSystem.set_pin_mode(SP_RDDATA, gpio_mode_t::GPIO_MODE_OUTPUT);
  fnSystem.digital_write(SP_RDDATA, DIGI_LOW);
  fnSystem.digital_write(SP_RDDATA, DIGI_HIGH); // ID RD for logic analyzer
  fnSystem.digital_write(SP_RDDATA, DIGI_LOW);
  // leave rd as input, pd6
  fnSystem.set_pin_mode(SP_RDDATA, gpio_mode_t::GPIO_MODE_INPUT); //, SystemManager::PULL_DOWN );  ot maybe pull up, too?
#endif

#ifdef PINMAP_A2_REV0
  fnSystem.set_pin_mode(SP_WREQ, gpio_mode_t::GPIO_MODE_INPUT);
  fnSystem.set_pin_mode(SP_DRIVE1, gpio_mode_t::GPIO_MODE_INPUT);
  fnSystem.set_pin_mode(SP_DRIVE2, gpio_mode_t::GPIO_MODE_INPUT);
  fnSystem.set_pin_mode(SP_EN35, gpio_mode_t::GPIO_MODE_INPUT);
  fnSystem.set_pin_mode(SP_HDSEL, gpio_mode_t::GPIO_MODE_INPUT);
  fnSystem.set_pin_mode(SP_RDDATA, gpio_mode_t::GPIO_MODE_OUTPUT);
  fnSystem.digital_write(SP_RDDATA, DIGI_HIGH); // Turn tristate buffer off by default
#else
  fnSystem.set_pin_mode(SP_ENABLE, gpio_mode_t::GPIO_MODE_INPUT);
#endif
#ifdef EXTRA
  fnSystem.set_pin_mode(SP_EXTRA, gpio_mode_t::GPIO_MODE_OUTPUT);
  fnSystem.digital_write(SP_EXTRA, DIGI_LOW);
  fnSystem.digital_write(SP_EXTRA, DIGI_HIGH); // ID extra for logic analyzer
  fnSystem.digital_write(SP_EXTRA, DIGI_LOW);
  fnSystem.digital_write(SP_EXTRA, DIGI_HIGH); // ID extra for logic analyzer
  fnSystem.digital_write(SP_EXTRA, DIGI_LOW);
  Debug_printf("\r\nEXTRA signaling line configured");
#endif
  Debug_printf("\r\nIWM GPIO configured");
}

//*****************************************************************************
// Function: encode_data_packet
// Parameters: source id
// Returns: none
//
// Description: encode 512 byte data packet for read block command from host
// requires the data to be in the packet buffer, and builds the smartport
// packet IN PLACE in the packet buffer
//*****************************************************************************
void iwmDevice::encode_data_packet(uint16_t num) 
{
  int grpbyte, grpcount;
  uint8_t checksum = 0, grpmsb;
  uint8_t group_buffer[7];

  // Calculate checksum of sector bytes before we destroy them
  for (int count = 0; count < num; count++) // xor all the data bytes
    checksum = checksum ^ packet_buffer[count];

  // Start assembling the packet at the rear and work 
  // your way to the front so we don't overwrite data
  // we haven't encoded yet

  // how many groups of 7?
  uint8_t numgrps = num / 7;
  uint8_t numodds = num % 7;

  //grps of 7
  for (grpcount = numgrps; grpcount >= 0; grpcount--) //73
  {
    memcpy(group_buffer, packet_buffer + numodds + (grpcount * 7), 7);
    // add group msb byte
    grpmsb = 0;
    for (grpbyte = 0; grpbyte < 7; grpbyte++)
      grpmsb = grpmsb | ((group_buffer[grpbyte] >> (grpbyte + 1)) & (0x80 >> (grpbyte + 1)));
    // groups start after odd bytes, which is at 13 + numodds + (numodds != 0) + 1
    int grpstart = 13 + numodds + (numodds != 0) + 1;
    packet_buffer[grpstart + (grpcount * 8)] = grpmsb | 0x80; // set msb to one

    // now add the group data bytes bits 6-0
    for (grpbyte = 0; grpbyte < 7; grpbyte++)
      packet_buffer[grpstart + 1 + (grpcount * 8) + grpbyte] = group_buffer[grpbyte] | 0x80;

  }
  
  // oddbytes
  packet_buffer[14] = 0x80; // init the oddmsb
  for (int oddcnt = 0; oddcnt < numodds; oddcnt++)
  {
    packet_buffer[14] |= (packet_buffer[oddcnt] & 0x80) >> (1 + oddcnt);
    packet_buffer[15 + oddcnt] = packet_buffer[oddcnt] | 0x80;
  }

  // header
  packet_buffer[0] = 0xff;  //sync bytes
  packet_buffer[1] = 0x3f;
  packet_buffer[2] = 0xcf;
  packet_buffer[3] = 0xf3;
  packet_buffer[4] = 0xfc;
  packet_buffer[5] = 0xff;

  packet_buffer[6] = 0xc3;  //PBEGIN - start byte
  packet_buffer[7] = 0x80;  //DEST - dest id - host
  packet_buffer[8] = id(); //SRC - source id - us
  packet_buffer[9] = 0x82;  //TYPE - 0x82 = data
  packet_buffer[10] = 0x80; //AUX
  packet_buffer[11] = 0x80; //STAT
  packet_buffer[12] = numodds | 0x80; //ODDCNT  - 1 odd byte for 512 byte packet
  packet_buffer[13] = numgrps | 0x80; //GRP7CNT - 73 groups of 7 bytes for 512 byte packet

  for (int count = 7; count < 14; count++) // now xor the packet header bytes
    checksum = checksum ^ packet_buffer[count];
  int lastidx = 14 + numodds + (numodds != 0) + numgrps * 8;
  packet_buffer[lastidx++] = checksum | 0xaa;      // 1 c6 1 c4 1 c2 1 c0
  packet_buffer[lastidx++] = (checksum >> 1) | 0xaa; // 1 c7 1 c5 1 c3 1 c1

  //end bytes
  packet_buffer[lastidx++] = 0xc8;  //pkt end
  packet_buffer[lastidx] = 0x00;  //mark the end of the packet_buffer
}

void iwmDevice::encode_data_packet() // to do overload with packet size for read?
{
  encode_data_packet(512);
  /* 
  int grpbyte, grpcount;
  uint8_t checksum = 0, grpmsb;
  uint8_t group_buffer[7];

  // Calculate checksum of sector bytes before we destroy them
  for (int count = 0; count < 512; count++) // xor all the data bytes
    checksum = checksum ^ packet_buffer[count];

  // Start assembling the packet at the rear and work 
  // your way to the front so we don't overwrite data
  // we haven't encoded yet

  //grps of 7
  for (grpcount = 72; grpcount >= 0; grpcount--) //73
  {
    memcpy(group_buffer, packet_buffer + 1 + (grpcount * 7), 7);
    // add group msb byte
    grpmsb = 0;
    for (grpbyte = 0; grpbyte < 7; grpbyte++)
      grpmsb = grpmsb | ((group_buffer[grpbyte] >> (grpbyte + 1)) & (0x80 >> (grpbyte + 1)));
    packet_buffer[16 + (grpcount * 8)] = grpmsb | 0x80; // set msb to one

    // now add the group data bytes bits 6-0
    for (grpbyte = 0; grpbyte < 7; grpbyte++)
      packet_buffer[17 + (grpcount * 8) + grpbyte] = group_buffer[grpbyte] | 0x80;

  }
  
  //total number of packet data bytes for 512 data bytes is 584
  //odd byte
  packet_buffer[14] = ((packet_buffer[0] >> 1) & 0x40) | 0x80;
  packet_buffer[15] = packet_buffer[0] | 0x80;

  packet_buffer[0] = 0xff;  //sync bytes
  packet_buffer[1] = 0x3f;
  packet_buffer[2] = 0xcf;
  packet_buffer[3] = 0xf3;
  packet_buffer[4] = 0xfc;
  packet_buffer[5] = 0xff;

  packet_buffer[6] = 0xc3;  //PBEGIN - start byte
  packet_buffer[7] = 0x80;  //DEST - dest id - host
  packet_buffer[8] = id(); //SRC - source id - us
  packet_buffer[9] = 0x82;  //TYPE - 0x82 = data
  packet_buffer[10] = 0x80; //AUX
  packet_buffer[11] = 0x80; //STAT
  packet_buffer[12] = 0x81; //ODDCNT  - 1 odd byte for 512 byte packet
  packet_buffer[13] = 0xC9; //GRP7CNT - 73 groups of 7 bytes for 512 byte packet

  for (int count = 7; count < 14; count++) // now xor the packet header bytes
    checksum = checksum ^ packet_buffer[count];
  packet_buffer[600] = checksum | 0xaa;      // 1 c6 1 c4 1 c2 1 c0
  packet_buffer[601] = checksum >> 1 | 0xaa; // 1 c7 1 c5 1 c3 1 c1

  //end bytes
  packet_buffer[602] = 0xc8;  //pkt end
  packet_buffer[603] = 0x00;  //mark the end of the packet_buffer 
  */
}

//*****************************************************************************
// Function: encode_extended_data_packet
// Parameters: source id
// Returns: none
//
// Description: encode 512 byte data packet for read block command from host
// requires the data to be in the packet buffer, and builds the smartport
// packet IN PLACE in the packet buffer
//*****************************************************************************
void iwmDevice::encode_extended_data_packet (uint8_t source)
{
  int grpbyte, grpcount;
  uint8_t checksum = 0, grpmsb;
  uint8_t group_buffer[7];

  // Calculate checksum of sector bytes before we destroy them
  for (int count = 0; count < 512; count++) // xor all the data bytes
    checksum = checksum ^ packet_buffer[count];

  // Start assembling the packet at the rear and work 
  // your way to the front so we don't overwrite data
  // we haven't encoded yet

  //grps of 7
  for (grpcount = 72; grpcount >= 0; grpcount--) //73
  {
    memcpy(group_buffer, packet_buffer + 1 + (grpcount * 7), 7);
    // add group msb byte
    grpmsb = 0;
    for (grpbyte = 0; grpbyte < 7; grpbyte++)
      grpmsb = grpmsb | ((group_buffer[grpbyte] >> (grpbyte + 1)) & (0x80 >> (grpbyte + 1)));
    packet_buffer[16 + (grpcount * 8)] = grpmsb | 0x80; // set msb to one

    // now add the group data bytes bits 6-0
    for (grpbyte = 0; grpbyte < 7; grpbyte++)
      packet_buffer[17 + (grpcount * 8) + grpbyte] = group_buffer[grpbyte] | 0x80;

  }
  
  //total number of packet data bytes for 512 data bytes is 584
  //odd byte
  packet_buffer[14] = ((packet_buffer[0] >> 1) & 0x40) | 0x80;
  packet_buffer[15] = packet_buffer[0] | 0x80;

  packet_buffer[0] = 0xff;  //sync bytes
  packet_buffer[1] = 0x3f;
  packet_buffer[2] = 0xcf;
  packet_buffer[3] = 0xf3;
  packet_buffer[4] = 0xfc;
  packet_buffer[5] = 0xff;

  packet_buffer[6] = 0xc3;  //PBEGIN - start byte
  packet_buffer[7] = 0x80;  //DEST - dest id - host
  packet_buffer[8] = source; //SRC - source id - us
  packet_buffer[9] = 0xC2;  //TYPE - 0xC2 = extended data
  packet_buffer[10] = 0x80; //AUX
  packet_buffer[11] = 0x80; //STAT
  packet_buffer[12] = 0x81; //ODDCNT  - 1 odd byte for 512 byte packet
  packet_buffer[13] = 0xC9; //GRP7CNT - 73 groups of 7 bytes for 512 byte packet

  for (int count = 7; count < 14; count++) // now xor the packet header bytes
    checksum = checksum ^ packet_buffer[count];

  packet_buffer[600] = checksum | 0xaa;      // 1 c6 1 c4 1 c2 1 c0
  packet_buffer[601] = checksum >> 1 | 0xaa; // 1 c7 1 c5 1 c3 1 c1

  //end bytes
  packet_buffer[602] = 0xc8;  //pkt end
  packet_buffer[603] = 0x00;  //mark the end of the packet_buffer

}



//*****************************************************************************
// Function: decode_data_packet
// Parameters: none
// Returns: error code, >0 = error encountered
//
// Description: decode 512 (arbitrary now) byte data packet for write block command from host
// decodes the data from the packet_buffer IN-PLACE!
//*****************************************************************************
bool iwmDevice::decode_data_packet(void)
{
  int grpbyte, grpcount;
  uint8_t numgrps, numodd;
  uint16_t numdata;
  uint8_t checksum = 0, bit0to6, bit7, oddbits, evenbits;
  uint8_t group_buffer[8];

  //Handle arbitrary length packets :) 
  numodd = packet_buffer[11] & 0x7f;
  numgrps = packet_buffer[12] & 0x7f;
  numdata = numodd + numgrps * 7;
  Debug_printf("\r\nDecoding %d bytes",numdata);

  // First, checksum  packet header, because we're about to destroy it
  for (int count = 6; count < 13; count++) // now xor the packet header bytes
    checksum = checksum ^ packet_buffer[count];

  int chkidx = 13 + numodd + (numodd != 0) + numgrps * 8;
  evenbits = packet_buffer[chkidx] & 0x55;
  oddbits = (packet_buffer[chkidx + 1] & 0x55) << 1;

  //add oddbyte(s), 1 in a 512 data packet
  for(int i = 0; i < numodd; i++){
    packet_buffer[i] = ((packet_buffer[13] << (i+1)) & 0x80) | (packet_buffer[14+i] & 0x7f);
  }

  // 73 grps of 7 in a 512 byte packet
  int grpstart = 12 + numodd + (numodd != 0) + 1;
  for (grpcount = 0; grpcount < numgrps; grpcount++)
  {
    memcpy(group_buffer, packet_buffer + grpstart + (grpcount * 8), 8);
    for (grpbyte = 0; grpbyte < 7; grpbyte++) {
      bit7 = (group_buffer[0] << (grpbyte + 1)) & 0x80;
      bit0to6 = (group_buffer[grpbyte + 1]) & 0x7f;
      packet_buffer[numodd + (grpcount * 7) + grpbyte] = bit7 | bit0to6;
    }
  }

  //verify checksum
  for (int count = 0; count < numdata; count++) // xor all the data bytes
    checksum = checksum ^ packet_buffer[count];

  Debug_printf("\r\ndecode data packet checksum calc %02x, packet %02x", checksum, (oddbits | evenbits));

  if (checksum != (oddbits | evenbits))
  {
    Debug_printf("\r\nCHECKSUM ERROR!");
    return true; // error!
  }
  
  num_decoded = numdata;
  return false;
}

//*****************************************************************************
// Function: encode_write_status_packet
// Parameters: source,status
// Returns: none
//
// Description: this is the reply to the write block data packet. The reply
// indicates the status of the write block cmd.
//*****************************************************************************
void iwmDevice::encode_write_status_packet(uint8_t source, uint8_t status)
{
  uint8_t checksum = 0;

  packet_buffer[0] = 0xff;  //sync bytes
  packet_buffer[1] = 0x3f;
  packet_buffer[2] = 0xcf;
  packet_buffer[3] = 0xf3;
  packet_buffer[4] = 0xfc;
  //  int i;
  packet_buffer[5] = 0xff;

  packet_buffer[6] = 0xc3;  //PBEGIN - start byte
  packet_buffer[7] = 0x80;  //DEST - dest id - host
  packet_buffer[8] = source; //SRC - source id - us
  packet_buffer[9] = PACKET_TYPE_STATUS;  //TYPE
  packet_buffer[10] = 0x80; //AUX
  packet_buffer[11] = status | 0x80; //STAT
  packet_buffer[12] = 0x80; //ODDCNT
  packet_buffer[13] = 0x80; //GRP7CNT

  for (int count = 7; count < 14; count++) // xor the packet header bytes
    checksum = checksum ^ packet_buffer[count];
  packet_buffer[14] = checksum | 0xaa;      // 1 c6 1 c4 1 c2 1 c0
  packet_buffer[15] = checksum >> 1 | 0xaa; // 1 c7 1 c5 1 c3 1 c1

  packet_buffer[16] = 0xc8;  //pkt end
  packet_buffer[17] = 0x00;  //mark the end of the packet_buffer

}

//*****************************************************************************
// Function: encode_init_reply_packet
// Parameters: source
// Returns: none
//
// Description: this is the reply to the init command packet. A reply indicates
// the original dest id has a device on the bus. If the STAT byte is 0, (0x80)
// then this is not the last device in the chain. This is written to support up
// to 4 partions, i.e. devices, so we need to specify when we are doing the last
// init reply.
//*****************************************************************************
void iwmDevice::encode_init_reply_packet (uint8_t source, uint8_t status)
{
  uint8_t checksum = 0;

  packet_buffer[0] = 0xff;  //sync bytes
  packet_buffer[1] = 0x3f;
  packet_buffer[2] = 0xcf;
  packet_buffer[3] = 0xf3;
  packet_buffer[4] = 0xfc;
  packet_buffer[5] = 0xff;

  packet_buffer[6] = 0xc3;  //PBEGIN - start byte
  packet_buffer[7] = 0x80;  //DEST - dest id - host
  packet_buffer[8] = source; //SRC - source id - us
  packet_buffer[9] = 0x80;  //TYPE
  packet_buffer[10] = 0x80; //AUX
  packet_buffer[11] = status | 0x80; //STAT - data status

  packet_buffer[12] = 0x80; //ODDCNT
  packet_buffer[13] = 0x80; //GRP7CNT

  for (int count = 7; count < 14; count++) // xor the packet header bytes
    checksum = checksum ^ packet_buffer[count];
  packet_buffer[14] = checksum | 0xaa;      // 1 c6 1 c4 1 c2 1 c0
  packet_buffer[15] = checksum >> 1 | 0xaa; // 1 c7 1 c5 1 c3 1 c1

  packet_buffer[16] = 0xc8; //PEND
  packet_buffer[17] = 0x00; //end of packet in buffer

}

//todo: this only replies a $21 error
void iwmDevice::encode_error_reply_packet (uint8_t stat)
{
  uint8_t checksum = 0;

  packet_buffer[0] = 0xff;  //sync bytes
  packet_buffer[1] = 0x3f;
  packet_buffer[2] = 0xcf;
  packet_buffer[3] = 0xf3;
  packet_buffer[4] = 0xfc;
  packet_buffer[5] = 0xff;

  packet_buffer[6] = 0xc3;  //PBEGIN - start byte
  packet_buffer[7] = 0x80;  //DEST - dest id - host
  packet_buffer[8] = id(); //SRC - source id - us
  packet_buffer[9] = PACKET_TYPE_STATUS;  //TYPE -status
  packet_buffer[10] = 0x80; //AUX
  packet_buffer[11] = stat | 0x80; //STAT - data status - error
  packet_buffer[12] = 0x80; //ODDCNT - 0 data bytes
  packet_buffer[13] = 0x80; //GRP7CNT

  for (int count = 7; count < 14; count++) // xor the packet header bytes
    checksum = checksum ^ packet_buffer[count];
  packet_buffer[14] = checksum | 0xaa;      // 1 c6 1 c4 1 c2 1 c0
  packet_buffer[15] = checksum >> 1 | 0xaa; // 1 c7 1 c5 1 c3 1 c1

  packet_buffer[16] = 0xc8; //PEND
  packet_buffer[17] = 0x00; //end of packet in buffer
}

void iwmDevice::iwm_return_badcmd(cmdPacket_t cmd)
{
  Debug_printf("\r\nUnit %02x Bad Command %02x", id(), cmd.command);
  encode_error_reply_packet(SP_ERR_BADCMD);
  IWM.SEND_PACKET((unsigned char *)packet_buffer);
}

void iwmDevice::iwm_return_ioerror(cmdPacket_t cmd)
{
  Debug_printf("\r\nUnit %02x Bad Command %02x", id(), cmd.command);
  encode_error_reply_packet(SP_ERR_IOERROR);
  IWM.SEND_PACKET((unsigned char *)packet_buffer);
}

//*****************************************************************************
// Function: verify_cmdpkt_checksum
// Parameters: none
// Returns: 0 = ok, 1 = error
//
// Description: verify the checksum for command packets
//
// &&&&&&&&not used at the moment, no error checking for checksum for cmd packet
//*****************************************************************************
bool iwmBus::verify_cmdpkt_checksum(void)
{
  //int length;
  uint8_t evenbits, oddbits, bit7, bit0to6, grpbyte;
  uint8_t calc_checksum = 0; //initial value is 0
  uint8_t pkt_checksum;

  //length = get_packet_length();
  //Debug_printf("\r\npacket length = %d", length);
  //2 oddbytes in cmd packet
  // calc_checksum ^= ((packet_buffer[13] << 1) & 0x80) | (packet_buffer[14] & 0x7f);
  // calc_checksum ^= ((packet_buffer[13] << 2) & 0x80) | (packet_buffer[15] & 0x7f);
  calc_checksum ^= ((command_packet.oddmsb << 1) & 0x80) | (command_packet.command & 0x7f);
  calc_checksum ^= ((command_packet.oddmsb << 2) & 0x80) | (command_packet.parmcnt & 0x7f);

  // 1 group of 7 in a cmd packet
  for (grpbyte = 0; grpbyte < 7; grpbyte++) {
    bit7 = (command_packet.grp7msb << (grpbyte + 1)) & 0x80;
    bit0to6 = (command_packet.data[17 + grpbyte]) & 0x7f;
    calc_checksum ^= bit7 | bit0to6;
  }

  // calculate checksum for overhead bytes
  for (int count = 6; count < 13; count++) // start from first id byte
    calc_checksum ^= command_packet.data[count];

  // int chkidx = 13 + numodd + (numodd != 0) + numgrps * 8;
  // evenbits = packet_buffer[chkidx] & 0x55;
  // oddbits = (packet_buffer[chkidx + 1] & 0x55) << 1;
  oddbits = (command_packet.chksum2 << 1) | 0x01;
  evenbits = command_packet.chksum1;
  pkt_checksum = oddbits & evenbits; // oddbits | evenbits;
  // every other bit is ==1 in checksum, so need to AND to get data back

  //  Debug_print(("Pkt Chksum Byte:\r\n"));
  //  Debug_print(pkt_checksum,DEC);
  //  Debug_print(("Calc Chksum Byte:\r\n"));
  //  Debug_print(calc_checksum,DEC);
  //  Debug_printf("\r\nChecksum - pkt,calc: %02x %02x", pkt_checksum, calc_checksum);
  // if ( pkt_checksum == calc_checksum )
  //   return false;
  // else
  //   return true;
  return (pkt_checksum != calc_checksum);  
}

void iwmDevice::iwm_status(cmdPacket_t cmd) // override;
{
  uint8_t status_code = cmd.g7byte3 & 0x7f; // (packet_buffer[19] & 0x7f); // | (((unsigned short)packet_buffer[16] << 3) & 0x80);
  Debug_printf("\r\nTarget Device: %02x", cmd.dest);
  // add a switch case statement for ALL THE STATUSESESESESS
  if (status_code == 0x03)
  { // if statcode=3, then status with device info block
    Debug_printf("\r\n******** Sending DIB! ********");
    encode_status_dib_reply_packet();
    // print_packet ((unsigned char*) packet_buffer,get_packet_length());
    fnSystem.delay(50);
    }
    else
    { // else just return device status
      Debug_printf("\r\nSending Status");
      encode_status_reply_packet();
    }
  print_packet(&packet_buffer[14]);
  IWM.SEND_PACKET((unsigned char *)packet_buffer);
}

//*****************************************************************************
// Function: packet_length
// Parameters: none
// Returns: length
//
// Description: Calculates the length of the packet in the packet_buffer.
// A zero marks the end of the packet data.
//*****************************************************************************
int iwmDevice::get_packet_length (void)
{
  int x = 5; // start at the 0xc3 beginning of packet
  while (packet_buffer[x++]);
  return x - 1; // point to last packet byte = C8
}


//*****************************************************************************
// Function: main loop
/*
 * notes:
 * with individual devices, like disk.cpp,
 * we need to hand off control to the device to service
 * the command packet. 
 * 
 * Disk II/3.5 selection is determined by the ENABLE lines
 * from BMOW - https://www.bigmessowires.com/2015/04/09/more-fun-with-apple-iigs-disks/
 * On an Apple II, things are more complicated. The Apple 5.25 controller card was the first to use a DB19 connector, and it supported two daisy-chained 5.25 inch drives. Pin 17 is /DRIVE1 enable, and pin 9 (unconnected on the Macintosh) is /DRIVE2 enable. Within each drive, internal circuitry routes the signal from input pin 9 to output pin 17 on the daisy-chain connector. Drive #2 doesn’t actually know that it’s drive #2 – it enables itself by observing /DRIVE1 on pin 17, just like the first drive – only the first drive has sneakily rerouted /DRIVE2 to /DRIVE1. This allows for two drives to be daisy chained.
 * On an Apple IIgs, it’s even more complicated. Its DB19 connector supports daisy-chaining two 3.5 inch drives, and two 5.25 inch drives – as well as even more SmartPort drives, which I won’t discuss now. Pin 4 (GND on the Macintosh) is /EN3.5, a new signal that enables the 3.5 inch drives when it’s low, or the 5.25 inch drives when it’s high. The 3.5 inch drives must appear before any 5.25 inch drives in the daisy chain. When /EN3.5 is low, the 3.5 inch drives use pins 17 and 9 to enable themselves, and when /EN3.5 is high, the 3.5 inch drives pass through the signals on pins 17 and 9 unmodified to the 5.25 drives behind them.
 * This is getting complicated, but there’s one final kick in the nuts: when the first 3.5 drive is enabled, by the IIgs setting /EN3.5 and /DRIVE1 both low, you would think the drive would disable the next 3.5 drive behind it by setting both /DRIVE1 and /DRIVE2 high at the daisy-chain connector. But no, the first 3.5 drive disables the second 3.5 drive by setting both /DRIVE1 and /DRIVE2 low! This looks like both are enabled at the same time, which would be a definite no-no, but the Apple 3.5 Drive contains circuitry that recognizes this “double enable” as being equivalent to a disable. Why it’s done this way, I don’t know, but I’m sure it has some purpose.
 * 
 * So for starters FN will look at the /DRIVEx line (not sure which one because IIc has internal floppy drive connected right now)
 * If floppy is enabled, the motor is spinning and FN needs to track the phases and spit out data (unless writereq is activated)
 * If floppy is disabled, smartport should be in control instead.
 * 
 * The smartport algorithm is something like:
 * check for 0x85 init and do a bus initialization:
 * BUS INIT
 * after a reset, all devices no longer have an address
 * and they are gating some signal (REQ?) so devices
 * down the chain cannot respond to commands. So the
 * first device responds to INIT. During this, it checks
 * the sense line (still not sure which pin this is) to see
 * if it is low (grounded) or high (floating or pulled up?).
 * It will be low if there's another device in the chain
 * after it. If it is the last device it will be high.
 * It sends this state in the response to INIT. It also
 * ungates whatever the magic line is so the next device
 * in the chain can receive the INIT command that is
 * coming next. This repeats until the last device in the
 * chain says it's so and the A2 will stop sending INITs.
 *
 * Every other command:
 * The bus class checks the target device and should pass
 * on the command packet to the device service routine.
 * Then the device can respond accordingly.
 *
 * When device ID is not FujiNet's:
 * If the device ID does not belong to any of the FujiNet
 * devices (disks, printers, modem, network, etc) then FN
 * should not respond. The SmartPortSD code runs through
 * the states for the packets that should come next. I'm
 * not sure this is the best because what happens in case
 * of a malfunction. I suppose there could be a time out
 * that takes us back to idle. This will take more
 * investigation.
 */
//*****************************************************************************
void iwmBus::service()
{
  iwm_ack_disable(); // go hi-Z
  iwm_ack_clr();     // prep for the next read packet

  if (iwm_drive_enables())
  {
    //Debug_printf("\r\nFloppy Drive ENabled!");
    iwm_rddata_clr();
    iwm_rddata_enable();
  }
  else
  {
    //Debug_printf("\r\nFloppy Drive DISabled!"); // debug msg latency here screws up SP timing.
    iwm_rddata_disable();
  }

  // read phase lines to check for smartport reset or enable
  switch (iwm_phases())
  {
  case iwm_phases_t::idle:
    break;
  case iwm_phases_t::reset:
    // instead of the code in this section, we should call a reset handler
    // the handler should reset every device
    // and wait for reset to clear (probably with a timeout)
    Debug_printf(("\r\nReset"));
    // hard coding 1 partition - will use disk class instances instead
    // smort->_devnum = 0;
    for (auto devicep : _daisyChain)
      devicep->_devnum = 0;

    while (iwm_phases() == iwm_phases_t::reset)
      ; // no timeout needed because the IWM must eventually clear reset.
    // even if it doesn't, we would just come back to here, so might as
    // well wait until reset clears.

    Debug_printf(("\r\nReset Cleared"));
    break;
  case iwm_phases_t::enable:
    // expect a command packet
#ifdef TEXT_RX_SPI
    if(iwm_read_packet_spi(command_packet.data, COMMAND_PACKET_LEN))
      return;
          //     iwm_ack_clr();
          // iwm_ack_enable(); // now ACK is enabled and cleared low, it is reset in the handlers
          // print_packet(command_packet.data, COMMAND_PACKET_LEN);
#else
    portDISABLE_INTERRUPTS(); // probably put the critical section inside the read packet function?
    while (iwm_read_packet(command_packet.data, COMMAND_PACKET_LEN))
    {
      portENABLE_INTERRUPTS();
      return;
    }
#endif
    // should not ACK unless we know this is our Command
    if (command_packet.command == 0x85)
    {
      iwm_ack_clr();
      iwm_ack_enable(); // now ACK is enabled and cleared low, it is reset in the handlers
#ifdef TEXT_RX_SPI
      spi_device_get_trans_result(spirx, &transptr, portMAX_DELAY);
#else 
      portENABLE_INTERRUPTS();
#endif
      // wait for REQ to go low
      //iwm_timer_reset();
      iwm_timer_reset();
      iwm_timer_latch();          // latch highspeed timer value
      iwm_timer_read();           //  grab timer low word
      iwm_timer_alarm_set(50000); // todo: figure out
      while (iwm_req_val())
      {
        iwm_timer_latch();               // latch highspeed timer value
        iwm_timer_read();                // grab timer low word
        if (iwm_timer.t0 > iwm_timer.tn) // test for timeout
        {                                // timeout!
          return;
        }
      }

#ifdef DEBUG
      print_packet(command_packet.data);
      Debug_printf("\r\nhandling init command");
#endif
      if (verify_cmdpkt_checksum())
      {
        Debug_printf("\r\nBAD CHECKSUM!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
        Debug_printf("\r\ndo init anyway");
      }      // to do - checksum verification? How to respond?
      handle_init();
    }
    else
    {
      // smort->process(command_packet);
      for (auto devicep : _daisyChain)
      {
        if (command_packet.dest == devicep->_devnum)
        {
          iwm_ack_clr();
          iwm_ack_enable(); // now ACK is enabled and cleared low, it is reset in the handlers
#ifdef TEXT_RX_SPI
          spi_device_get_trans_result(spirx, &transptr, portMAX_DELAY);
#else
          portENABLE_INTERRUPTS();
#endif
          // wait for REQ to go low
          iwm_timer_reset();
          iwm_timer_latch();          // latch highspeed timer value
          iwm_timer_read();           //  grab timer low word
          iwm_timer_alarm_set(50000); // todo: figure out
          while (iwm_req_val())
          {
            iwm_timer_latch();               // latch highspeed timer value
            iwm_timer_read();                // grab timer low word
            if (iwm_timer.t0 > iwm_timer.tn) // test for timeout
            {                                // timeout!
              return;
            }
          }

          // need to take time here to service other ESP processes so they can catch up
          taskYIELD(); // Allow other tasks to run
          
          _activeDev = devicep;
          // handle command
          if (verify_cmdpkt_checksum())
          {
            Debug_printf("\r\nBAD CHECKSUM!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
            _activeDev->iwm_return_ioerror(command_packet);
          }
          else
          {
            _activeDev->process(command_packet);
          }
        }
      }
    }
  } // switch (phasestate)
}

bool iwmBus::iwm_drive_enables()
{
#ifdef PINMAP_A2_REV0
  return false; // ignore floppy drives for now
#else
  return !iwm_enable_val();
#endif
}

void iwmBus::handle_init()
{
  uint8_t status = 0;
  iwmDevice* pDevice = nullptr;

  fnLedManager.set(LED_BUS, true);

  iwm_rddata_clr();
  iwm_rddata_enable();

  
  // to do - get the next device in the daisy chain and assign ID
  for (auto it = _daisyChain.begin(); it != _daisyChain.end(); ++it)
  {
    // tell the Fuji it's device no.
    if (it == _daisyChain.begin())
    {
      theFuji._devnum = command_packet.dest;
    }
    // assign dev numbers
    pDevice = (*it);
    if (pDevice->id() == 0)
    {
      pDevice->_devnum = command_packet.dest; // assign address
      if (++it == _daisyChain.end())
        status = 0xff; // end of the line, so status=non zero - to do: check GPIO for another device in the physical daisy chain
      pDevice->encode_init_reply_packet(command_packet.dest, status);
      Debug_printf("\r\nSending INIT Response Packet...");
      SEND_PACKET((uint8_t *)pDevice->packet_buffer); // timeout error return is not handled here (yet?)

      // print_packet ((uint8_t*) packet_buffer,get_packet_length());

      Debug_printf(("\r\nDrive: %02x\r\n"), pDevice->id());
      fnLedManager.set(LED_BUS, false);
      return;
    }
  }

  fnLedManager.set(LED_BUS, false);

}

// Add device to SIO bus
void iwmBus::addDevice(iwmDevice *pDevice, iwm_fujinet_type_t deviceType)
{
  // SmartPort interface assigns device numbers to the devices in the daisy chain one at a time
  // as opposed to using standard or fixed device ID's like Atari SIO. Therefore, an emulated
  // device cannot rely on knowing its device number until it is assigned.
  // Instead of using device_id's to know what kind a specific device is, smartport 
  // uses a Device Information Block (DIB) that is returned in a status call for DIB. The 
  // DIB includes a 16-character string, Device type byte, and Device subtype byte.
  // In the IIgs firmware reference, the following device types are defined:
  // 0 - memory cards (internal to the machine)
  // 1 - Apple and Uni 3.5 drives
  // 2 - harddisk
  // 3 - SCSI disk
  // The subtype uses the 3 msb's to indicate the following:
  // 0x80 == 1 -> support extended smartport
  // 0x40 == 1 -> supprts disk-switched errors
  // 0x20 == 0 -> removable media (1 means non removable)

  // todo: work out how to use addDevice
  // we can add devices and indicate they are not initialized and have no device ID - call it a value of 0
  // when the SP bus goes into RESET, we would rip through the list setting initialized to false and
  // setting device id's to 0. Then on each INIT command, we iterate through the list, setting 
  // initialized to true and assigning device numbers as assigned by the smartport controller in the A2.
  // so I need "reset()" and "initialize()" functions.

  // todo: I need a way to internally keep track of what kind of device each one is. I'm thinking an
  // enumerated class type might work well here. It can be expanded as needed and an extra case added 
  // below. I can also make this a switch case structure to ensure each case of the class is handled.

    // assign dedicated pointers to certain devices
    switch (deviceType)
    {
    case iwm_fujinet_type_t::BlockDisk:
      break;
    case iwm_fujinet_type_t::FujiNet:
      _fujiDev = (iwmFuji *)pDevice;
      break;
    case iwm_fujinet_type_t::Modem:
      _modemDev = (iwmModem *)pDevice;
      break;
    case iwm_fujinet_type_t::Network:
      // todo: work out how to assign different network devices - idea:
      // include a number in the DIB name, e.g., "NETWORK 1"
      // and extract that number from the DIB and use it as the index
      //_netDev[device_id - SIO_DEVICEID_FN_NETWORK] = (iwmNetwork *)pDevice;
      break;
    case iwm_fujinet_type_t::CPM:
       _cpmDev = (iwmCPM *)pDevice;
       break;
    case iwm_fujinet_type_t::Printer:
      _printerdev = (iwmPrinter *)pDevice;
      break;
    case iwm_fujinet_type_t::Voice:
    // not yet implemented: todo - take SAM and implement as a special block device. Also then available for disk rotate annunciation. 
      break;
    case iwm_fujinet_type_t::Other:
      break;
    }

    pDevice->_devnum = 0;
    pDevice->_initialized = false;

    _daisyChain.push_front(pDevice);
}

// Removes device from the SIO bus.
// Note that the destructor is called on the device!
void iwmBus::remDevice(iwmDevice *p)
{
    _daisyChain.remove(p);
}

// Should avoid using this as it requires counting through the list
int iwmBus::numDevices()
{
    int i = 0;
    __BEGIN_IGNORE_UNUSEDVARS
    for (auto devicep : _daisyChain)
        i++;
    return i;
    __END_IGNORE_UNUSEDVARS
}

void iwmBus::changeDeviceId(iwmDevice *p, int device_id)
{
    for (auto devicep : _daisyChain)
    {
        if (devicep == p)
            devicep->_devnum = device_id;
    }
}

iwmDevice *iwmBus::deviceById(int device_id)
{
    for (auto devicep : _daisyChain)
    {
        if (devicep->_devnum == device_id)
            return devicep;
    }
    return nullptr;
}

void iwmBus::enableDevice(uint8_t device_id)
{
    iwmDevice *p = deviceById(device_id);
    p->device_active = true;
}

void iwmBus::disableDevice(uint8_t device_id)
{
    iwmDevice *p = deviceById(device_id);
    p->device_active = false;
}

// Give devices an opportunity to clean up before a reboot
void iwmBus::shutdown()
{
    for (auto devicep : _daisyChain)
    {
        Debug_printf("Shutting down device %02x\n",devicep->id());
        devicep->shutdown();
    }
    Debug_printf("All devices shut down.\n");
}

#ifdef TESTTX
void iwmBus::test_send(iwmDevice* smort)
{
  iwm_rddata_clr();
  iwm_rddata_enable(); 
  iwm_ack_clr();
  iwm_ack_enable();
  
  smort->_devnum = 0x81; //remember source id for partition
  uint8_t status = 0xff; //yes, so status=non zero

  smort->encode_init_reply_packet(0x81, status);

  while (true)
  {
    Debug_printf("\r\nSending INIT Response Packet...");
    SEND_PACKET((uint8_t *)smort->packet_buffer); // timeout error return is not handled here (yet?)
    fnSystem.delay(1000);
  }
}
#endif

// void iwmBus::startup_hack()
// {
//   _daisyChain.front()->startup_hack();
// }

iwmBus IWM; // global smartport bus variable

#endif /* BUILD_APPLE */
