#ifdef BUILD_APPLE
#include "iwm.h"

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

//      SP BUS     GPIO       SIO
//      ---------  ----     ---------
#define SP_WRPROT   27
#define SP_ACK      27      //  CLKIN
#define SP_REQ      39
#define SP_PHI0     39      //  CMD
#define SP_PHI1     22      //  PROC
#define SP_PHI2     36      //  MOTOR
#define SP_PHI3     26      //  INT
#define SP_RDDATA   21      //  DATAIN
#define SP_WRDATA   33      //  DATAOUT

// figure out which ones are required
// #include "esp_timer.h"
#include "driver/timer.h" // contains the hardware timer register data structure
//#include "soc/timer_group_reg.h"

#include "../../include/debug.h"
#include "fnSystem.h"
// #include "led.h"

#include "fnFsTNFS.h"

#include <string.h>

#define TIMER_DIVIDER         (2)  //  Hardware timer clock divider
#define TIMER_SCALE           (TIMER_BASE_CLK / TIMER_DIVIDER)  // convert counter value to seconds
#define TIMER_USEC_FACTOR     (TIMER_SCALE / 1000000)
#define TIMER_100NS_FACTOR    (TIMER_SCALE / 100000)
#define TIMER_ADJUST          5 // substract this value to adjust for overhead

#define IWM_BIT_CELL          4 // microseconds - 2 us for fast mode
#define IWM_TX_PW             1 // microseconds - 1/2 us for fast mode

#undef VERBOSE_IWM
#undef TESTTX


FileSystemTNFS tserver;

//------------------------------------------------------------------------------

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

void iwmBus::iwm_timer_latch()
{
  TIMERG1.hw_timer[1].update = 0;
}

void iwmBus::iwm_timer_read()
{
  iwm_timer.t0 = TIMERG1.hw_timer[1].cnt_low;
}

void iwmBus::iwm_timer_alarm_set(int s)
{
  iwm_timer.tn = iwm_timer.t0 + s * TIMER_USEC_FACTOR - TIMER_ADJUST;
}

void iwmBus::iwm_timer_alarm_snooze(int s)
{
  iwm_timer.tn += s * TIMER_USEC_FACTOR - TIMER_ADJUST; // 3 microseconds

}

void iwmBus::iwm_timer_wait()
{
  do
  {
    iwm_timer_latch();
    iwm_timer_read();
  } while (iwm_timer.t0 < iwm_timer.tn);
}

void iwmBus::iwm_timer_reset()
{
  TIMERG1.hw_timer[1].load_low = 0;
  TIMERG1.hw_timer[1].reload = 0;
}

void iwmBus::iwm_rddata_set()
{
  GPIO.out_w1ts = ((uint32_t)1 << SP_RDDATA);
}

void iwmBus::iwm_rddata_clr()
{
  GPIO.out_w1tc = ((uint32_t)1 << SP_RDDATA);
}

void iwmBus::iwm_rddata_enable()
{
  GPIO.enable_w1ts = ((uint32_t)0x01 << SP_RDDATA);  
}

void iwmBus::iwm_rddata_disable()
{
  GPIO.enable_w1tc = ((uint32_t)0x01 << SP_RDDATA);
}

bool iwmBus::iwm_wrdata_val()
{
  return (GPIO.in1.val & ((uint32_t)0x01 << (SP_WRDATA - 32)));
}

