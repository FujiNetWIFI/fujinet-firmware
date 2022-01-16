#ifdef BUILD_APPLE
#include "spsd.h"

/** 
 * converting to ESP32 use by @jeffpiep 
 * as preparation for creating FujiNet for Apple II plus ][+
 * search for "todo" to find things to work on
 * 
 * step 1 - just respond to a smartport (sp) "reset"
 * step 2 - receive a port id command (from boot sequence)
 * step 3 - don't know yet
**/

/* pin assignments for Arduino UNO 
from  http://www.users.on.net/~rjustice/SmartportCFA/SmartportSD.htm
I added the IDC20 column to the left to list the Disk II 20-pin pins based on
https://www.bigmessowires.com/2015/04/09/more-fun-with-apple-iigs-disks/

IDC20   IIc     DB 19     Arduino

1       GND      1        GND to board        
        GND      2
        GND      3
        GND      4
        -12V     5
        +5V      6        +5v to board
        +12V     7
        +12V     8
        EXTINT   9
20      WRPROT   10       PA5   (ACK for smartport)
2       PH0      11       PD2   (REQ for smartport)
4       PH1      12       PD3
6       PH2      13       PD4
8       PH3      14       PD5
        WREQ     15         
        (NC)     16       
        DRVEN    17      
16      RDDATA   18       PD6
18      WRDATA   19       PD7

        STATUS LED        PA4
        EJECT BUTTON      PA3

*/

//*****************************************************************************
//
// Based on:
//
// Apple //c Smartport Compact Flash adapter
// Written by Robert Justice  email: rjustice(at)internode.on.net
// Ported to Arduino UNO with SD Card adapter by Andrea Ottaviani email: andrea.ottaviani.69(at)gmail.com
// SD FAT support added by Katherine Stark at https://gitlab.com/nyankat/smartportsd/
//
//*****************************************************************************

//         SP BUS     GPIO       SIO            IDC
//         ---------  ----       -------       ------
// #define SP_WRPROT   27                       
// #define SP_ACK      27        CLKIN          20
// #define SP_REQ      39
// #define SP_PHI0     39        CMD            2
// #define SP_PHI1     22        PROC           4
// #define SP_PHI2     36        MOTOR          6
// #define SP_PHI3     26        INT            8
// #define SP_RDDATA   21        DATAIN         16
// #define SP_WRDATA   33        DATAOUT        18
#define SP_WRPROT   27
#define SP_ACK      27
#define SP_REQ      39
#define SP_PHI0     39
#define SP_PHI1     22
#define SP_PHI2     36
#define SP_PHI3     26
#define SP_RDDATA   21
#define SP_WRDATA   33
#define SP_EXTRA    32 // CLOCKOUT

#include "esp_timer.h"
#include "driver/timer.h"
#include "soc/timer_group_reg.h"
//#include "freertos/task.h"

#include "../../include/debug.h"
#include "fnSystem.h"
#include "led.h"

#include "fnFsTNFS.h"

#define HEX 16
#define DEC 10

#include <string.h>

#define TIMER_DIVIDER         (2)  //  Hardware timer clock divider
#define TIMER_SCALE           (TIMER_BASE_CLK / TIMER_DIVIDER)  // convert counter value to seconds
#define TIMER_USEC_FACTOR     (TIMER_SCALE / 1000000)
#define TIMER_ADJUST          5 // substract this value to adjust for overhead

#undef VERBOSE
#define TESTTX

extern FileSystemTNFS tserver;
FileSystemTNFS tserver;
portMUX_TYPE myMutex = portMUX_INITIALIZER_UNLOCKED;

//------------------------------------------------------------------------------

void spDevice::hw_timer_latch()
{
  TIMERG1.hw_timer[1].update = 0;
}

void spDevice::hw_timer_read()
{
  t0 = TIMERG1.hw_timer[1].cnt_low;
}

void spDevice::hw_timer_alarm_set(int s)
{
  tn = t0 + s * TIMER_USEC_FACTOR - TIMER_ADJUST;
}

void spDevice::hw_timer_alarm_snooze(int s)
{
  tn += s * TIMER_USEC_FACTOR - TIMER_ADJUST; // 3 microseconds

}

void spDevice::hw_timer_wait()
{
  do
  {
    hw_timer_latch();
    hw_timer_read();
  } while (t0 < tn);
}

void spDevice::hw_timer_reset()
{
  TIMERG1.hw_timer[1].load_low = 0;
  TIMERG1.hw_timer[1].reload = 0;
}

void spDevice::smartport_rddata_set()
{
  GPIO.out_w1ts = ((uint32_t)1 << SP_RDDATA);
}

void spDevice::smartport_rddata_clr()
{
  GPIO.out_w1tc = ((uint32_t)1 << SP_RDDATA);
}

void spDevice::smartport_rddata_enable()
{
  GPIO.enable_w1ts = ((uint32_t)0x01 << SP_RDDATA);  
}

void spDevice::smartport_rddata_disable()
{
  GPIO.enable_w1tc = ((uint32_t)0x01 << SP_RDDATA);
}

bool spDevice::smartport_wrdata_val()
{
  return (GPIO.in1.val & ((uint32_t)0x01 << (SP_WRDATA - 32)));
}

bool spDevice::smartport_req_val()
{
  return (GPIO.in1.val & (0x01 << (SP_REQ-32)));
}

void spDevice::smartport_extra_clr()
{
  GPIO.out1_w1tc.data = ((uint32_t)0x01 << (SP_EXTRA - 32));
}

void spDevice::smartport_extra_set()
{
  GPIO.out1_w1ts.data = ((uint32_t)0x01 << (SP_EXTRA - 32));
}

