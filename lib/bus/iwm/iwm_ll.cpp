#ifdef BUILD_APPLE

#include <string.h>

#include "esp_rom_gpio.h"

#include "iwm_ll.h"
#include "iwm.h"
#include "../device/iwm/disk2.h"
#include "../device/iwm/fuji.h"
#include "fnSystem.h"
#include "fnHardwareTimer.h"
#include "../../include/debug.h"

#define MHZ (1000*1000)

volatile uint8_t _phases = 0;
volatile bool sp_command_mode = false;
volatile int isrctr = 0;

void IRAM_ATTR phi_isr_handler(void *arg)
{
  // handle SP Command Packet or Disk ][ track changes
  // maintain the diskii process:
  // update the head position based on phases
  // put the right track in the SPI buffer

  _phases = (uint8_t)(GPIO.in1.val & (uint32_t)0b1111);
  
  if (!sp_command_mode && (_phases == 0b1011))
  {
    smartport.iwm_read_packet_spi(IWM.command_packet.data, COMMAND_PACKET_LEN);
    if (IWM.command_packet.command == 0x85)
    {
      smartport.iwm_ack_clr();
      sp_command_mode = true;
    }
    else
    {
      for (auto devicep : IWM._daisyChain)
      {
        if (IWM.command_packet.dest == devicep->id())
        {
          smartport.iwm_ack_clr();
          sp_command_mode = true;
        }
      }
    }
    smartport.spi_end();
  }
  else if (diskii_xface.iwm_enable_states() & 0b11)
  {
    
    if (theFuji._fnDisk2s[diskii_xface.iwm_enable_states() - 1].move_head())
    {
      isrctr++;
      theFuji._fnDisk2s[diskii_xface.iwm_enable_states() - 1].change_track(isrctr);
    }
  }
}

inline void iwm_sp_ll::iwm_extra_set()
{
#ifdef EXTRA
  GPIO.out_w1ts = ((uint32_t)1 << SP_EXTRA);
#endif
}

inline void iwm_sp_ll::iwm_extra_clr()
{
#ifdef EXTRA
  GPIO.out_w1tc = ((uint32_t)1 << SP_EXTRA);
#endif
}

