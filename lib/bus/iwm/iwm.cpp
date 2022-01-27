#ifdef BUILD_APPLE
#include "iwm.h"
#include "fnSystem.h"
#include "fnFsTNFS.h"
#include <string.h>
#include "driver/timer.h" // contains the hardware timer register data structure
#include "../../include/debug.h"
#include "utils.h"

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
#define SP_EXTRA    32      //  CLKOUT

// hardware timer parameters for bit-banging I/O
#define TIMER_DIVIDER         (2)  //  Hardware timer clock divider
#define TIMER_SCALE           (TIMER_BASE_CLK / TIMER_DIVIDER)  // convert counter value to seconds
#define TIMER_USEC_FACTOR     (TIMER_SCALE / 1000000)
#define TIMER_100NS_FACTOR    (TIMER_SCALE / 10000000)
#define TIMER_ADJUST          0 // substract this value to adjust for overhead

//#define IWM_BIT_CELL          4 // microseconds - 2 us for fast mode
//#define IWM_TX_PW             1 // microseconds - 1/2 us for fast mode

#undef VERBOSE_IWM


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
  iwm_timer.tn = iwm_timer.t0 + s * TIMER_100NS_FACTOR - TIMER_ADJUST;
}

void iwmBus::iwm_timer_alarm_snooze(int s)
{
  iwm_timer.tn += s * TIMER_100NS_FACTOR - TIMER_ADJUST; // 3 microseconds

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

void iwmBus::iwm_extra_set()
{
  GPIO.out1_w1ts.data = ((uint32_t)0x01 << (SP_EXTRA - 32));
}

void iwmBus::iwm_extra_clr()
{
  GPIO.out1_w1tc.data = ((uint32_t)0x01 << (SP_EXTRA - 32));  
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
  portDISABLE_INTERRUPTS(); // probably put the critical section inside the read packet function?

  iwm_timer_reset();
  // cache all the functions
  // iwm_timer_latch();        // latch highspeed timer value
  // iwm_timer_read();
  // iwm_timer_alarm_set(1);
  // iwm_timer_wait();
  // iwm_timer_alarm_snooze(1);
  // iwm_timer_wait();
  
  // signal the logic analyzer
  iwm_extra_clr();
  iwm_extra_set();
  iwm_extra_clr();
  iwm_extra_set();
  iwm_extra_clr();

    // setup a timeout counter to wait for REQ response
  iwm_timer_latch();        // latch highspeed timer value
  iwm_timer_read();      //  grab timer low word
  iwm_timer_alarm_set(1000); // todo: logic analyzer says 40 usec

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
      portENABLE_INTERRUPTS();
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
      portENABLE_INTERRUPTS();
      return 1;
    }
  };

  // I think there's an extra usec because logic analyzer says 9 us from REQ to first WR edge
  // there are two 0's (each 4 usec) and then the 1 (edge) at the start of the first sync byte 
  // iwm_timer_alarm_set(9); 
  // iwm_timer_wait();
  //iwm_timer_latch();   // latch highspeed timer value
  //iwm_timer_read(); // grab timer low word
  //iwm_extra_set(); // signal to LA we're entering the read packet loop
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
      iwm_timer_alarm_set(39); // 4 usec
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
    a[idx++] = rxbyte; // havebyte: st   x+,r23                         ;17                    ;2   save byte in buffer
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
        have_data = false;
        break;
      }
    } while (iwm_wrdata_val() == prev_level);
    numbits = 8;       // ;1   8bits to read
  } while (have_data); //(have_data); // while have_data
  //           rjmp nxtbyte                        ;46  ;47               ;2   get next byte

  // endpkt:   clr  r23
  a[idx] = 0; //           st   x+,r23               ;save zero byte in buffer to mark end

  //todo: try something here
  // so I shuold just return a 1 without an ACK
if (idx<17) // invalid packet! but for now return OK and don't ACK
{
  portENABLE_INTERRUPTS();
  return 1;
}

  portENABLE_INTERRUPTS();
  return 0;
}

int iwmBus::iwm_read_packet_timeout(int attempts, uint8_t *a)
{
  iwm_ack_set();
  for (int i=0; i < attempts; i++)
  {
    if (!iwm_read_packet(a))
    {
      iwm_ack_clr(); // todo - make ack functions public so devices can call them?
      return 0;
    }
  }
  iwm_ack_disable();
  return 1;
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

  // should this be ack disable or ack set?
  iwm_ack_set(); // ack is already enabled by the response to the command read


#ifndef TESTTX
  // 1:        sbic _SFR_IO_ADDR(PIND),2   ;wait for req line to go high
  // setup a timeout counter to wait for REQ response
  iwm_timer_latch();        // latch highspeed timer value
  iwm_timer_read();      //  grab timer low word
  iwm_timer_alarm_set(1000); // 0.1 millisecond

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
#else
  iwm_timer_latch();
  iwm_timer_read();
#endif // TESTTX

  // CRITICAL TO HAVE 1 US BETWEEN req AND FIRST PULSE to put the falling edge 2 us after REQ
  // at one point i had to do a trim alarm setting, but now can do standard call - i think the
  // timing behavior changed because I call alarm_set and alarm_snooze up at the top
  // because i think i'm caching the function calls
  //iwm_timer.tn = iwm_timer.t0 + (1 * TIMER_USEC_FACTOR / 2) - TIMER_ADJUST; // NEED JUST 1/2 USEC
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
 // iwm_ack_disable();       // need to release the bus
  portENABLE_INTERRUPTS(); // takes 7 us to execute
  return 0;

}