//------------------------------------------------------
/** ACK and REQ
 * how ACK works, my interpretation of the iigs firmware reference.
 * ACK is normally high when device is ready to receive commands.
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
void spDevice::smartport_ack_clr()
{
  //GPIO.enable_w1ts = ((uint32_t)0x01 << SP_ACK);
GPIO.out_w1tc = ((uint32_t)1 << SP_ACK);
#ifdef VERBOSE
  Debug_print("a");
#endif
}

void spDevice::smartport_ack_set()
{
  //GPIO.enable_w1tc = ((uint32_t)0x01 << SP_ACK);
GPIO.out_w1ts = ((uint32_t)1 << SP_ACK);
#ifdef VERBOSE
  Debug_print("A");
#endif
}

void spDevice::smartport_ack_enable()
{
  GPIO.enable_w1ts = ((uint32_t)0x01 << SP_ACK);  
}

void spDevice::smartport_ack_disable()
{
  GPIO.enable_w1tc = ((uint32_t)0x01 << SP_ACK);
}

int spDevice::smartport_handshake()
{
  int ret = 0;

  smartport_ack_clr();
  smartport_ack_enable();
  smartport_rddata_disable();

  hw_timer_latch();
  hw_timer_read();
  hw_timer_alarm_set(10000); // 1 millisecond
  do
  {
    hw_timer_latch();
    hw_timer_read();
    if (t0 > tn)
    {
      // timeout!
#ifdef VERBOSE
  Debug_print("t");
#endif
      ret = 1;
      break;
    }
  } while (smartport_req_val()); // wait for REQ to go low
#ifdef VERBOSE
  if (ret == 0)
    Debug_print("r");
#endif
  smartport_ack_set();
  smartport_ack_disable();

  return ret;
}

//------------------------------------------------------

bool spDevice::smartport_phase_val(int p)
{ // move to smartport bus class when refactoring
// #define SP_PHI0     39
// #define SP_PHI1     22
// #define SP_PHI2     32
// #define SP_PHI3     26
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
    break;
  }
  Debug_printf("\r\nphase number out of range");
  return false;
}

spDevice::phasestate_t spDevice::smartport_phases()
{ 
  phasestate_t phasestate = phasestate_t::idle;
  // phase lines for smartport bus reset
  // ph3=0 ph2=1 ph1=0 ph0=1
  // phase lines for smartport bus enable
  // ph3=1 ph2=x ph1=1 ph0=x
  if (smartport_phase_val(1) && smartport_phase_val(3))
    phasestate = phasestate_t::enable;
  else if (smartport_phase_val(0) && smartport_phase_val(2) && !smartport_phase_val(1) && !smartport_phase_val(3))
    phasestate = phasestate_t::reset;

#ifdef DEBUG
  if (phasestate != oldphase)
  {
    switch (phasestate)
    {
    case phasestate_t::idle:
      Debug_printf("\r\nidle");
      break;
    case phasestate_t::reset:
      Debug_printf("\r\nreset");
      break;
    case phasestate_t::enable:
      Debug_printf("\r\nenable");
    }
    oldphase=phasestate;
  }
#endif

  return phasestate;
}

//------------------------------------------------------


int IRAM_ATTR spDevice::ReceivePacket(uint8_t *a)
{
  //*****************************************************************************
  // Function: ReceivePacket
  // Parameters: packet_buffer pointer
  // Returns: status (1 = timeout, 0 = OK)
  //
  // Description: This handles the ACK and REQ lines and reads a packet into the
  // packet_buffer
  //
  //*****************************************************************************
  
  bool have_data = true;
  int idx = 0;             // index into *a
  bool bit = 0;        // logical bit value
  bool prev_level = true; // ((uint32_t)0x01 << (SP_WRDATA - 32)); // previous value of WRDATA line
  bool current_level;  // current value of WRDATA line
  uint8_t rxbyte = 0;      // r23 received byte being built bit by bit
  int numbits;             // number of bits left to read into the rxbyte

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
 * reads 250kbit/sec serial data that includes sometimes irregular timing between 
 * bytes. The SP serial data are encoded such that the logical meaning of the 
 * current bit depends on the signal level of the previous bit. This can also
 * be interpreted as over-lapping two-bit sequences. Byte framing is done by
 * ensuring the first bit (really bit 7) of the next byte is always the 
 * opposite signal level of the last bit (bit 0) of the current byte. The
 * algorithm waits for the transition and then starts the new byte.
 */

  // 'a' is the receive buffer pointer

  hw_timer_reset();

  smartport_ack_set();

  // setup a timeout counter to wait for REQ response
  hw_timer_latch();        // latch highspeed timer value
  hw_timer_read();      //  grab timer low word
  hw_timer_alarm_set(100); // logic analyzer says 40 usec

  // todo: this waiting for REQ seems like it should be done
  // in the main loop, if control can be passed quickly enough
  // to the receive routine. Otherwise, we sit here blocking(?)
  // until REQ goes high. As long as PHIx is in Enable mode.
  while ( !smartport_req_val() )  
  {
    hw_timer_latch();   // latch highspeed timer value
    hw_timer_read(); // grab timer low word
    if (t0 > tn)                      // test for timeout
    { // timeout!
#ifdef VERBOSE
      // timeout
      Debug_print("t");
#endif
      return 1;
    }
  };

#ifdef VERBOSE
  // REQ received!
  Debug_print("R");
#endif


  // setup a timeout counter to wait for WRDATA to be ready response
  hw_timer_latch();                    // latch highspeed timer value
  hw_timer_read();    //  grab timer low word
  hw_timer_alarm_set(32); // 32 usec - 1 byte
  while (smartport_wrdata_val())
  {
    hw_timer_latch();   // latch highspeed timer value
    hw_timer_read(); // grab timer low word
    if (t0 > tn)     // test for timeout
    {                // timeout!
#ifdef VERBOSE
      // timeout
      Debug_print("t");
#endif
      return 1;
    }
  };

  do // have_data
  {
    // beginning of the byte
    // delay 2 us until middle of 4-us bit
    hw_timer_latch();
    hw_timer_read();
    hw_timer_alarm_set(2); //TIMER_SCALE / 500000; // 2 usec
    numbits = 8; // ;1   8bits to read
//    hw_timer_wait();
    do
    {
      hw_timer_wait();
      // logic table:
      //  prev_level  current_level   decoded bit
      //  0           0               0
      //  0           1               1
      //  1           0               1
      //  1           1               0
      // this is an exclusive OR operation
      //todo: can curent_level and prev_level be bools and then uint32_t is implicitly
      //typecast down to bool? then the code can be:
      current_level = smartport_wrdata_val();       // nxtbit:   sbic _SFR_IO_ADDR(PIND),7           ;2   ;2    ;1  ;1      ;1/2 now read a bit, cycle time is 4us
      hw_timer_alarm_set(4); // 4 usec
      bit = prev_level ^ current_level;
      rxbyte <<= 1;
      rxbyte |= bit;
      prev_level = current_level;
      //
      // current_level = smartport_wrdata_val();       // nxtbit:   sbic _SFR_IO_ADDR(PIND),7           ;2   ;2    ;1  ;1      ;1/2 now read a bit, cycle time is 4us
      // hw_timer_alarm_set(4); // 4 usec
      // bit = prev_level ^ current_level;
      // rxbyte <<= 1;
      // rxbyte |= (uint8_t)(bit > 0);
      // prev_level = current_level;
      if ((--numbits) == 0)
        break; // end of byte
      // todo: use best (whatever works) alarm setting because
      // in sendpacket, the alarm is set every bit instead of snoozed

    } while(1);
    a[idx++] = rxbyte; // havebyte: st   x+,r23                         ;17                    ;2   save byte in buffer
    hw_timer_alarm_snooze(19); // 19 usec from smartportsd assy routine
#ifdef VERBOSE
    Debug_printf("%02x", rxbyte);
#endif
    // now wait for leading edge of next byte
    do // return (GPIO.in1.val >> (pin - 32)) & 0x1;
    {
      hw_timer_latch();
      hw_timer_read();
      if (t0 > tn)
      {
        // end of packet
        have_data = false;
        break;
      }
    } while (smartport_wrdata_val() == prev_level);
  } while (have_data); //(have_data); // while have_data
  //           rjmp nxtbyte                        ;46  ;47               ;2   get next byte

  // endpkt:   clr  r23
  a[idx] = 0; //           st   x+,r23               ;save zero byte in buffer to mark end

  smartport_ack_clr();
  while (smartport_req_val())
    ;

  return 0;
}

