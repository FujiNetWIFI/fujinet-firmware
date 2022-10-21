#ifdef BUILD_APPLE

#include <string.h>

#include "iwm_ll.h"
#include "fnSystem.h"
#include "fnHardwareTimer.h"
#include "../../include/debug.h"

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

inline bool iwm_sp_ll::iwm_enable_val()
{
  return true;
}

void iwm_sp_ll::encode_spi_packet(uint8_t *a)
{
  // clear out spi buffer
  memset(spi_buffer, 0, SPI_BUFFER_LEN);
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


int IRAM_ATTR iwm_sp_ll::iwm_send_packet_spi(uint8_t *a)
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

  //print_packet((uint8_t *)a);
  encode_spi_packet((uint8_t *)a);

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
      Debug_printf("\r\nSendPacket timeout waiting for REQ");
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
  if (smartport.req_wait_for_falling_timeout(15000))
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


int IRAM_ATTR iwm_sp_ll::iwm_read_packet_spi(uint8_t *a, int n) 
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

  command packet on YS is 919 us
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
  
  memset(spi_buffer, 0xff , SPI_BUFFER_LEN);
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
  int idx = 0;             // index into *a
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
      Debug_printf("\r\npulsewidth = %d, halfwidth = %d",pulsewidth,halfwidth);
      Debug_printf("\r\nspibyte spibit intctr sampval preval rxbit rxbyte");
#endif
      int i = 0;
      while (i < pulsewidth)
      {
        current_level = spirx_get_next_sample();
        current_level ? iwm_extra_clr() : iwm_extra_set();
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
    a[idx++] = rxbyte;
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

void iwm_sp_ll::spi_end() { spi_device_polling_end(spirx, portMAX_DELAY); };

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

  esp_err_t ret; // used for calling SPI library functions below

    spi_buffer=(uint8_t*)heap_caps_malloc(SPI_BUFFER_LEN, MALLOC_CAP_DMA); 

  spi_device_interface_config_t devcfg = {
      .mode = 0,                   // SPI mode 0
      .duty_cycle_pos = 0,         ///< Duty cycle of positive clock, in 1/256th increments (128 = 50%/50% duty). Setting this to 0 (=not setting it) is equivalent to setting this to 128.
      .cs_ena_pretrans = 0,        ///< Amount of SPI bit-cycles the cs should be activated before the transmission (0-16). This only works on half-duplex transactions.
      .cs_ena_posttrans = 0,       ///< Amount of SPI bit-cycles the cs should stay active after the transmission (0-16)
      .clock_speed_hz = 1 * 1000 * 1000, // Clock out at 1 MHz
      .input_delay_ns = 0,
      .spics_io_num = -1,                // CS pin
      .queue_size = 2                    // We want to be able to queue 7 transactions at a time
  };

    // use same SPI as SDCARD
    ret=spi_bus_add_device(HSPI_HOST, &devcfg, &spi);

  assert(ret == ESP_OK);

// SPI for receiving packets - sprirx
// use different SPI than SDCARD
  spi_bus_config_t bus_cfg = {
      .mosi_io_num = -1,
      .miso_io_num = SP_WRDATA,
      .sclk_io_num = -1,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = SPI_BUFFER_LEN,
      .flags = SPICOMMON_BUSFLAG_MASTER,
      .intr_flags = 0};
  spi_device_interface_config_t rxcfg = {
      .mode = 0, // SPI mode 0
      .duty_cycle_pos = 0,         ///< Duty cycle of positive clock, in 1/256th increments (128 = 50%/50% duty). Setting this to 0 (=not setting it) is equivalent to setting this to 128.
      .cs_ena_pretrans = 0,
      .cs_ena_posttrans = 0,
      .clock_speed_hz = f_spirx, // f_over * f_nyquist, // Clock at 500 kHz x oversampling factor
      .input_delay_ns = 0,
      .spics_io_num = -1,        // CS pin
      .flags = SPI_DEVICE_HALFDUPLEX,
      .queue_size = 1};          // We want to be able to queue 7 transactions at a time

  ret = spi_bus_initialize(VSPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
  assert(ret == ESP_OK);
  ret = spi_bus_add_device(VSPI_HOST, &rxcfg, &spirx);
  assert(ret == ESP_OK);

}

void iwm_sp_ll::setup_gpio()
{
  fnSystem.set_pin_mode(SP_ACK, gpio_mode_t::GPIO_MODE_OUTPUT);
  fnSystem.digital_write(SP_ACK, DIGI_LOW); // set up ACK ahead of time to go LOW when enabled
  //set ack to input to avoid clashing with other devices when sp bus is not enabled
  fnSystem.set_pin_mode(SP_ACK, gpio_mode_t::GPIO_MODE_INPUT);
  
  fnSystem.set_pin_mode(SP_PHI0, gpio_mode_t::GPIO_MODE_INPUT); // REQ line
  fnSystem.set_pin_mode(SP_PHI1, gpio_mode_t::GPIO_MODE_INPUT);
  fnSystem.set_pin_mode(SP_PHI2, gpio_mode_t::GPIO_MODE_INPUT);
  fnSystem.set_pin_mode(SP_PHI3, gpio_mode_t::GPIO_MODE_INPUT);

  // fnSystem.set_pin_mode(SP_WRDATA, gpio_mode_t::GPIO_MODE_INPUT); // not needed cause set in SPI?

  fnSystem.set_pin_mode(SP_WREQ, gpio_mode_t::GPIO_MODE_INPUT);
  fnSystem.set_pin_mode(SP_DRIVE1, gpio_mode_t::GPIO_MODE_INPUT);
  fnSystem.set_pin_mode(SP_DRIVE2, gpio_mode_t::GPIO_MODE_INPUT);
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
  Debug_printf("\r\nEXTRA signaling line configured");
#endif
  
}

iwm_sp_ll smartport;

#endif