void IRAM_ATTR iwm_sp_ll::encode_spi_packet()
{
  // clear out spi buffer
  memset(spi_buffer, 0, SPI_SP_LEN);
  // loop through "l" bytes of the buffer "packet_buffer"
  uint16_t i=0,j=0;
  while(packet_buffer[i])
  {
    // Debug_printf("\nByte %02X: ",packet_buffer[i]);
    // for each byte, loop through 4 x 2-bit pairs
    uint8_t mask = 0x80;
    for (int k = 0; k < 4; k++)
    {
      if (packet_buffer[i] & mask)
      {
        spi_buffer[j] |= 0x40;
      }
      mask >>= 1;
      if (packet_buffer[i] & mask)
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


int IRAM_ATTR iwm_sp_ll::iwm_send_packet_spi()
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

  portDISABLE_INTERRUPTS();

  encode_spi_packet();

  // send data stream using SPI
  esp_err_t ret;
  spi_transaction_t trans;
  memset(&trans, 0, sizeof(spi_transaction_t));
  trans.tx_buffer = spi_buffer; // finally send the line data
  trans.length = spi_len * 8;   // Data length, in bits
  trans.flags = 0;              // undo SPI_TRANS_USE_TXDATA flag

  iwm_ack_set(); // go hi-z - signal ready to send data

  // 1:        sbic _SFR_IO_ADDR(PIND),2   ;wait for req line to go high
  if (req_wait_for_rising_timeout(300000))
    {
      // timeout!
      Debug_printf("\nSendPacket timeout waiting for REQ");
      portENABLE_INTERRUPTS(); // takes 7 us to execute
      return 1;
    }

  // send the data
  iwm_rddata_clr(); // enable the tri-state buffer
  ret = spi_device_polling_transmit(spi, &trans);
  iwm_rddata_set(); // make rddata hi-z
  iwm_ack_clr();
  assert(ret == ESP_OK);

  // wait for REQ to go low
  if (req_wait_for_falling_timeout(15000))
  {
    Debug_println("Send REQ timeout");
    // iwm_ack_disable();       // need to release the bus
    portENABLE_INTERRUPTS(); // takes 7 us to execute
    return 1;
  }
  portENABLE_INTERRUPTS();
  return 0;
}

bool IRAM_ATTR iwm_sp_ll::spirx_get_next_sample()
{
  if (spirx_bit_ctr > 7)
  {
    spirx_bit_ctr = 0;
    spirx_byte_ctr++;
  }
  return (((spi_buffer[spirx_byte_ctr] << spirx_bit_ctr++) & 0x80) == 0x80);
}


int IRAM_ATTR iwm_sp_ll::iwm_read_packet_spi(int n)
{
  return iwm_read_packet_spi(packet_buffer, n);
}

int IRAM_ATTR iwm_sp_ll::iwm_read_packet_spi(uint8_t* buffer, int n)
{ // read data stream using SPI
  fnTimer.reset();

  // signal the logic analyzer
  iwm_extra_set();

  /* calculations for determining array sizes
  int numsamples = pulsewidth * (n + 2) * 8;
  command packet on DIY SP is 872 us long
  2051282 * 872e-6 = 1798 samples = 224 byes
  nominal command length is 27 bytes * 8 * 8 = 1728 samples
  1798/1728 = 1.04

  command packet on YellowStone (YS) is 919 us
  2.052 * 919 = 1886 samples
  1886 / 1728 = 1.0914    --    this one says we need 10% extra array length

  write block packet on DIY 29 is 20.1 ms long
  2052kHz * 20.1ms =  41245 samples = 5156 bytes
  nominal 604 bytes for block packet = 38656 samples
  41245/38656 = 1.067

  write block packet on YS is 18.95 ms so should fit within DIY
  IIgs take 18.88 ms for a write block
  */

  spi_len = n * pulsewidth * 11 / 10 ; //add 10% for overhead to accomodate YS command packet
  
  memset(spi_buffer, 0xff, SPI_SP_LEN);

  memset(&rxtrans, 0, sizeof(spi_transaction_t));
  rxtrans.flags = 0;
  rxtrans.length = 0; //spi_len * 8;   // Data length, in bits
  rxtrans.rxlength = spi_len * 8;   // Data length, in bits
  rxtrans.tx_buffer = nullptr;
  rxtrans.rx_buffer = spi_buffer; // finally send the line data

  // setup a timeout counter to wait for REQ response
  if (req_wait_for_rising_timeout(1000))
  { // timeout!
#ifdef VERBOSE_IWM
    // timeout
    Debug_print("t");
#endif
    iwm_extra_clr();
    return 1;
  }

  esp_err_t ret = spi_device_polling_start(spirx, &rxtrans, portMAX_DELAY);
  assert(ret == ESP_OK);
  iwm_extra_clr();

  // decode the packet here
  spirx_byte_ctr = 0; // initialize the SPI buffer sampler
  spirx_bit_ctr = 0;

  bool have_data = true;
  bool synced = false;
  int idx = 0;             // index into *buffer
  bool bit = false; // = 0;        // logical bit value

  uint8_t rxbyte = 0;      // r23 received byte being built bit by bit
  int numbits = 8;             // number of bits left to read into the rxbyte

  bool prev_level = true;
  bool current_level; // level is signal value (fast time), bits are decoded data values (slow time)

  //for tracking the number of samples
  int bit_position;
  int last_bit_pos = 0;
  int samples;
  bool start_packet = true;

  fnTimer.latch();               // latch highspeed timer value
  fnTimer.read();                //  grab timer low word
  // sync byte is 10 * 8 * (10*1000*1000/2051282) = 39.0 us long
  fnTimer.alarm_set(390 * 3 / 2);          // wait for first 1.5 sync bytes

  do // have_data
  {
    iwm_extra_set(); // signal to LA we're in the nested loop

    bit_position = spirx_byte_ctr * 8 + spirx_bit_ctr; // current bit positon
    samples = bit_position - last_bit_pos; // difference since last time
    last_bit_pos = bit_position;

    fnTimer.wait();

    if (start_packet) // is at the start, assume sync byte, 39.2 us for 10-bit sync bytes
    {
      fnTimer.alarm_set( 390 ); // latch and read already done in fnTimer.wait()
      start_packet = false;
    }
    else
    {
      fnTimer.alarm_snooze( (samples * 10 * 1000 * 1000) / f_spirx); // samples * 10 /2 ); // snooze the timer based on the previous number of samples
    }

    iwm_extra_clr();
    do
    {
      bit = false; // assume no edge in this next bit
#ifdef VERBOSE_IWM
      Debug_printf("\npulsewidth = %d, halfwidth = %d",pulsewidth,halfwidth);
      Debug_printf("\nspibyte spibit intctr sampval preval rxbit rxbyte");
#endif
      int i = 0;
      while (i < pulsewidth)
      {
        current_level = spirx_get_next_sample();
        current_level ? iwm_extra_clr() : iwm_extra_set();
#ifdef VERBOSE_IWM
        Debug_printf("\n%7d %6d %6d %7d %6d %5d %6d", spirx_byte_ctr, spirx_bit_ctr, i, current_level, prev_level, bit, rxbyte);
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
      }
      rxbyte <<= 1;
      rxbyte |= bit;
      iwm_extra_set(); // signal to LA we're done with this bit
    } while (--numbits > 0);
    if ((rxbyte == 0xc3) && (!synced))
    {
      synced = true;
      idx = 5;
    }
    buffer[idx++] = rxbyte;
    // wait for leading edge of next byte or timeout for end of packet
    int timeout_ctr = (f_spirx * 19) / (1000 * 1000); //((f_nyquist * f_over) * 18) / (1000 * 1000);
#ifdef VERBOSE_IWM
    Debug_printf("%02x ", rxbyte);
#endif
    // now wait for leading edge of next byte
    iwm_extra_clr();
    if (idx > n)
      have_data = false;
    else
      do
      {
        if (--timeout_ctr < 1)
        { // end of packet
          have_data = false;
          break;
        }
      } while (spirx_get_next_sample() == prev_level);
    numbits = 8;
  } while (have_data); // while have_data
  return 0;
}

void IRAM_ATTR iwm_sp_ll::spi_end() { spi_device_polling_end(spirx, portMAX_DELAY); };

bool iwm_sp_ll::req_wait_for_falling_timeout(int t)
{
  fnTimer.reset();
  fnTimer.latch();      // latch highspeed timer value
  fnTimer.read();       //  grab timer low word
  fnTimer.alarm_set(t); // t in 100ns units
  while (iwm_req_val())
  {
    fnTimer.latch();       // latch highspeed timer value
    fnTimer.read();        // grab timer low word
    if (fnTimer.timeout()) // test for timeout
    {                      // timeout!
      return true;
    }
  }
  return false;
}

bool iwm_sp_ll::req_wait_for_rising_timeout(int t)
{
  fnTimer.reset();
  fnTimer.latch();      // latch highspeed timer value
  fnTimer.read();       //  grab timer low word
  fnTimer.alarm_set(t); // t in 100ns units
  while (!iwm_req_val())
  {
    fnTimer.latch();       // latch highspeed timer value
    fnTimer.read();        // grab timer low word
    if (fnTimer.timeout()) // test for timeout
    {                      // timeout!
      return true;
    }
  }
  return false;
}

void iwm_sp_ll::setup_spi()
{
  int spirx_mosi_pin = -1;
  esp_err_t ret; // used for calling SPI library functions below

  spi_buffer = (uint8_t *)heap_caps_malloc(SPI_SP_LEN, MALLOC_CAP_DMA);

  if(fnSystem.check_spifix())
    spirx_mosi_pin = SP_SPI_FIX_PIN;

  // SPI for receiving packets - sprirx
  bus_cfg = {
    .mosi_io_num = spirx_mosi_pin,
    .miso_io_num = SP_WRDATA,
    .sclk_io_num = -1,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
    .max_transfer_sz = TRACK_LEN, // SPI_II_LEN,
    .flags = SPICOMMON_BUSFLAG_MASTER,
    .intr_flags = 0
  };

  ret = spi_bus_initialize(VSPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
  assert(ret == ESP_OK);

  spi_device_interface_config_t rxcfg = {
    .mode = 0,                      // SPI mode 0
    .duty_cycle_pos = 0,            ///< Duty cycle of positive clock, in 1/256th increments (128 = 50%/50% duty). Setting this to 0 (=not setting it) is equivalent to setting this to 128.
    .cs_ena_pretrans = 0,
    .cs_ena_posttrans = 0,
    .clock_speed_hz = f_spirx,      // f_over * f_nyquist, // Clock at 500 kHz x oversampling factor
    .input_delay_ns = 0,
    .spics_io_num = -1,             // CS pin
    .flags = SPI_DEVICE_HALFDUPLEX,
    .queue_size = 1                 // We want to be able to queue 7 transactions at a time
  };

  ret = spi_bus_add_device(VSPI_HOST, &rxcfg, &spirx);
  assert(ret == ESP_OK);


  spi_device_interface_config_t devcfg = {
    .mode = 0,                   // SPI mode 0
    .duty_cycle_pos = 0,         ///< Duty cycle of positive clock, in 1/256th increments (128 = 50%/50% duty). Setting this to 0 (=not setting it) is equivalent to setting this to 128.
    .cs_ena_pretrans = 0,        ///< Amount of SPI bit-cycles the cs should be activated before the transmission (0-16). This only works on half-duplex transactions.
    .cs_ena_posttrans = 0,       ///< Amount of SPI bit-cycles the cs should stay active after the transmission (0-16)
    .clock_speed_hz = 1 * MHZ, // Clock out at 1 MHz
    .input_delay_ns = 0,
    .spics_io_num = -1,                // CS pin
    .queue_size = 2                    // We want to be able to queue 7 transactions at a time
  };

  if(fnSystem.check_spifix())
  {
    // use different SPI than SDCARD
    ret = spi_bus_add_device(VSPI_HOST, &devcfg, &spi);
    assert(ret == ESP_OK);
    // connect peripheral to GPIO because RMT screwed it up
    esp_rom_gpio_connect_out_signal(PIN_SD_HOST_MOSI, spi_periph_signal[VSPI_HOST].spid_out, false, false);
  }
  else
  {
    // use same SPI as SDCARD
    ret = spi_bus_add_device(HSPI_HOST, &devcfg, &spi);
    assert(ret == ESP_OK);
    // connect peripheral to GPIO because RMT screwed it up
    esp_rom_gpio_connect_out_signal(PIN_SD_HOST_MOSI, spi_periph_signal[HSPI_HOST].spid_out, false, false);
  }

  if (smartport.spiMutex == NULL)
  {
    smartport.spiMutex = xSemaphoreCreateMutex();
  }

}

void iwm_sp_ll::setup_gpio()
{
  fnSystem.set_pin_mode(SP_ACK, gpio_mode_t::GPIO_MODE_OUTPUT);
  fnSystem.digital_write(SP_ACK, DIGI_LOW); // set up ACK ahead of time to go LOW when enabled
  //set ack to input to avoid clashing with other devices when sp bus is not enabled

  fnSystem.set_pin_mode(SP_ACK, gpio_mode_t::GPIO_MODE_INPUT, SystemManager::pull_updown_t::PULL_UP);
  
  fnSystem.set_pin_mode(SP_PHI0, gpio_mode_t::GPIO_MODE_INPUT, SystemManager::pull_updown_t::PULL_NONE, gpio_int_type_t::GPIO_INTR_ANYEDGE); // REQ line
  fnSystem.set_pin_mode(SP_PHI1, gpio_mode_t::GPIO_MODE_INPUT, SystemManager::pull_updown_t::PULL_NONE, gpio_int_type_t::GPIO_INTR_ANYEDGE);
  fnSystem.set_pin_mode(SP_PHI2, gpio_mode_t::GPIO_MODE_INPUT, SystemManager::pull_updown_t::PULL_NONE, gpio_int_type_t::GPIO_INTR_ANYEDGE);
  fnSystem.set_pin_mode(SP_PHI3, gpio_mode_t::GPIO_MODE_INPUT, SystemManager::pull_updown_t::PULL_NONE, gpio_int_type_t::GPIO_INTR_ANYEDGE);

  // fnSystem.set_pin_mode(SP_WRDATA, gpio_mode_t::GPIO_MODE_INPUT); // not needed cause set in SPI?

  fnSystem.set_pin_mode(SP_WREQ, gpio_mode_t::GPIO_MODE_INPUT);
  fnSystem.set_pin_mode(SP_DRIVE1, gpio_mode_t::GPIO_MODE_INPUT);
  fnSystem.set_pin_mode(SP_DRIVE2, gpio_mode_t::GPIO_MODE_INPUT, SystemManager::pull_updown_t::PULL_UP);
  fnSystem.set_pin_mode(SP_EN35, gpio_mode_t::GPIO_MODE_INPUT);
  fnSystem.set_pin_mode(SP_HDSEL, gpio_mode_t::GPIO_MODE_INPUT);
  fnSystem.set_pin_mode(SP_RDDATA, gpio_mode_t::GPIO_MODE_OUTPUT); // tri-state buffer control
  fnSystem.digital_write(SP_RDDATA, DIGI_HIGH); // Turn tristate buffer off by default

#ifdef EXTRA
  fnSystem.set_pin_mode(SP_EXTRA, gpio_mode_t::GPIO_MODE_OUTPUT);
  fnSystem.digital_write(SP_EXTRA, DIGI_LOW);
  fnSystem.digital_write(SP_EXTRA, DIGI_HIGH); // ID extra for logic analyzer
  fnSystem.digital_write(SP_EXTRA, DIGI_LOW);
  fnSystem.digital_write(SP_EXTRA, DIGI_HIGH); // ID extra for logic analyzer
  fnSystem.digital_write(SP_EXTRA, DIGI_LOW);
  Debug_printf("\nEXTRA signaling line configured");
#endif


  // attach the interrupt service routine
  gpio_isr_handler_add((gpio_num_t)SP_PHI0, phi_isr_handler, NULL);
  gpio_isr_handler_add((gpio_num_t)SP_PHI1, phi_isr_handler, NULL);
  gpio_isr_handler_add((gpio_num_t)SP_PHI2, phi_isr_handler, NULL);
  gpio_isr_handler_add((gpio_num_t)SP_PHI3, phi_isr_handler, NULL);
}

void iwm_sp_ll::encode_packet(uint8_t source, iwm_packet_type_t packet_type, uint8_t status, const uint8_t* data, uint16_t num)
{
  // generic version would need:
  // source id
  // packet type
  // status code
  // pointer to the data
  // number of bytes to encode

  uint8_t checksum = 0;
  int numgrps = 0;
  int numodds = 0;

  if ((data != nullptr) && (num != 0))
  {
  int grpbyte, grpcount;
  uint8_t grpmsb;
  uint8_t group_buffer[7];
                                    // Calculate checksum of sector bytes before we destroy them
    for (int count = 0; count < num; count++) // xor all the data bytes
      checksum = checksum ^ data[count];

    // Start assembling the packet at the rear and work
    // your way to the front so we don't overwrite data
    // we haven't encoded yet

    // how many groups of 7?
    numgrps = num / 7;
    numodds = num % 7;

    // grps of 7
    for (grpcount = numgrps - 1; grpcount >= 0; grpcount--) // 73
    {
      memcpy(group_buffer, data + numodds + (grpcount * 7), 7);
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
  }
  // oddbytes
  packet_buffer[14] = 0x80; // init the oddmsb
  for (int oddcnt = 0; oddcnt < numodds; oddcnt++)
  {
    packet_buffer[14] |= (data[oddcnt] & 0x80) >> (1 + oddcnt);
    packet_buffer[15 + oddcnt] = data[oddcnt] | 0x80;
  }

  // header
  packet_buffer[0] = 0xff; // sync bytes
  packet_buffer[1] = 0x3f;
  packet_buffer[2] = 0xcf;
  packet_buffer[3] = 0xf3;
  packet_buffer[4] = 0xfc;
  packet_buffer[5] = 0xff;

  packet_buffer[6] = 0xc3;  //PBEGIN - start byte
  packet_buffer[7] = 0x80;  //DEST - dest id - host
  packet_buffer[8] = source; //SRC - source id - us
  packet_buffer[9] = static_cast<uint8_t>(packet_type);  //TYPE - 0x82 = data
  packet_buffer[10] = 0x80; //AUX
  packet_buffer[11] = status | 0x80; //STAT
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

//*****************************************************************************
// Function: decode_data_packet
// Parameters: none
// Returns: error code, >0 = error encountered
//
// Description: decode 512 (arbitrary now) byte data packet for write block command from host
// decodes the data from the packet_buffer IN-PLACE!
//*****************************************************************************
int iwm_sp_ll::decode_data_packet(uint8_t* output_data)
{
  return decode_data_packet(packet_buffer, output_data);
}

int iwm_sp_ll::decode_data_packet(uint8_t* input_data, uint8_t* output_data)
{
  int grpbyte, grpcount;
  uint8_t numgrps, numodd;
  uint16_t numdata;
  uint8_t checksum = 0, bit0to6, bit7, oddbits, evenbits;
  uint8_t group_buffer[8];

  //Handle arbitrary length packets :)
  numodd = input_data[11] & 0x7f;
  numgrps = input_data[12] & 0x7f;
  numdata = numodd + numgrps * 7;
  Debug_printf("\nDecoding %d bytes",numdata);
  // if (numdata==512)
  // {
  //   // print out packets
  //   print_packet(input_data,BLOCK_PACKET_LEN);
  // }
  // First, checksum  packet header, because we're about to destroy it
  for (int count = 6; count < 13; count++) // now xor the packet header bytes
    checksum = checksum ^ input_data[count];

  int chkidx = 13 + numodd + (numodd != 0) + numgrps * 8;
  evenbits = input_data[chkidx] & 0x55;
  oddbits = (input_data[chkidx + 1] & 0x55) << 1;

  //add oddbyte(s), 1 in a 512 data packet
  for(int i = 0; i < numodd; i++){
    output_data[i] = ((input_data[13] << (i+1)) & 0x80) | (input_data[14+i] & 0x7f);
  }

  // 73 grps of 7 in a 512 byte packet
  int grpstart = 12 + numodd + (numodd != 0) + 1;
  for (grpcount = 0; grpcount < numgrps; grpcount++)
  {
    memcpy(group_buffer, input_data + grpstart + (grpcount * 8), 8);
    for (grpbyte = 0; grpbyte < 7; grpbyte++)
    {
      bit7 = (group_buffer[0] << (grpbyte + 1)) & 0x80;
      bit0to6 = (group_buffer[grpbyte + 1]) & 0x7f;
      output_data[numodd + (grpcount * 7) + grpbyte] = bit7 | bit0to6;
    }
  }

  //verify checksum
  for (int count = 0; count < numdata; count++) // xor all the output_data bytes
    checksum = checksum ^ output_data[count];

  Debug_printf("\ndecode data checksum calc %02x, packet %02x", checksum, (oddbits | evenbits));

  if (checksum != (oddbits | evenbits))
  {
    Debug_printf("\nCHECKSUM ERROR!");
    return -1; // error!
  }

  return numdata;
}

void iwm_sp_ll::set_output_to_spi()
{
 esp_rom_gpio_connect_out_signal(PIN_SD_HOST_MOSI, spi_periph_signal[HSPI_HOST].spid_out, false, false);
}

// =========================================================================================
// ========================== DISK II below ======== SP above ==============================
// =========================================================================================

// https://docs.espressif.com/projects/esp-idf/en/v3.3.5/api-reference/peripherals/rmt.html
#define RMT_TX_CHANNEL rmt_channel_t::RMT_CHANNEL_0
#define RMT_USEC (APB_CLK_FREQ / MHZ)

void iwm_diskii_ll::start()
{
 ESP_ERROR_CHECK(fnRMT.rmt_write_bitstream(RMT_TX_CHANNEL, track_buffer, track_numbits));
}

void iwm_diskii_ll::stop()
{
  fnRMT.rmt_tx_stop(RMT_TX_CHANNEL);
}

void iwm_diskii_ll::set_output_to_rmt()
{
  esp_rom_gpio_connect_out_signal(PIN_SD_HOST_MOSI, rmt_periph_signals.channels[0].tx_sig, false, false);
}

// KEEEEEEEEEEEEEEEEEEP FOR A WHILE UNTIL ALL TECHNIQUES LEARNED ARE USED OR NO LONGER NEEDED
// void IRAM_ATTR iwm_diskii_ll::rmttest(void)
// {
//  iwm_rddata_clr(); // enable the tri-state buffer
  
// size_t num_samples = 512*12;
// uint8_t* sample = (uint8_t*)heap_caps_malloc(num_samples, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
// if (sample == NULL)
//     Debug_println("could not allocate sample buffer");

// memset(sample, 0xff, num_samples);
// sample[1]=0;
// sample[num_samples-2]=0;
// sample[num_samples-1]=0b01111111;
// copy_track(sample, num_samples, num_samples * 8 - 2);
// Debug_printf("\nSending %d items", num_samples);//number_of_items);
//   //ESP_ERROR_CHECK(fnRMT.rmt_write_sample(RMT_TX_CHANNEL, sample, num_samples, false));
//   esp_rom_gpio_connect_out_signal(PIN_SD_HOST_MOSI, rmt_periph_signals.channels[0].tx_sig, false, false);
  // ESP_ERROR_CHECK(fnRMT.rmt_write_bitstream(RMT_TX_CHANNEL, track_buffer, track_numbits));
//   // fnSystem.delay(100);
//   // fnRMT.rmt_tx_stop(RMT_TX_CHANNEL);
//   // fnSystem.delay(50);
//   // ESP_ERROR_CHECK(fnRMT.rmt_write_sample(RMT_TX_CHANNEL, sample, num_samples, false));
// fnSystem.delay(2000);
// Debug_printf ("\nSample transmission complete");
// //gpio_set_direction(gpio_num_t(PIN_SD_HOST_MOSI),gpio_mode_t::GPIO_MODE_INPUT);
// Debug_printf("\r\ngpio set to input");
// GPIO.func_out_sel_cfg[PIN_SD_HOST_MOSI].oen_sel = 1; // let me control the enable register
// GPIO.enable_w1tc = ((uint32_t)0x01 << PIN_SD_HOST_MOSI);

//     // Ensure no other output signal is routed via GPIO matrix to this pin
// // REG_WRITE(GPIO_FUNC0_OUT_SEL_CFG_REG + (SP_WRDATA * 4),SIG_GPIO_OUT_IDX);

// // GPIO.func_out_sel_cfg[PIN_SD_HOST_MOSI].func_sel = .....;

// fnSystem.delay(1000);
// // gpio_matrix_out(gpio_num_t(SP_WRDATA), RMT_SIG_OUT0_IDX + RMT_TX_CHANNEL, 0, 0);
// //gpio_set_direction(gpio_num_t(PIN_SD_HOST_MOSI),gpio_mode_t::GPIO_MODE_INPUT_OUTPUT);
// //fnRMT.rmt_set_pin(RMT_TX_CHANNEL,RMT_MODE_TX, (gpio_num_t)SP_WRDATA );
// GPIO.enable_w1ts = ((uint32_t)0x01 << PIN_SD_HOST_MOSI);
// Debug_printf("\r\ngpio back to out");

// fnSystem.delay(1000);
// fnRMT.rmt_tx_stop(RMT_TX_CHANNEL);

// //GPIO.func_out_sel_cfg[PIN_SD_HOST_MOSI].func_sel = 0;
// esp_rom_gpio_connect_out_signal(PIN_SD_HOST_MOSI, spi_periph_signal[HSPI_HOST].spid_out, false, false);

// Debug_printf("\r\nconnect to SPI");

// }

//Convert uint8_t type of data to rmt format data.
void IRAM_ATTR encode_rmt_stream(const void* src, rmt_item32_t* dest, size_t src_size, 
                         size_t wanted_num, size_t* translated_size, size_t* item_num)
{
    if(src == NULL || dest == NULL) {
        *translated_size = 0;
        *item_num = 0;
        return;
    }
    // *src is equal to *track_buffer
    // src_size is equal to numbits
    // translated_size is not used
    // item_num will equal wanted_num at end

    // call diskii_xface.nextbit() to get the next bit out of the track buffer

    const rmt_item32_t bit0 = {{{ 3 * RMT_USEC, 0, RMT_USEC, 0 }}}; //Logical 0
    const rmt_item32_t bit1 = {{{ 3 * RMT_USEC, 0, RMT_USEC, 1 }}}; //Logical 1
    size_t size = 0;
    size_t num = 0;
    uint8_t *psrc = (uint8_t *)src;
    rmt_item32_t* pdest = dest;
    while (size < src_size && num < wanted_num)
    {
      for (int i = 0; i < 8; i++)
      {
        if (*psrc & (0x1 << i))
        {
          pdest->val = bit1.val;
        }
        else
        {
          pdest->val = bit0.val;
        }
        num++;
        pdest++;
      }
      size++;
      psrc++;
    }
    *translated_size = size;
    *item_num = num;
}

//Convert track data to rmt format data.
void IRAM_ATTR encode_rmt_bitstream(const void* src, rmt_item32_t* dest, size_t src_size, 
                         size_t wanted_num, size_t* translated_size, size_t* item_num)
{
    // *src is equal to *track_buffer
    // src_size is equal to numbits
    // translated_size is not used
    // item_num will equal wanted_num at end

    if (src == NULL || dest == NULL)
    {
      *translated_size = 0;
      *item_num = 0;
      return;
    }

    const rmt_item32_t bit0 = {{{ 3 * RMT_USEC, 0, RMT_USEC, 0 }}}; //Logical 0
    const rmt_item32_t bit1 = {{{ 3 * RMT_USEC, 0, RMT_USEC, 1 }}}; //Logical 1
    static uint8_t window = 0;
    uint8_t outbit = 0;
    size_t num = 0;
    rmt_item32_t* pdest = dest;
    while (num < wanted_num)
    {
        // move this to nextbit()
        // MC34780 behavior for random bit insertion
      // https://applesaucefdc.com/woz/reference2/
      window <<= 1;
      window |= (uint8_t)diskii_xface.nextbit();
      window &= 0x0f;
      if (window != 0)
      {
        outbit = window & 0x02;
      }
      else
      {
        outbit = diskii_xface.fakebit();
      }

      if (outbit != 0)
      {
        pdest->val = bit1.val;
      }
      else
      {
        pdest->val = bit0.val;
      }
        num++;
        pdest++;
    }
    *translated_size = wanted_num;
    *item_num = wanted_num;
}


/*
 * Initialize the RMT Tx channel
 */
void iwm_diskii_ll::setup_rmt()
{
#define RMT_TX_CHANNEL rmt_channel_t::RMT_CHANNEL_0
  track_buffer = (uint8_t *)heap_caps_malloc(TRACK_LEN, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (track_buffer == NULL)
    Debug_println("could not allocate track buffer");
    
  config.rmt_mode = rmt_mode_t::RMT_MODE_TX;
  config.channel = RMT_TX_CHANNEL;
#ifdef RMTTEST
  config.gpio_num = (gpio_num_t)SP_EXTRA; 
#else
  config.gpio_num = (gpio_num_t)PIN_SD_HOST_MOSI; //SP_WRDATA; // SP_SPI_FIX_PIN ; //PIN_SD_HOST_MOSI;
#endif
  config.mem_block_num = 8;
  config.tx_config.loop_en = false;
  config.tx_config.carrier_en = false;
  config.tx_config.idle_output_en = true;
  config.tx_config.idle_level = rmt_idle_level_t::RMT_IDLE_LEVEL_LOW;
  config.clk_div = 1; // use full 80 MHz resolution of APB clock

  ESP_ERROR_CHECK(fnRMT.rmt_config(&config));
  ESP_ERROR_CHECK(fnRMT.rmt_driver_install(config.channel, 0, ESP_INTR_FLAG_IRAM));
  ESP_ERROR_CHECK(fnRMT.rmt_translator_init(config.channel, encode_rmt_bitstream));
}

bool IRAM_ATTR iwm_diskii_ll::nextbit()
{
  // bits go MSB first
  bool outbit;
  // outbit = (track_buffer[track_byte_ctr] & (0x01 << track_bit_ctr)) != 0;
  outbit = (track_buffer[track_byte_ctr] & (0x80 >> track_bit_ctr)) != 0; // bits go MSB first

  ++track_bit_ctr %= 8;
  if (track_bit_ctr == 0)
    ++track_byte_ctr %= track_numbytes;
  
  if (track_location() >= track_numbits)
  {
    track_bit_ctr = 0;
    track_byte_ctr = 0;
  }

  return outbit;
}

bool IRAM_ATTR iwm_diskii_ll::fakebit()
{
  // MC3470 random bit behavior https://applesaucefdc.com/woz/reference2/ 
  /** Of course, coming up with random values like this can be a bit processor intensive, 
   * so it is adequate to create a randomly-filled circular buffer of 32 bytes. 
   * We then just pull bits from this whenever we are in “fake bit mode”. 
   * This buffer should also be used for empty tracks as designated with an 0xFF value 
   * in the TMAP Chunk (see below). You will want to have roughly 30% of the buffer be 1 bits.
   * 
   * For testing the MC3470 generation of fake bits, you can turn to "The Print Shop Companion". 
   * If you have control at the main menu, then you are passing this test.
   * 
  **/
  // generate PN bits using Octave/MATLAB with
  // for i=1:32, printf("0b"),printf("%d",rand(8,1)<0.3),printf(","),end
  const uint8_t MC3470[] = {0b01010000, 0b10110011, 0b01000010, 0b00000000, 0b10101101, 0b00000010, 0b01101000, 0b01000110, 0b00000001, 0b10010000, 0b00001000, 0b00111000, 0b00001000, 0b00100101, 0b10000100, 0b00001000, 0b10001000, 0b01100010, 0b10101000, 0b01101000, 0b10010000, 0b00100100, 0b00001011, 0b00110010, 0b11100000, 0b01000001, 0b10001010, 0b00000000, 0b11000001, 0b10001000, 0b10001000, 0b00000000};
 
  static int MC3470_byte_ctr;
  static int MC3470_bit_ctr;

  ++MC3470_bit_ctr %= 8;
  if (MC3470_bit_ctr == 0)
    ++MC3470_byte_ctr %= sizeof(MC3470);
  
  return (MC3470[MC3470_byte_ctr] & (0x01 << MC3470_bit_ctr)) != 0;
}

void IRAM_ATTR iwm_diskii_ll::copy_track(uint8_t *track, size_t tracklen, size_t trackbits)
{
// new_position = current_position * new_track_length / current_track_length
// memset 0's when track == nullptr
// Remember to maintain the bit position even when on an empty track (TMAP value of 0xFF). 

  // copy track from SPIRAM to INTERNAL RAM
  if (track != nullptr)
    memcpy(track_buffer, track, tracklen);
  else
    memset(track_buffer, 0, tracklen);
  track_numbytes = tracklen;
  track_numbits = trackbits;
}

uint8_t IRAM_ATTR iwm_diskii_ll::iwm_enable_states()
{
  uint8_t states = 0;

  // only enable diskII if we are either not on an en35 capable host, or we are on an en35host and /EN35=high
  if (!IWM.en35Host || (IWM.en35Host && (GPIO.in1.val & (0x01 << (SP_EN35 - 32)))))
  {
    // Temporary while we debug Disk ][
#ifdef DISKII_DRIVE1
    states |= !((GPIO.in1.val & (0x01 << (SP_DRIVE1 - 32))) >> (SP_DRIVE1 - 32));
#endif
#ifdef DISKII_DRIVE2
    states |= !((GPIO.in & (0x01 << SP_DRIVE2)) >> SP_DRIVE2);
#endif
  }
  return states;
}

iwm_sp_ll smartport;
iwm_diskii_ll diskii_xface;

#endif