int spDevice::SendPacket(const uint8_t *a)
{
  //*****************************************************************************
  // Function: SendPacket
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
 txbyte = a[idx++];

#ifdef TESTTX
    taskENTER_CRITICAL(&myMutex);
    smartport_extra_set();
    smartport_rddata_enable();
#endif

// Disable interrupts
// https://esp32developer.com/programming-in-c-c/interrupts/interrupts-general
// You can suspend interrupts and context switches by calling  portDISABLE_INTERRUPTS
// and the interrupts on that core should stop firing, stopping task switches as well.
// Call portENABLE_INTERRUPTS after you're done
smartport_rddata_set();

  hw_timer_reset();
//  smartport_rddata_clr();
  smartport_ack_set();


  // 1:        sbic _SFR_IO_ADDR(PIND),2   ;wait for req line to go high
  // setup a timeout counter to wait for REQ response
  hw_timer_latch();        // latch highspeed timer value
  hw_timer_read();      //  grab timer low word
#ifndef TESTTX
hw_timer_alarm_set(100); // 10 millisecond

  // while (!fnSystem.digital_read(SP_REQ))
  while ( !smartport_req_val() ) //(GPIO.in1.val >> (pin - 32)) & 0x1
  {
    hw_timer_latch();   // latch highspeed timer value
    hw_timer_read(); // grab timer low word
    if (t0 > tn)                      // test for timeout
    {
      // timeout!
      Debug_printf("\r\nSendPacket timeout waiting for REQ");
      smartport_rddata_disable();
      portENABLE_INTERRUPTS(); // takes 7 us to execute
      return 1;
    }
  };
// ;

#ifdef VERBOSE
  // REQ received!
  Debug_print("R");
#endif

#endif // TESTTX

  // SEEMS CRITICAL TO HAVE 1 US BETWEEN req AND FIRST PULSE to put the falling edge 2 us after REQ
  hw_timer_alarm_set(1); // throw in a bit of time before sending first pulse
  //tn = t0 + TIMER_USEC_FACTOR - TIMER_ADJUST; // NEED JUST 1/2 USEC
  hw_timer_wait();

  do // beware of entry into the loop and an extended first pulse ...
  {
    do
    {
      // send MSB first, then ROL byte for next bit
      hw_timer_latch();
      if (txbyte & 0x80)
        smartport_rddata_set();
      else
        smartport_rddata_clr();
     
      hw_timer_read();
      hw_timer_alarm_snooze(1); // 1 microsecond - snooze to finish off 4 us period
      hw_timer_wait();

      smartport_rddata_clr();
      hw_timer_alarm_set(3); // 3 microseconds - set on falling edge of pulse

      // do some updating while in 3-us low period
      if ((--numbits) == 0)
        break;

      txbyte <<= 1; //           rol  r23  
      hw_timer_wait();
    } while (1);

    txbyte = a[idx++]; // nxtsbyte: ld   r23,x+                 ;59               ;43         ;2   get first byte from buffer
    numbits = 8; //           ldi  r25,8                  ;62               ;46         ;1   8bits to read
    hw_timer_wait(); // finish the 3 usec low period
  } while (txbyte); //           cpi  r23,0                  ;60               ;44         ;1   zero marks end of data

  smartport_ack_clr();

#ifndef TESTTX
  while (smartport_req_val());
#endif

#ifdef TESTTX
    smartport_rddata_disable();
    smartport_extra_clr();
    taskEXIT_CRITICAL(&myMutex);
#endif

return 0;
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
void spDevice::encode_data_packet (uint8_t source)
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
void spDevice::encode_extended_data_packet (uint8_t source)
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
// Description: decode 512 byte data packet for write block command from host
// decodes the data from the packet_buffer IN-PLACE!
//*****************************************************************************
int spDevice::decode_data_packet (void)
{
  int grpbyte, grpcount;
  uint8_t numgrps, numodd;
  uint8_t checksum = 0, bit0to6, bit7, oddbits, evenbits;
  uint8_t group_buffer[8];

  //Handle arbitrary length packets :) 
  numodd = packet_buffer[11] & 0x7f;
  numgrps = packet_buffer[12] & 0x7f;

  // First, checksum  packet header, because we're about to destroy it
  for (int count = 6; count < 13; count++) // now xor the packet header bytes
    checksum = checksum ^ packet_buffer[count];

  evenbits = packet_buffer[599] & 0x55;
  oddbits = (packet_buffer[600] & 0x55 ) << 1;

  //add oddbyte(s), 1 in a 512 data packet
  for(int i = 0; i < numodd; i++){
    packet_buffer[i] = ((packet_buffer[13] << (i+1)) & 0x80) | (packet_buffer[14+i] & 0x7f);
  }

  // 73 grps of 7 in a 512 byte packet
  for (grpcount = 0; grpcount < numgrps; grpcount++)
  {
    memcpy(group_buffer, packet_buffer + 15 + (grpcount * 8), 8);
    for (grpbyte = 0; grpbyte < 7; grpbyte++) {
      bit7 = (group_buffer[0] << (grpbyte + 1)) & 0x80;
      bit0to6 = (group_buffer[grpbyte + 1]) & 0x7f;
      packet_buffer[1 + (grpcount * 7) + grpbyte] = bit7 | bit0to6;
    }
  }

  //verify checksum
  for (int count = 0; count < 512; count++) // xor all the data bytes
    checksum = checksum ^ packet_buffer[count];

  if (checksum == (oddbits | evenbits))
    return 0; //noerror
  else
    return 6; //smartport bus error code

}

//*****************************************************************************
// Function: encode_write_status_packet
// Parameters: source,status
// Returns: none
//
// Description: this is the reply to the write block data packet. The reply
// indicates the status of the write block cmd.
//*****************************************************************************
void spDevice::encode_write_status_packet(uint8_t source, uint8_t status)
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
  packet_buffer[9] = 0x81;  //TYPE
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
void spDevice::encode_init_reply_packet (uint8_t source, uint8_t status)
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
  packet_buffer[11] = status; //STAT - data status

  packet_buffer[12] = 0x80; //ODDCNT
  packet_buffer[13] = 0x80; //GRP7CNT

  for (int count = 7; count < 14; count++) // xor the packet header bytes
    checksum = checksum ^ packet_buffer[count];
  packet_buffer[14] = checksum | 0xaa;      // 1 c6 1 c4 1 c2 1 c0
  packet_buffer[15] = checksum >> 1 | 0xaa; // 1 c7 1 c5 1 c3 1 c1

  packet_buffer[16] = 0xc8; //PEND
  packet_buffer[17] = 0x00; //end of packet in buffer

}