bool iwmBus::iwm_req_val()
{
  return (GPIO.in1.val & (0x01 << (SP_REQ-32)));
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
void iwmBus::iwm_ack_clr()
{
  //GPIO.enable_w1ts = ((uint32_t)0x01 << SP_ACK);
GPIO.out_w1tc = ((uint32_t)1 << SP_ACK);
#ifdef VERBOSE_IWM
  Debug_print("a");
#endif
}

void iwmBus::iwm_ack_set()
{
  //GPIO.enable_w1tc = ((uint32_t)0x01 << SP_ACK);
GPIO.out_w1ts = ((uint32_t)1 << SP_ACK);
#ifdef VERBOSE_IWM
  Debug_print("A");
#endif
}

void iwmBus::iwm_ack_enable()
{
  GPIO.enable_w1ts = ((uint32_t)0x01 << SP_ACK);  
}

void iwmBus::iwm_ack_disable()
{
  GPIO.enable_w1tc = ((uint32_t)0x01 << SP_ACK);
}

//------------------------------------------------------

bool iwmBus::iwm_phase_val(int p)
{ 
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
  if (iwm_phase_val(1) && iwm_phase_val(3))
    phasestate = iwm_phases_t::enable;
  else if (iwm_phase_val(0) && iwm_phase_val(2) && !iwm_phase_val(1) && !iwm_phase_val(3))
    phasestate = iwm_phases_t::reset;

#ifdef DEBUG
  if (phasestate != oldphase)
  {
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


int IRAM_ATTR iwmBus::iwm_read_packet(uint8_t *a)
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
 * reads 250kbit/sec serial data that includes sometimes irregular timing between 
 * bytes. The SP serial data are encoded such that the logical meaning of the 
 * current bit depends on the signal level of the previous bit. This can also
 * be interpreted as over-lapping two-bit sequences. Byte framing is done by
 * ensuring the first bit (really bit 7) of the next byte is always the 
 * opposite signal level of the last bit (bit 0) of the current byte. The
 * algorithm waits for the transition and then starts the new byte.
 */

  // 'a' is the receive buffer pointer

  iwm_timer_reset();
  // cache all the functions
  iwm_timer_latch();        // latch highspeed timer value
  iwm_timer_read();
  iwm_timer_alarm_set(1);
  iwm_timer_wait();
  iwm_timer_alarm_snooze(1);
  iwm_timer_wait();
  
  // setup a timeout counter to wait for REQ response
  iwm_timer_latch();        // latch highspeed timer value
  iwm_timer_read();      //  grab timer low word
  iwm_timer_alarm_set(100); // logic analyzer says 40 usec

  // todo: this waiting for REQ seems like it should be done
  // in the main loop, if control can be passed quickly enough
  // to the receive routine. Otherwise, we sit here blocking(?)
  // until REQ goes high. As long as PHIx is in Enable mode.
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
      return 1;
    }
  };

#ifdef VERBOSE_IWM
  // REQ received!
  Debug_print("R");
#endif


  // we might want to just block on writedata without timout so there's
  // faster control passed to the do-loop
  // setup a timeout counter to wait for WRDATA to be ready response
  iwm_timer_latch();                    // latch highspeed timer value
  iwm_timer_read();    //  grab timer low word
  iwm_timer_alarm_set(32); // 32 usec - 1 byte
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
      return 1;
    }
  };

  iwm_timer_alarm_set(1);
  iwm_timer_wait();

  do // have_data
  {
    // beginning of the byte
    // delay 2 us until middle of 4-us bit
    // testing: can we rely on previous t0 value?
    //iwm_timer_latch();
    //iwm_timer_read();
    iwm_timer_alarm_set( 2 * (idx>0) ); //TIMER_SCALE / 500000; // 2 usec
 
//    iwm_timer_wait();
    do
    {
      iwm_timer_wait();
      // logic table:
      //  prev_level  current_level   decoded bit
      //  0           0               0
      //  0           1               1
      //  1           0               1
      //  1           1               0
      // this is an exclusive OR operation
      //todo: can curent_level and prev_level be bools and then uint32_t is implicitly
      //typecast down to bool? then the code can be:
      current_level = iwm_wrdata_val();       // nxtbit:   sbic _SFR_IO_ADDR(PIND),7           ;2   ;2    ;1  ;1      ;1/2 now read a bit, cycle time is 4us
      iwm_timer_alarm_set(IWM_BIT_CELL); // 4 usec
      bit = prev_level ^ current_level;
      rxbyte <<= 1;
      rxbyte |= bit;
      prev_level = current_level;
      //
      // current_level = iwm_wrdata_val();       // nxtbit:   sbic _SFR_IO_ADDR(PIND),7           ;2   ;2    ;1  ;1      ;1/2 now read a bit, cycle time is 4us
      // iwm_timer_alarm_set(4); // 4 usec
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
    iwm_timer_alarm_snooze(19); // 19 usec from smartportsd assy routine
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
        have_data = false;
        break;
      }
    } while (iwm_wrdata_val() == prev_level);
       numbits = 8; // ;1   8bits to read
  } while (have_data); //(have_data); // while have_data
  //           rjmp nxtbyte                        ;46  ;47               ;2   get next byte

  // endpkt:   clr  r23
  a[idx] = 0; //           st   x+,r23               ;save zero byte in buffer to mark end

  //iwm_ack_clr();
  iwm_ack_enable(); // should already be cleared
  while (iwm_req_val())
    ;

  return 0;
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

  iwm_ack_set(); // ack is already enabled by the response to the command read