void iwmBus::setup(void)
{
  Debug_printf(("\r\nIWM FujiNet based on SmartportSD v1.15\r\n"));

  fnSystem.set_pin_mode(SP_ACK, gpio_mode_t::GPIO_MODE_OUTPUT);
  fnSystem.digital_write(SP_ACK, DIGI_LOW); // set up ACK ahead of time to go LOW when enabled
  fnSystem.digital_write(SP_ACK, DIGI_HIGH); // ID ACK for Logic Analyzer
  fnSystem.digital_write(SP_ACK, DIGI_LOW); // set up ACK ahead of time to go LOW when enabled
  //set ack (hv) to input to avoid clashing with other devices when sp bus is not enabled
  fnSystem.set_pin_mode(SP_ACK, gpio_mode_t::GPIO_MODE_INPUT); //, SystemManager::PULL_UP ); // todo: test this - i think this makes sense to keep the ACK line high while not in use
  
  fnSystem.set_pin_mode(SP_PHI0, gpio_mode_t::GPIO_MODE_INPUT); // REQ line
  fnSystem.set_pin_mode(SP_PHI1, gpio_mode_t::GPIO_MODE_INPUT);
  fnSystem.set_pin_mode(SP_PHI2, gpio_mode_t::GPIO_MODE_INPUT);
  fnSystem.set_pin_mode(SP_PHI3, gpio_mode_t::GPIO_MODE_INPUT);

  fnSystem.set_pin_mode(SP_WRDATA, gpio_mode_t::GPIO_MODE_INPUT);

  fnSystem.set_pin_mode(SP_RDDATA, gpio_mode_t::GPIO_MODE_OUTPUT);
  fnSystem.digital_write(SP_RDDATA, DIGI_LOW);
  fnSystem.digital_write(SP_RDDATA, DIGI_HIGH); // ID RD for logic analyzer
  fnSystem.digital_write(SP_RDDATA, DIGI_LOW);
  // leave rd as input, pd6
  fnSystem.set_pin_mode(SP_RDDATA, gpio_mode_t::GPIO_MODE_INPUT); //, SystemManager::PULL_DOWN );  ot maybe pull up, too?

  fnSystem.set_pin_mode(SP_EXTRA, gpio_mode_t::GPIO_MODE_OUTPUT);
  fnSystem.digital_write(SP_EXTRA, DIGI_LOW);
  fnSystem.digital_write(SP_EXTRA, DIGI_HIGH); // ID extra for logic analyzer
  fnSystem.digital_write(SP_EXTRA, DIGI_LOW);

  Debug_printf("\r\nIWM GPIO configured");

  timer_config();
  Debug_printf("\r\nIWM timer started");
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

//todo: this only replies a $21 error
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
// Function: verify_cmdpkt_checksum
// Parameters: none
// Returns: 0 = ok, 1 = error
//
// Description: verify the checksum for command packets
//
// &&&&&&&&not used at the moment, no error checking for checksum for cmd packet
//*****************************************************************************
bool iwmDevice::verify_cmdpkt_checksum(void)
{
  int length;
  uint8_t evenbits, oddbits, bit7, bit0to6, grpbyte;
  uint8_t calc_checksum = 0; //initial value is 0
  uint8_t pkt_checksum;

  length = packet_length();
  Debug_printf("\r\npacket length = %d", length);
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
  pkt_checksum = oddbits & evenbits; // oddbits | evenbits;
  // every other bit is ==1 in checksum, so need to AND to get data back

  //  Debug_print(("Pkt Chksum Byte:\r\n"));
  //  Debug_print(pkt_checksum,DEC);
  //  Debug_print(("Calc Chksum Byte:\r\n"));
  //  Debug_print(calc_checksum,DEC);
  Debug_printf("\r\nChecksum - pkt,calc: %02x %02x", pkt_checksum, calc_checksum);
  // if ( pkt_checksum == calc_checksum )
  //   return false;
  // else
  //   return true;
  return (pkt_checksum != calc_checksum);  
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

void iwmDevice::print_packet()
{
  Debug_printf("\r\n");
  for (int i = 0; i < 28; i++)
  {
    if (packet_buffer[i])
      Debug_printf("%02x ", packet_buffer[i]);
    else
      break;
  }
  Debug_printf("\r\n");
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


//*****************************************************************************
// Function: main loop
// //*****************************************************************************
void iwmBus::service(iwmDevice* smort) 
{
  // iwm_rddata_disable(); // todo - figure out sequence of setting IWM pins and where this should go
  // iwm_rddata_clr();
  // while (true)
  // {
    iwm_ack_disable();
    iwm_ack_clr(); // prep for the next read packet
     
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
      while (iwm_phases() == iwm_phases_t::reset)
        ; // todo: should there be a timeout feature?
        // hard coding 1 partition - will use disk class instances instead
        smort->_devnum = 0;
        Debug_printf(("\r\nReset Cleared"));
      break;
    case iwm_phases_t::enable:
    // expect a command packet
    // todo: make a command packet structure type, create a temp one and pass it to iwm_read_packet
    // so we don't have to hijack some device's packet_buffer
    // also, do we have a universal packet buffer, or does each device have its own?
     
      if (iwm_read_packet((uint8_t *)smort->packet_buffer))
      {
        break; //error timeout, break and loop again 
      }
      // todo: should only ack if it's our device and if checksum is OK
      // if (smort->packet_buffer[6] != smort->id())
      // {
      //   Debug_printf ("\r\nnot our packet!");
      //   break;
      // }
      if (smort->verify_cmdpkt_checksum())
      {
        // Debug_printf("\r\nBAD CHECKSUM!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
        // break;
      }
      iwm_ack_clr();
      iwm_ack_enable(); // have to act really fast
      // now ACK is enabled and cleared low, it is reset in the handlers


      /***
       * todo notes:
       * once we make an actual device, like disk.cpp, then
       * we need to hand off control to the device to service
       * the command packet. I think the algorithm is something like:
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

      //Todo: should not ACK unless we know this is our Command
      // should move this block to the main Bus service section
      // and only ACK when we know it's our device, then pass
      // control to that device
      //iwm_ack_clr();

      // do we need to wait for REQ to go low here, or should we just pass control
      // and set up for the command? Once we see REQ go low,
      // and we're ready to respond, the we disable ACK
      // setup a timeout counter to wait for REQ response
      iwm_timer_latch();        // latch highspeed timer value
      iwm_timer_read();         //  grab timer low word
      iwm_timer_alarm_set(50000); // todo: figure out
      while (iwm_req_val())
      {
        iwm_timer_latch();               // latch highspeed timer value
        iwm_timer_read();                // grab timer low word
        if (iwm_timer.t0 > iwm_timer.tn) // test for timeout
        {                                // timeout!
#ifdef VERBOSE_IWM
          // timeout
          Debug_print("t");
#endif
          break;
        }
      }

      if (smort->packet_buffer[14] == 0x85)
      {
        #ifdef DEBUG
       smort->print_packet();
#endif
        Debug_printf("\r\nhandling init command");
        handle_init(smort);
      }
      else
     {
#ifdef DEBUG
       smort->print_packet();
#endif
        smort->process();
     } 
    }   // switch (phasestate)
  // }     // while(true)
}

void iwmBus::handle_init(iwmDevice* smort)
{
  uint8_t source;

  iwm_rddata_clr();
  iwm_rddata_enable(); 
  
  source = smort->packet_buffer[6];
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
    smort->_devnum = source; //remember source id for partition
    uint8_t status = 0xff; //yes, so status=non zero

  smort->encode_init_reply_packet(source, status);
  //print_packet ((uint8_t*) packet_buffer,packet_length());
  Debug_printf("\r\nSending INIT Response Packet...");
  iwm_send_packet((uint8_t *)smort->packet_buffer); // timeout error return is not handled here (yet?)

  //print_packet ((uint8_t*) packet_buffer,packet_length());

  Debug_printf(("\r\nDrive: %02x"),smort->id());
}

// Add device to SIO bus
void iwmBus::addDevice(iwmDevice *pDevice, iwm_internal_type_t deviceType)
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

  // todo: work out how to use addDevice during an INIT sequence
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
    case iwm_internal_type_t::GenericBlock:
      // no special device assignment needed
      break;
    case iwm_internal_type_t::GenericChar:
      // no special device assignment needed
      break;
    case iwm_internal_type_t::FujiNet:
      _fujiDev = (iwmFuji *)pDevice;
      break;
    case iwm_internal_type_t::Modem:
      _modemDev = (iwmModem *)pDevice;
      break;
    case iwm_internal_type_t::Network:
      // todo: work out how to assign different network devices - idea:
      // include a number in the DIB name, e.g., "NETWORK 1"
      // and extract that number from the DIB and use it as the index
      //_netDev[device_id - SIO_DEVICEID_FN_NETWORK] = (iwmNetwork *)pDevice;
      break;
    case iwm_internal_type_t::CPM:
    //   _cpmDev = (iwmCPM *)pDevice;
       break;
    case iwm_internal_type_t::Printer:
      _printerdev = (iwmPrinter *)pDevice;
      break;
    case iwm_internal_type_t::Voice:
    // not yet implemented: todo - take SAM and implement as a special block device. Also then available for disk rotate annunciation. 
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
    iwm_send_packet((uint8_t *)smort->packet_buffer); // timeout error return is not handled here (yet?)
    fnSystem.delay(1000);
  }
}
#endif

iwmBus IWM; // global smartport bus variable

#endif /* BUILD_APPLE */