//*****************************************************************************
// Function: encode_status_reply_packet
// Parameters: source
// Returns: none
//
// Description: this is the reply to the status command packet. The reply
// includes following:
// data byte 1 is general info.
// data byte 2-4 number of blocks. 2 is the LSB and 4 the MSB. 
// Size determined from image file.
//*****************************************************************************
void spDevice::encode_status_reply_packet (device d)
{

  uint8_t checksum = 0;
  uint8_t data[4];

  //Build the contents of the packet
  //Info byte
  //Bit 7: Block  device
  //Bit 6: Write allowed
  //Bit 5: Read allowed
  //Bit 4: Device online or disk in drive
  //Bit 3: Format allowed
  //Bit 2: Media write protected
  //Bit 1: Currently interrupting (//c only)
  //Bit 0: Currently open (char devices only) 
  data[0] = 0b11111000;
  //Disk size
  data[1] = d.blocks & 0xff;
  data[2] = (d.blocks >> 8 ) & 0xff;
  data[3] = (d.blocks >> 16 ) & 0xff;

  
  packet_buffer[0] = 0xff;  //sync bytes
  packet_buffer[1] = 0x3f;
  packet_buffer[2] = 0xcf;
  packet_buffer[3] = 0xf3;
  packet_buffer[4] = 0xfc;
  packet_buffer[5] = 0xff;

  packet_buffer[6] = 0xc3;  //PBEGIN - start byte
  packet_buffer[7] = 0x80;  //DEST - dest id - host
  packet_buffer[8] = d.device_id; //SRC - source id - us
  packet_buffer[9] = 0x81;  //TYPE -status
  packet_buffer[10] = 0x80; //AUX
  packet_buffer[11] = 0x80; //STAT - data status
  packet_buffer[12] = 0x84; //ODDCNT - 4 data bytes
  packet_buffer[13] = 0x80; //GRP7CNT
  //4 odd bytes
  packet_buffer[14] = 0x80 | ((data[0]>> 1) & 0x40) | ((data[1]>>2) & 0x20) | (( data[2]>>3) & 0x10) | ((data[3]>>4) & 0x08 ); //odd msb
  packet_buffer[15] = data[0] | 0x80; //data 1
  packet_buffer[16] = data[1] | 0x80; //data 2 
  packet_buffer[17] = data[2] | 0x80; //data 3 
  packet_buffer[18] = data[3] | 0x80; //data 4 
   
  for(int i = 0; i < 4; i++){ //calc the data bytes checksum
    checksum ^= data[i];
  }
  for (int count = 7; count < 14; count++) // xor the packet header bytes
    checksum = checksum ^ packet_buffer[count];
  packet_buffer[19] = checksum | 0xaa;      // 1 c6 1 c4 1 c2 1 c0
  packet_buffer[20] = checksum >> 1 | 0xaa; // 1 c7 1 c5 1 c3 1 c1

  packet_buffer[21] = 0xc8; //PEND
  packet_buffer[22] = 0x00; //end of packet in buffer

}


//*****************************************************************************
// Function: encode_long_status_reply_packet
// Parameters: source
// Returns: none
//
// Description: this is the reply to the extended status command packet. The reply
// includes following:
// data byte 1
// data byte 2-5 number of blocks. 2 is the LSB and 5 the MSB. 
// Size determined from image file.
//*****************************************************************************
void spDevice::encode_extended_status_reply_packet (device d)
{
  uint8_t checksum = 0;

  uint8_t data[5];

  //Build the contents of the packet
  //Info byte
  //Bit 7: Block  device
  //Bit 6: Write allowed
  //Bit 5: Read allowed
  //Bit 4: Device online or disk in drive
  //Bit 3: Format allowed
  //Bit 2: Media write protected
  //Bit 1: Currently interrupting (//c only)
  //Bit 0: Currently open (char devices only) 
  data[0] = 0b11111000;
  //Disk size
  data[1] = d.blocks & 0xff;
  data[2] = (d.blocks >> 8 ) & 0xff;
  data[3] = (d.blocks >> 16 ) & 0xff;
  data[4] = (d.blocks >> 24 ) & 0xff;

  packet_buffer[0] = 0xff;  //sync bytes
  packet_buffer[1] = 0x3f;
  packet_buffer[2] = 0xcf;
  packet_buffer[3] = 0xf3;
  packet_buffer[4] = 0xfc;
  packet_buffer[5] = 0xff;

  packet_buffer[6] = 0xc3;  //PBEGIN - start byte
  packet_buffer[7] = 0x80;  //DEST - dest id - host
  packet_buffer[8] = d.device_id; //SRC - source id - us
  packet_buffer[9] = 0xC1;  //TYPE - extended status
  packet_buffer[10] = 0x80; //AUX
  packet_buffer[11] = 0x80; //STAT - data status
  packet_buffer[12] = 0x85; //ODDCNT - 5 data bytes
  packet_buffer[13] = 0x80; //GRP7CNT
  //5 odd bytes
  packet_buffer[14] = 0x80 | ((data[0]>> 1) & 0x40) | ((data[1]>>2) & 0x20) | (( data[2]>>3) & 0x10) | ((data[3]>>4) & 0x08 ) | ((data[4] >> 5) & 0x04) ; //odd msb
  packet_buffer[15] = data[0] | 0x80; //data 1
  packet_buffer[16] = data[1] | 0x80; //data 2 
  packet_buffer[17] = data[2] | 0x80; //data 3 
  packet_buffer[18] = data[3] | 0x80; //data 4 
  packet_buffer[19] = data[4] | 0x80; //data 5
   
  for(int i = 0; i < 5; i++){ //calc the data bytes checksum
    checksum ^= data[i];
  }
  //calc the data bytes checksum
  for (int count = 7; count < 14; count++) // xor the packet header bytes
    checksum = checksum ^ packet_buffer[count];
  packet_buffer[20] = checksum | 0xaa;      // 1 c6 1 c4 1 c2 1 c0
  packet_buffer[21] = checksum >> 1 | 0xaa; // 1 c7 1 c5 1 c3 1 c1

  packet_buffer[22] = 0xc8; //PEND
  packet_buffer[23] = 0x00; //end of packet in buffer

}
void spDevice::encode_error_reply_packet (uint8_t source)
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
  packet_buffer[9] = 0x80;  //TYPE -status
  packet_buffer[10] = 0x80; //AUX
  packet_buffer[11] = 0xA1; //STAT - data status - error
  packet_buffer[12] = 0x80; //ODDCNT - 0 data bytes
  packet_buffer[13] = 0x80; //GRP7CNT

  for (int count = 7; count < 14; count++) // xor the packet header bytes
    checksum = checksum ^ packet_buffer[count];
  packet_buffer[14] = checksum | 0xaa;      // 1 c6 1 c4 1 c2 1 c0
  packet_buffer[15] = checksum >> 1 | 0xaa; // 1 c7 1 c5 1 c3 1 c1

  packet_buffer[16] = 0xc8; //PEND
  packet_buffer[17] = 0x00; //end of packet in buffer

}