#ifndef TESTTX
  // 1:        sbic _SFR_IO_ADDR(PIND),2   ;wait for req line to go high
  // setup a timeout counter to wait for REQ response
  iwm_timer_latch();        // latch highspeed timer value
  iwm_timer_read();      //  grab timer low word
  iwm_timer_alarm_set(100); // 10 millisecond

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
      portENABLE_INTERRUPTS(); // takes 7 us to execute
      return 1;
    }
  };
// ;

#ifdef VERBOSE_IWM
  // REQ received!
  Debug_print("R");
#endif

#endif // TESTTX

  // CRITICAL TO HAVE 1 US BETWEEN req AND FIRST PULSE to put the falling edge 2 us after REQ
  // at one point i had to do a trim alarm setting, but now can do standard call - i think the
  // timing behavior changed because I call alarm_set and alarm_snooze up at the top
  // because i think i'm caching the function calls
  //iwm_timer.tn = iwm_timer.t0 + (1 * TIMER_USEC_FACTOR / 2) - TIMER_ADJUST; // NEED JUST 1/2 USEC
  iwm_timer_alarm_set(1);
  iwm_timer_wait();

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
      iwm_timer_alarm_snooze(1); // 1 microsecond - snooze to finish off 4 us period
      iwm_timer_wait();

      iwm_rddata_clr();
      iwm_timer_alarm_set(3); // 3 microseconds - set on falling edge of pulse

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
  while (iwm_req_val());

  iwm_rddata_disable();
  portENABLE_INTERRUPTS(); // takes 7 us to execute
  
return 0;
}