//*****************************************************************************
// Function: encode_status_dib_reply_packet
// Parameters: source
// Returns: none
//
// Description: this is the reply to the status command 03 packet. The reply
// includes following:
// data byte 1
// data byte 2-4 number of blocks. 2 is the LSB and 4 the MSB.
// Calculated from actual image file size.
//*****************************************************************************
void spDevice::encode_status_dib_reply_packet (device d)
{
  int grpbyte, grpcount, i;
  int grpnum, oddnum; 
  uint8_t checksum = 0, grpmsb;
  uint8_t group_buffer[7];
  uint8_t data[25];
  //data buffer=25: 3 x Grp7 + 4 odds
  grpnum=3;
  oddnum=4;
  
  //* write data buffer first (25 bytes) 3 grp7 + 4 odds
  data[0] = 0xf8; //general status - f8 
  //number of blocks =0x00ffff = 65525 or 32mb
  data[1] = d.blocks & 0xff; //block size 1 
  data[2] = (d.blocks >> 8 ) & 0xff; //block size 2 
  data[3] = (d.blocks >> 16 ) & 0xff ; //block size 3 
  data[4] = 0x0b; //ID string length - 11 chars
  data[5] = 'S';
  data[6] = 'M';
  data[7] = 'A';
  data[8] = 'R';
  data[9] = 'T';
  data[10] = 'P';
  data[11] = 'O';
  data[12] = 'R';
  data[13] = 'T';
  data[14] = 'S';
  data[15] = 'D';
  data[16] = ' ';
  data[17] = ' ';
  data[18] = ' ';
  data[19] = ' ';
  data[20] = ' ';  //ID string (16 chars total)
  data[21] = 0x02; //Device type    - 0x02  harddisk
  data[22] = 0x0a; //Device Subtype - 0x0a
  data[23] = 0x01; //Firmware version 2 bytes
  data[24] = 0x0f; //
    

 // print_packet ((uint8_t*) data,packet_length()); // debug
 // Debug_print(("\nData loaded"));
// Calculate checksum of sector bytes before we destroy them
  for (int count = 0; count < 25; count++) // xor all the data bytes
    checksum = checksum ^ data[count];

 // Start assembling the packet at the rear and work 
  // your way to the front so we don't overwrite data
  // we haven't encoded yet

  //grps of 7
  for (grpcount = grpnum-1; grpcount >= 0; grpcount--) // 3
  {
    for (i=0;i<8;i++) {
      group_buffer[i]=data[i + oddnum + (grpcount * 7)];
    }
    // add group msb byte
    grpmsb = 0;
    for (grpbyte = 0; grpbyte < 7; grpbyte++)
      grpmsb = grpmsb | ((group_buffer[grpbyte] >> (grpbyte + 1)) & (0x80 >> (grpbyte + 1)));
    packet_buffer[(14 + oddnum + 1) + (grpcount * 8)] = grpmsb | 0x80; // set msb to one

    // now add the group data bytes bits 6-0
    for (grpbyte = 0; grpbyte < 7; grpbyte++)
      packet_buffer[(14 + oddnum + 2) + (grpcount * 8) + grpbyte] = group_buffer[grpbyte] | 0x80;
  }
       
            
  //odd byte
  packet_buffer[14] = 0x80 | ((data[0]>> 1) & 0x40) | ((data[1]>>2) & 0x20) | (( data[2]>>3) & 0x10) | ((data[3]>>4) & 0x08 ); //odd msb
  packet_buffer[15] = data[0] | 0x80;
  packet_buffer[16] = data[1] | 0x80;
  packet_buffer[17] = data[2] | 0x80;
  packet_buffer[18] = data[3] | 0x80;;

  packet_buffer[0] = 0xff;  //sync bytes
  packet_buffer[1] = 0x3f;
  packet_buffer[2] = 0xcf;
  packet_buffer[3] = 0xf3;
  packet_buffer[4] = 0xfc;
  packet_buffer[5] = 0xff;
  packet_buffer[6] = 0xc3;  //PBEGIN - start byte
  packet_buffer[7] = 0x80;  //DEST - dest id - host
  packet_buffer[8] = d.device_id; //SRC - source id - us
  packet_buffer[9] = 0x81;  //TYPE -status
  packet_buffer[10] = 0x80; //AUX
  packet_buffer[11] = 0x80; //STAT - data status
  packet_buffer[12] = 0x84; //ODDCNT - 4 data bytes
  packet_buffer[13] = 0x83; //GRP7CNT - 3 grps of 7
   
  for (int count = 7; count < 14; count++) // xor the packet header bytes
    checksum = checksum ^ packet_buffer[count];
  packet_buffer[43] = checksum | 0xaa;      // 1 c6 1 c4 1 c2 1 c0
  packet_buffer[44] = checksum >> 1 | 0xaa; // 1 c7 1 c5 1 c3 1 c1

  packet_buffer[45] = 0xc8; //PEND
  packet_buffer[46] = 0x00; //end of packet in buffer
}


//*****************************************************************************
// Function: encode_long_status_dib_reply_packet
// Parameters: source
// Returns: none
//
// Description: this is the reply to the status command 03 packet. The reply
// includes following:
// data byte 1
// data byte 2-5 number of blocks. 2 is the LSB and 5 the MSB.
// Calculated from actual image file size.
//*****************************************************************************
void spDevice::encode_extended_status_dib_reply_packet (device d)
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
  packet_buffer[8] = d.device_id; //SRC - source id - us
  packet_buffer[9] = 0x81;  //TYPE -status
  packet_buffer[10] = 0x80; //AUX
  packet_buffer[11] = 0x83; //STAT - data status
  packet_buffer[12] = 0x80; //ODDCNT - 4 data bytes
  packet_buffer[13] = 0x83; //GRP7CNT - 3 grps of 7
  packet_buffer[14] = 0xf0; //grp1 msb
  packet_buffer[15] = 0xf8; //general status - f8
  //number of blocks =0x00ffff = 65525 or 32mb
  packet_buffer[16] = d.blocks & 0xff; //block size 1 
  packet_buffer[17] = (d.blocks >> 8 ) & 0xff; //block size 2 
  packet_buffer[18] = ((d.blocks >> 16 ) & 0xff) | 0x80 ; //block size 3 - why is the high bit set?
  packet_buffer[19] = ((d.blocks >> 24 ) & 0xff) | 0x80 ; //block size 3 - why is the high bit set?  
  packet_buffer[20] = 0x8d; //ID string length - 13 chars
  packet_buffer[21] = 'S';
  packet_buffer[22] = 'm';  //ID string (16 chars total)
  packet_buffer[23] = 0x80; //grp2 msb
  packet_buffer[24] = 'a';
  packet_buffer[25] = 'r';
  packet_buffer[26] = 't';
  packet_buffer[27] = 'p';
  packet_buffer[28] = 'o';
  packet_buffer[29] = 'r';
  packet_buffer[30] = 't';
  packet_buffer[31] = 0x80; //grp3 msb
  packet_buffer[32] = ' ';
  packet_buffer[33] = 'S';
  packet_buffer[34] = 'D';
  packet_buffer[35] = ' ';
  packet_buffer[36] = ' ';
  packet_buffer[37] = ' ';
  packet_buffer[38] = ' ';
  packet_buffer[39] = 0x80; //odd msb
  packet_buffer[40] = 0x02; //Device type    - 0x02  harddisk
  packet_buffer[41] = 0x00; //Device Subtype - 0x20
  packet_buffer[42] = 0x01; //Firmware version 2 bytes
  packet_buffer[43]=  0x0f;
  packet_buffer[44] = 0x90; //

  for (int count = 7; count < 45; count++) // xor the packet bytes
    checksum = checksum ^ packet_buffer[count];
  packet_buffer[45] = checksum | 0xaa;      // 1 c6 1 c4 1 c2 1 c0
  packet_buffer[46] = checksum >> 1 | 0xaa; // 1 c7 1 c5 1 c3 1 c1

  packet_buffer[47] = 0xc8; //PEND
  packet_buffer[48] = 0x00; //end of packet in buffer

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
int spDevice::verify_cmdpkt_checksum(void)
{
  int length;
  uint8_t evenbits, oddbits, bit7, bit0to6, grpbyte;
  uint8_t calc_checksum = 0; //initial value is 0
  uint8_t pkt_checksum;

  length = packet_length();

  //2 oddbytes in cmd packet
  calc_checksum ^= ((packet_buffer[13] << 1) & 0x80) | (packet_buffer[14] & 0x7f);
  calc_checksum ^= ((packet_buffer[13] << 2) & 0x80) | (packet_buffer[15] & 0x7f);

  // 1 group of 7 in a cmd packet
  for (grpbyte = 0; grpbyte < 7; grpbyte++) {
    bit7 = (packet_buffer[16] << (grpbyte + 1)) & 0x80;
    bit0to6 = (packet_buffer[17 + grpbyte]) & 0x7f;
    calc_checksum ^= bit7 | bit0to6;
  }

  // calculate checksum for overhead bytes
  for (int count = 6; count < 13; count++) // start from first id byte
    calc_checksum ^= packet_buffer[count];

  oddbits = (packet_buffer[length - 2] << 1) | 0x01;
  evenbits = packet_buffer[length - 3];
  pkt_checksum = oddbits | evenbits;

  //  Debug_print(("Pkt Chksum Byte:\r\n"));
  //  Debug_print(pkt_checksum,DEC);
  //  Debug_print(("Calc Chksum Byte:\r\n"));
  //  Debug_print(calc_checksum,DEC);

  if ( pkt_checksum == calc_checksum )
    return 1;
  else
    return 0;

}

//*****************************************************************************
// Function: print_packet
// Parameters: pointer to data, number of bytes to be printed
// Returns: none
//
// Description: prints packet data for debug purposes to the serial port
//*****************************************************************************
void spDevice::print_packet (uint8_t* data, int bytes)
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
        Debug_print(data[count + row], HEX);
        Debug_print(" ");
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

//*****************************************************************************
// Function: packet_length
// Parameters: none
// Returns: length
//
// Description: Calculates the length of the packet in the packet_buffer.
// A zero marks the end of the packet data.
//*****************************************************************************
int spDevice::packet_length (void)
{
  int x = 0;

  while (packet_buffer[x++]);
  return x - 1; // point to last packet byte = C8
}


//*****************************************************************************
// Function: led_err
// Parameters: none
// Returns: nonthing
//
// Description: Flashes status led for show error status
//
//*****************************************************************************
void spDevice::led_err(void)
{
  // todo replace with fnSystem LED call
  // int i = 0;
  // todo interrupts();
  Debug_printf(("\r\nError!"));
  fnLedManager.blink(eLed::LED_BUS, 5);

 /*  pinMode(statusledPin, OUTPUT);

  for (i = 0; i < 5; i++) {
    digitalWrite(statusledPin, HIGH);
    fnSystem.delay(1500);
    digitalWrite(statusledPin, LOW);
    fnSystem.delay(100);
    digitalWrite(statusledPin, HIGH);
    fnSystem.delay(1500);
    digitalWrite(statusledPin, HIGH);
  } */

}




//*****************************************************************************
// Function: mcuInit
// Parameters: none
// Returns: none
//
// Description: Initialize the ATMega32
//*****************************************************************************
void spDevice::mcuInit(void)
{
  fnSystem.set_pin_mode(SP_ACK, gpio_mode_t::GPIO_MODE_OUTPUT);
  fnSystem.digital_write(SP_ACK, DIGI_HIGH);
  //set ack (hv) to input to avoid clashing with other devices when sp bus is not enabled
  fnSystem.set_pin_mode(SP_ACK, gpio_mode_t::GPIO_MODE_INPUT); //, SystemManager::PULL_UP ); // todo: test this - i think this makes sense to keep the ACK line high while not in use
  
  fnSystem.set_pin_mode(SP_PHI0, gpio_mode_t::GPIO_MODE_INPUT);
  fnSystem.set_pin_mode(SP_PHI1, gpio_mode_t::GPIO_MODE_INPUT);
  fnSystem.set_pin_mode(SP_PHI2, gpio_mode_t::GPIO_MODE_INPUT);
  fnSystem.set_pin_mode(SP_PHI3, gpio_mode_t::GPIO_MODE_INPUT);

  fnSystem.set_pin_mode(SP_WRDATA, gpio_mode_t::GPIO_MODE_INPUT);

  fnSystem.set_pin_mode(SP_RDDATA, gpio_mode_t::GPIO_MODE_OUTPUT);
  fnSystem.digital_write(SP_RDDATA, DIGI_LOW);
  // leave rd as input, pd6
  fnSystem.set_pin_mode(SP_RDDATA, gpio_mode_t::GPIO_MODE_INPUT); //, SystemManager::PULL_DOWN );  

  fnSystem.set_pin_mode(SP_EXTRA, gpio_mode_t::GPIO_MODE_OUTPUT);
}

/* todo memory reporting, although FujiNet firmware does this already
#ifdef __arm__
// should use uinstd.h to define sbrk but Due causes a conflict
extern "C" char* sbrk(int incr);
#else  // __ARM__
extern char *__brkval;
#endif  // __arm__
*/

int spDevice::freeMemory() {
//todo memory reporting, although FujiNet firmware does this already
/*   extern int __bss_end;
  //extern int *__brkval;
  int free_memory;
  if ((int)__brkval == 0) {
    // if no heap use from end of bss section
    free_memory = ((int)&free_memory) - ((int)&__bss_end);
  } else {
    // use from top of stack to heap
    free_memory = ((int)&free_memory) - ((int)__brkval);
  }
  return free_memory;
 */
 return 0;
 }


bool spDevice::open_tnfs_image( device &d)
{
  Debug_printf("\r\nmounting server");
  tserver.start("159.203.160.80"); //"atari-apps.irata.online");
  Debug_printf("\r\nopening file");
  d.sdf = tserver.file_open("/test.hdv","rb");

  Debug_printf(("\r\nTesting file "));
  // d.sdf.printName();
  if(d.sdf == nullptr) // .isOpen()||!d.sdf.isFile())
  {
    Debug_printf(("\r\nFile must exist, be open and be a regular file before checking for valid image type!"));
    return false;
  }

  long s = tserver.filesize(d.sdf);
  
  if ( ( s != ((s>>9)<<9) ) || (s==0) || (s==-1))
  {
    Debug_printf(("\r\nFile must be an unadorned ProDOS order image with no header!"));
    Debug_printf(("\r\nThis means its size must be an exact multiple of 512!"));
    return false;
  }

  Debug_printf(("\r\nFile good!"));
  d.blocks = tserver.filesize(d.sdf) >> 9;

  return true;

}
// TODO: Allow image files with headers, too
// TODO: Respect read-only bit in header
bool spDevice::open_image( device &d, std::string filename )
{
   // d.sdf = sdcard.open(filename, O_RDWR);
  Debug_printf("\r\nright before file open call");
  d.sdf = fnSDFAT.file_open(filename.c_str(), "rb");
  Debug_printf(("\r\nTesting file "));
  // d.sdf.printName();
  if(d.sdf == nullptr) // .isOpen()||!d.sdf.isFile())
  {
    Debug_printf(("\r\nFile must exist, be open and be a regular file before checking for valid image type!"));
    return false;
  }

  long s = fnSDFAT.filesize(d.sdf);
  if ( ( s != ((s>>9)<<9) ) || (s==0) || (s==-1))
  {
    Debug_printf(("\r\nFile must be an unadorned ProDOS order image with no header!"));
    Debug_printf(("\r\nThis means its size must be an exact multiple of 512!"));
    return false;
  }

  Debug_printf(("\r\nFile good!"));
  d.blocks = fnSDFAT.filesize(d.sdf) >> 9;

  return true;
}