void iwmBus::setup(void)
{
  Debug_printf(("\r\nIWM FujiNet based on SmartportSD v1.15\r\n"));

  fnSystem.set_pin_mode(SP_ACK, gpio_mode_t::GPIO_MODE_OUTPUT);
  fnSystem.digital_write(SP_ACK, DIGI_LOW); // set up ACK ahead of time to go LOW when enabled
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
  fnSystem.set_pin_mode(SP_RDDATA, gpio_mode_t::GPIO_MODE_INPUT); //, SystemManager::PULL_DOWN );  ot maybe pull up, too?
  Debug_printf("\r\nIWM GPIO configured");

  timer_config();
  Debug_printf("\r\nIWM timer started");

  // todo - get rid of this code
  smort.open_tnfs_image();
  if (smort.d.sdf != nullptr)
    Debug_printf("\r\nfile open good");
  else
    Debug_printf("\r\nImage open error!");
  Debug_printf("\r\nDemo TNFS file open complete - remember to remove this code");
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
void iwmDevice::encode_data_packet (uint8_t source)
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
// Description: decode 512 byte data packet for write block command from host
// decodes the data from the packet_buffer IN-PLACE!
//*****************************************************************************
int iwmDevice::decode_data_packet (void)
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
void iwmDevice::encode_status_reply_packet()
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
void iwmDevice::encode_extended_status_reply_packet()
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
void iwmDevice::encode_error_reply_packet (uint8_t source)
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
void iwmDevice::encode_status_dib_reply_packet()
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
void iwmDevice::encode_extended_status_dib_reply_packet()
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
int iwmDevice::verify_cmdpkt_checksum(void)
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

#ifdef DEBUG
//*****************************************************************************
// Function: print_packet
// Parameters: pointer to data, number of bytes to be printed
// Returns: none
//
// Description: prints packet data for debug purposes to the serial port
//*****************************************************************************
void iwmDevice::print_packet (uint8_t* data, int bytes)
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
#endif

//*****************************************************************************
// Function: packet_length
// Parameters: none
// Returns: length
//
// Description: Calculates the length of the packet in the packet_buffer.
// A zero marks the end of the packet data.
//*****************************************************************************
int iwmDevice::packet_length (void)
{
  int x = 0;

  while (packet_buffer[x++]);
  return x - 1; // point to last packet byte = C8
}


bool iwmDevice::open_tnfs_image()
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
bool iwmDevice::open_image(std::string filename )
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

//*****************************************************************************
// Function: main loop
// //*****************************************************************************
void iwmBus::service() 
{
  iwm_rddata_disable(); // todo - figure out sequence of setting IWM pins and where this should go
  iwm_rddata_clr();
  while (true)
  {
    iwm_ack_disable();
    iwm_ack_clr(); // prep for the next read packet
     
    // read phase lines to check for smartport reset or enable
    switch (iwm_phases())
    {
    case iwm_phases_t::idle:
      break;
    case iwm_phases_t::reset:
      Debug_printf(("\r\nReset"));
      while (iwm_phases() == iwm_phases_t::reset)
        ; // todo: should there be a timeout feature?
        // hard coding 1 partition - will use disk class instances  instead                                                    // to check if needed
        smort.d.device_id = 0;
        Debug_printf(("\r\nReset Cleared"));
      break;
    case iwm_phases_t::enable:
      portDISABLE_INTERRUPTS();
      if (iwm_read_packet((uint8_t *)smort.packet_buffer))
      {
        portENABLE_INTERRUPTS(); 
        break; //error timeout, break and loop again  // todo: for now ack going low is in iwm_read_packet
      }
      portENABLE_INTERRUPTS();
      // now ACK is enabled and cleared low, it is reset in the handlers
#ifdef DEBUG
      Debug_printf("\r\n");
      for (int i = 0; i < 28; i++)
      {
        if (smort.packet_buffer[i])
          Debug_printf("%02x ", smort.packet_buffer[i]);
        else
          break;
      }
      Debug_printf("\r\n");
#endif

      switch (smort.packet_buffer[14])
      {
      case 0x81: // read block
        Debug_printf("\r\nhandling read block command");
        smort.handle_readblock();
        break;
      case 0x85: //is an init cmd
        Debug_printf("\r\nhandling init command");
        handle_init();
        break;
      } // switch (cmd)
    }   // switch (phasestate)
  }     // while(true)
}

void iwmDevice::handle_readblock()
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
  Debug_printf("\r\nRead block %02x\r\n",block_num);

  if (fseek(d.sdf, (block_num * 512), SEEK_SET))
  {
    Debug_printf("\r\nRead seek err! block #%02x", block_num);
    if (d.sdf != nullptr)
    {
      Debug_printf("\r\nPartition file is open!");
    }
    else
    {
      Debug_printf("\r\nPartition file is closed!");
    }
    return;
  }

  sdstato = fread((unsigned char *)packet_buffer, 1, 512, d.sdf); //Reading block from SD Card
  if (sdstato != 512)
  {
    Debug_printf("\r\nFile Read err: %d bytes", sdstato);
    return;
  }
  encode_data_packet(source);
  Debug_printf("\r\nsending block packet ...");
  //iwm_ack_set(); // todo: probably put ack req handshake inside of send and receive packet()
  // portDISABLE_INTERRUPTS();
  // iwm_rddata_enable();
  IWM.iwm_send_packet((unsigned char *)packet_buffer); // this returns timeout errors but that's not handled here
  // iwm_rddata_disable();
  // portENABLE_INTERRUPTS(); // takes 7 us to execute

  //Debug_printf(status);
  //print_packet ((unsigned char*) packet_buffer,packet_length());
  //print_packet ((unsigned char*) sector_buffer,15);
}

void iwmBus::handle_init()
{
  uint8_t source;

  iwm_rddata_enable(); // todo: instead, do we enable rddata and ack when phasestate==enable?
  iwm_rddata_clr(); // todo: enable is done below so maybe not needed here?

  source = smort.packet_buffer[6];
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
    smort.d.device_id = source; //remember source id for partition
    uint8_t status = 0xff; //yes, so status=non zero

  smort.encode_init_reply_packet(source, status);
  //print_packet ((uint8_t*) packet_buffer,packet_length());
  Debug_printf("\r\nSending INIT Response Packet...");
  // portDISABLE_INTERRUPTS();
  // iwm_rddata_enable();
  iwm_send_packet((uint8_t *)smort.packet_buffer); // timeout error return is not handled here (yet?)
  // ack-req handshake is inside of sendpacket
  // iwm_rddata_disable();
  // portENABLE_INTERRUPTS(); // takes 7 us to execute

  //print_packet ((uint8_t*) packet_buffer,packet_length());

  Debug_printf(("\r\nDrive: %02x"),smort.d.device_id);
}

iwmBus IWM; // global smartport bus variable

#endif /* BUILD_APPLE */