void spDevice::spsd_setup() {
  //sdf[0] = &sdf1;
  //sdf[1] = &sdf2;
  // put your setup code here, to run once:
  mcuInit();
  
  // already done in main.cpp - Debug_begin(230400);
  Debug_printf(("\r\nSmartportSD v1.15\r\n"));
  /* todo 
  initPartition = eeprom_read_byte(0);
  if (initPartition == 0xFF) initPartition = 0;
  initPartition = (initPartition % 4); 
  */
  

  Debug_printf(("\r\nFree memory before opening images: "));
  Debug_print(freeMemory());

  for(uint8_t i=0; i<NUM_PARTITIONS; i++)
  {
    std::string part = "/PART";
    part += std::to_string(i+1);
    part += ".PO";
    Debug_printf("\r\nopening %s",part.c_str());
    // open_image(devices[i], part ); // std::string operations
    open_tnfs_image(devices[i]);
    if(devices[i].sdf != nullptr)
      Debug_printf("\r\n%s open good",part.c_str());
    else
    {
      Debug_printf(("\r\nImage "));
      Debug_print(i, DEC);
      Debug_print((" open error! Filename = "));
      Debug_print(part.c_str());
    } 
  }  
  
  timer_config();
  Debug_printf("\r\ntimer started");

  Debug_println();
  // to do: figure out how RDDATA is shared on the daisy chain
  fnSystem.digital_write(SP_RDDATA, DIGI_LOW);
}

//*****************************************************************************
// Function: main loop
// Parameters: none
// Returns: 0
//
// Description: Main function for Apple //c Smartport Compact Flash adpater
//*****************************************************************************
void spDevice::spsd_loop() 
{
  smartport_rddata_disable();
  smartport_rddata_clr();
  while (true)
  {
    smartport_ack_set();
    // read phase lines to check for smartport reset or enable
    phasestate = smartport_phases();

    switch (phasestate)
    {
    case phasestate_t::idle:
#ifdef VERBOSE
      Debug_printf("\r\nIdle Case");
      fnSystem.delay(100);
#endif
      break;
    case phasestate_t::reset:
#ifdef VERBOSE
      Debug_printf("\r\nReset Case");
      fnSystem.delay(100);
#endif
      Debug_printf(("\r\nReset"));
      while (smartport_phases() == phasestate_t::reset)
        ; // todo: should there be a timeout feature?
        // hard coding 1 partition - will use disk class instances  instead                                                    // to check if needed
        devices[0].device_id = 0;
        Debug_printf(("\r\nReset Cleared"));
      break;
    case phasestate_t::enable:
#ifdef VERBOSE
      Debug_printf("\r\nEnable Case");
      fnSystem.delay(100);
#endif
      portDISABLE_INTERRUPTS();
      smartport_ack_enable();
      if (ReceivePacket((uint8_t *)packet_buffer))
      {
        portENABLE_INTERRUPTS(); 
        break; //error timeout, break and loop again  // todo: for now ack going low is in ReceivePacket
      }
      portENABLE_INTERRUPTS();

#ifdef DEBUG
      Debug_printf("\r\n");
      for (int i = 0; i < 28; i++)
      {
        Debug_printf("%02x ", packet_buffer[i]);
        if (packet_buffer[i] == 0)
          break;
      }
      Debug_printf("\r\n");
#endif

      switch (packet_buffer[14])
      {
      case 0x81: // read block
        Debug_printf("\r\nhandling read block command");
        handle_readblock();
        break;
      case 0x85: //is an init cmd
        Debug_printf("\r\nhandling init command");
        handle_init();
        break;
} // switch (cmd)
    } // switch (phasestate)
  } // while(true)
}

void spDevice::handle_readblock()
{
 uint8_t LBH, LBL, LBN, LBT;
 unsigned long int block_num;
 size_t sdstato;
 uint8_t source;

  source = packet_buffer[6];
  Debug_printf("\r\nDrive %02x", source);

  LBH = packet_buffer[16]; //high order bits
  LBT = packet_buffer[21]; //block number high
  LBL = packet_buffer[20]; //block number middle
  LBN = packet_buffer[19]; //block number low
  block_num = (LBN & 0x7f) | (((unsigned short)LBH << 3) & 0x80);
  // block num second byte
  //print_packet ((unsigned char*) packet_buffer,packet_length());
  //Added (unsigned short) cast to ensure calculated block is not underflowing.
  block_num = block_num + (((LBL & 0x7f) | (((unsigned short)LBH << 4) & 0x80)) << 8);
  block_num = block_num + (((LBT & 0x7f) | (((unsigned short)LBH << 5) & 0x80)) << 16);
  Debug_printf("\r\nRead block %02x",block_num);

  if (fseek(devices[0].sdf, (block_num * 512), SEEK_SET))
  {
    Debug_printf("\r\nRead seek err! block #%02x", block_num);
    if (devices[0].sdf != nullptr)
    {
      Debug_printf("\r\nPartition file is open!");
    }
    else
    {
      Debug_printf("\r\nPartition file is closed!");
    }
    return;
  }

  sdstato = fread((unsigned char *)packet_buffer, 1, 512, devices[0].sdf); //Reading block from SD Card
  if (sdstato != 512)
  {
    Debug_printf("\r\nFile Read err: %d bytes", sdstato);
    return;
  }
  encode_data_packet(source);
  Debug_printf("\r\nsending block packet ...");
  //smartport_ack_set(); // todo: probably put ack req handshake inside of send and receive packet()

  SendPacket((unsigned char *)packet_buffer); // this returns timeout errors but that's not handled here


  //Debug_printf(status);
  //print_packet ((unsigned char*) packet_buffer,packet_length());
  //print_packet ((unsigned char*) sector_buffer,15);
}

void spDevice::handle_init()
{
  uint8_t source;

  smartport_rddata_enable(); // todo: instead, do we enable rddata and ack when phasestate==enable?
  smartport_rddata_clr(); // todo: enable is done below so maybe not needed here?

  source = packet_buffer[6];
  // if (number_partitions_initialised < NUM_PARTITIONS)
  // {                                                                                                   //are all init'd yet
  //   devices[(number_partitions_initialised - 1 + initPartition) % NUM_PARTITIONS].device_id = source; //remember source id for partition
  //   number_partitions_initialised++;
  //   status = 0x80; //no, so status=0
  // }
  // else if (number_partitions_initialised == NUM_PARTITIONS)
  // {                                                                                                   // the last one
  //   devices[(number_partitions_initialised - 1 + initPartition) % NUM_PARTITIONS].device_id = source; //remember source id for partition
  //   number_partitions_initialised++;
  //   status = 0xff; //yes, so status=non zero
  // }
    devices[0].device_id = source; //remember source id for partition
    uint8_t status = 0xff; //yes, so status=non zero

  encode_init_reply_packet(source, status);
  //print_packet ((uint8_t*) packet_buffer,packet_length());
  Debug_printf("\r\nSending INIT Response Packet...");
  // portDISABLE_INTERRUPTS();
  // smartport_rddata_enable();
  SendPacket((uint8_t *)packet_buffer); // timeout error return is not handled here (yet?)
  // ack-req handshake is inside of sendpacket
  // smartport_rddata_disable();
  // portENABLE_INTERRUPTS(); // takes 7 us to execute

  //print_packet ((uint8_t*) packet_buffer,packet_length());

  Debug_printf(("\r\nDrive: %02x"),devices[0].device_id);
}

void spDevice::timer_1us_example()
{
  fnSystem.set_pin_mode(PIN_INT, gpio_mode_t::GPIO_MODE_OUTPUT);
  // uint8_t o = DIGI_LOW;
  int64_t t0 = esp_timer_get_time();
  int64_t tn;
  while(1)
  {
    //fnSystem.digital_write(PIN_INT,o);
    GPIO.out_w1ts = ((uint32_t)1 << PIN_INT);
    // o = (~o);
    tn = t0 + 3;
    do
    {
    t0 = esp_timer_get_time(); 
    } while (t0<=tn);
    GPIO.out_w1tc = ((uint32_t)1 << PIN_INT);
    tn = t0 + 3;
    do
    {
    t0 = esp_timer_get_time(); 
    } while (t0<=tn);
  }
}


void spDevice::timer_config()
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


#define DELAY 159

void spDevice::hw_timer_pulses()
{
  fnSystem.set_pin_mode(PIN_INT, gpio_mode_t::GPIO_MODE_OUTPUT);
  
  timer_config_t config;
  config.divider = TIMER_DIVIDER; // default clock source is APB
  config.counter_dir = TIMER_COUNT_UP;
  config.counter_en = TIMER_PAUSE;
  config.alarm_en = TIMER_ALARM_DIS;

  /* Timer's counter will initially start from value below.
       Also, if auto_reload is set, this value will be automatically reload on alarm */
  timer_init(TIMER_GROUP_1, TIMER_1, &config);
  timer_set_counter_value(TIMER_GROUP_1, TIMER_1, 0);

  uint32_t t0 = 0;
  uint32_t tn = t0 + DELAY;
  timer_start(TIMER_GROUP_1, TIMER_1);
  while (1)
  {
    do
    {
      TIMERG1.hw_timer[1].update = 0;
      t0 = TIMERG1.hw_timer[1].cnt_low;
      // WRITE_PERI_REG(TIMG_T0UPDATE_REG(1), 0); // 0x3FF6000C
      // t0 = READ_PERI_REG(TIMG_T0LO_REG(1));    // 0x3FF60004
    } while (t0 < tn);
    GPIO.out_w1tc = ((uint32_t)1 << PIN_INT);
    tn = t0 + DELAY;
    do
    {
      TIMERG1.hw_timer[1].update = 0;
      t0 = TIMERG1.hw_timer[1].cnt_low;
      // WRITE_PERI_REG(TIMG_T0UPDATE_REG(1), 0); // 0x3FF6000C
      // t0 = READ_PERI_REG(TIMG_T0LO_REG(1));    // 0x3FF60004
    } while (t0 < tn);
    GPIO.out_w1ts = ((uint32_t)1 << PIN_INT);
    tn = t0 + DELAY;
  }
}

void spDevice::hw_timer_direct_reg()
{
  timer_config_t config;
  config.divider = TIMER_DIVIDER; // default clock source is APB
  config.counter_dir = TIMER_COUNT_UP;
  config.counter_en = TIMER_PAUSE;
  config.alarm_en = TIMER_ALARM_DIS;

  /* Timer's counter will initially start from value below.
       Also, if auto_reload is set, this value will be automatically reload on alarm */
  timer_init(TIMER_GROUP_1, TIMER_1, &config);
  timer_set_counter_value(TIMER_GROUP_1, TIMER_1, 0);


  uint64_t t0 = 0;
  uint32_t tlo;
  timer_start(TIMER_GROUP_1, TIMER_1);
  while (1)
  {
      timer_get_counter_value(TIMER_GROUP_1, TIMER_1, &t0);
      // WRITE_PERI_REG(TIMG_T0UPDATE_REG(0),0);
      //tlo = READ_PERI_REG(TIMG_T0LO_REG(0)); 
      //Debug_printf("%lu ", tlo);
      // WRITE_PERI_REG(TIMG_T1UPDATE_REG(0),0);
      // tlo = READ_PERI_REG(TIMG_T1LO_REG(0)); 
      // Debug_printf("%lu ", tlo);
      WRITE_PERI_REG(TIMG_T0UPDATE_REG(1),0); // 0x3FF6000C
      tlo = READ_PERI_REG(TIMG_T0LO_REG(1)); // 0x3FF60004
      Debug_printf("%lu %lu \r\n",tlo, uint32_t(t0));
      // Debug_printf("%lu\r\n", tlo);
      // WRITE_PERI_REG(TIMG_T1UPDATE_REG(1),0);
      // tlo = READ_PERI_REG(TIMG_T1LO_REG(1)); 
      // Debug_printf("%lu\r\n ", tlo);
  }
}

const uint8_t a[] {0x3f,0xcf,0xf3,0xfc,0xff,0xc3,0x81,0x80,0x80,0x80,0x80,0x3f,0xcf,0xf3,0xfc,0xff,0xc3,0x81,0x80,0x80,0x80,0x80,0x3f,0xcf,0xf3,0xfc,0xff,0xc3,0x81,0x80,0x80,0x80,0x80,0x3f,0xcf,0xf3,0xfc,0xff,0xc3,0x81,0x80,0x80,0x80,0x80,0x3f,0xcf,0xf3,0xfc,0xff,0xc3,0x81,0x80,0x80,0x80,0x80,0x3f,0xcf,0xf3,0xfc,0xff,0xc3,0x81,0x80,0x80,0x80,0x80,0x3f,0xcf,0xf3,0xfc,0xff,0xc3,0x81,0x80,0x80,0x80,0x80,0x3f,0xcf,0xf3,0xfc,0xff,0xc3,0x81,0x80,0x80,0x80,0x80,0x00};

void spDevice::test_send()
/*
FF SYNC SELF SYNCHRONIZING BYTES 0
3F : : 32 micro Sec.
CF : : 32 micro Sec.
F3 : : 32 micro Sec.
FC : : 32 micro Sec.
FF : : 32 micro Sec.
C3 PBEGIN MARKS BEGINNING OF PACKET 32 micro Sec.
81 DEST DESTINATION UNIT NUMBER 32 micro Sec.
80 SRC SOURCE UNIT NUMBER 32 micro Sec.
80 TYPE PACKET TYPE FIELD 32 micro Sec.
80 AUX PACKET AUXILLIARY TYPE FIELD 32 micro Sec.
80
*/
{
  fnSystem.set_pin_mode(SP_RDDATA, gpio_mode_t::GPIO_MODE_OUTPUT);
  fnSystem.set_pin_mode(SP_ACK, gpio_mode_t::GPIO_MODE_OUTPUT);
  fnSystem.digital_write(SP_ACK, DIGI_LOW);
  fnSystem.digital_write(SP_EXTRA,DIGI_LOW);
  while(1)
  {
    Debug_printf("\r\nsending packet now");

    SendPacket(a);

    fnSystem.delay(1000);
  }
}

void IRAM_ATTR spDevice::test_edge_capture()
{
  uint32_t stamp[100];
  uint32_t prev_val = ((uint32_t)0x01 << (SP_WRDATA - 32));
  uint32_t curr_val;
  int i=0;

  portDISABLE_INTERRUPTS();
  hw_timer_reset();
  do
  {
    if (prev_val)
      smartport_extra_set();
    else
      smartport_extra_clr();   
    do
    {
      curr_val = smartport_wrdata_val();
    } while (curr_val == prev_val);
    hw_timer_latch();
    hw_timer_read();
    stamp[i] = t0;
    prev_val = curr_val;
  } while (++i < 20);
  portENABLE_INTERRUPTS();
  for (int i = 0; i < 20; i++)
  {
    Debug_printf("\r\n%d",stamp[i]);
  }
}


#endif