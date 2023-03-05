#ifdef BUILD_APPLE
#ifndef IWM_LL_H
#define IWM_LL_H

#include <queue>
// #include <driver/gpio.h>
#include <driver/spi_master.h>
// #include <driver/spi_common.h>
// #include "soc/spi_periph.h"
// #include "soc/io_mux_reg.h"
// #include "esp_rom_gpio.h"
// #include "hal/gpio_hal.h"
#include <freertos/semphr.h>

#include "../../include/pinmap.h"
#include "fnRMTstream.h"

// #define SPI_II_LEN 27000        // 200 ms at 1 mbps for disk ii + some extra
#define TRACK_LEN 6646          // https://applesaucefdc.com/woz/reference2/
#define SPI_SP_LEN 6000         // should be long enough for 20.1 ms (for SoftSP) + some margin - call it 22 ms. 2051282*.022 =  45128.204 bits / 8 = 5641.0255 bytes
#define BLOCK_PACKET_LEN    604 //606

#define PACKET_TYPE_CMD 0x80
#define PACKET_TYPE_STATUS 0x81
#define PACKET_TYPE_DATA 0x82

extern volatile uint8_t _phases;
extern volatile bool sp_command_mode;
extern volatile int isrctr;

enum class iwm_packet_type_t
{
  cmd = PACKET_TYPE_CMD,
  status = PACKET_TYPE_STATUS,
  data = PACKET_TYPE_DATA,
  ext_cmd = PACKET_TYPE_CMD | 0x40,
  ext_status = PACKET_TYPE_STATUS | 0x40,
  ext_data = PACKET_TYPE_DATA | 0x40
};

/** ACK and REQ
 * 
 * SmartPort ACK and REQ lines are used in a return-to-zero 4-phase handshake sequence.
 * 
 * how ACK works, my interpretation of the iigs firmware reference.
 * ACK is normally high-Z (deasserted) when device is ready to receive commands.
 * host will send (assert) REQ high to make a request and send a command.
 * device responds after command is received by sending (assert) ACK low.
 * host completes command handshake by sending REQ low (deassert).
 * device signals its ready for the next step (receive/send/status)
 * by sending ACK back high (deassert).
 * 
 * The sequence is:
 * 
 * step   REQ         ACK               smartport state
 * 0      deassert    deassert          idle
 * 1      assert      deassert          enabled, apple ii sending command or data to peripheral
 * 2      assert      assert            peripheral acknowledges it received data
 * 3      deassert    assert            apple ii does it's part to return to idle, peripheral is processing command or data
 * 0      deassert    deassert          peripheral returns to idle when it's ready for another command
 * 
 * Electrically, how ACK works with multiple devices on bus:
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

class iwm_sp_ll
{
private:
  // low level bit-banging i/o functions
  void iwm_rddata_set() { GPIO.out_w1ts = ((uint32_t)1 << SP_RDDATA); }; // make RDDATA go hi-z through the tri-state
  void iwm_rddata_clr() { GPIO.out_w1tc = ((uint32_t)1 << SP_RDDATA); }; // enable the tri-state buffer activating RDDATA
  bool iwm_req_val() { return (GPIO.in1.val & (0x01 << (SP_REQ-32))); };
  void iwm_extra_set();
  void iwm_extra_clr();

  // SPI data handling
  uint8_t *spi_buffer; //[8 * (BLOCK_PACKET_LEN+2)]; //smartport packet buffer
  uint16_t spi_len;
  spi_device_handle_t spi;
  // SPI receiver
  spi_transaction_t rxtrans;
  spi_device_handle_t spirx;
  /** SPI data clock 
   * N  Clock MHz   /8 Bit rate (kHz)    Bit/Byte period (us)
   * 39	2.051282051	256.4102564	        3.9	31.2          256410 is only 0.3% faster than 255682
   * 40	2	          250.	                4.0	32
   * 41	1.951219512	243.902439	          4.1	32.8
  **/
  // const int f_spirx = APB_CLK_FREQ / 39; // 2051282 Hz or 2052kHz or 2.052 MHz - works for NTSC but ...
  const int f_spirx = APB_CLK_FREQ / 40; // 2 MHz - need slower rate for PAL
  const int pulsewidth = 8; // 8 samples per bit
  const int halfwidth = pulsewidth / 2;

  // SPI receiver data stream counters
  int spirx_byte_ctr;
  int spirx_bit_ctr;

  uint8_t packet_buffer[BLOCK_PACKET_LEN]; //smartport packet buffer
  uint16_t packet_len;

public:
  SemaphoreHandle_t spiMutex;
  // Phase lines and ACK handshaking
  void iwm_ack_set() { GPIO.enable_w1tc = ((uint32_t)0x01 << SP_ACK); }; // disable the line so it goes hi-z
  void iwm_ack_clr() { GPIO.enable_w1ts = ((uint32_t)0x01 << SP_ACK); };  // enable the line already set to low
  bool req_wait_for_falling_timeout(int t);
  bool req_wait_for_rising_timeout(int t);
  uint8_t iwm_phase_vector() { return (uint8_t)(GPIO.in1.val & (uint32_t)0b1111); };

  // Smartport Bus handling by SPI interface
  void encode_spi_packet();
  int iwm_send_packet_spi();
  bool spirx_get_next_sample();
  int iwm_read_packet_spi(uint8_t *buffer, int n);
  int iwm_read_packet_spi(int n);
  void spi_end();

  int decode_data_packet(uint8_t* input_data, uint8_t* output_data); //decode smartport data packet
  int decode_data_packet(uint8_t* output_data); //decode smartport data packet
  void encode_packet(uint8_t source, iwm_packet_type_t packet_type, uint8_t status, const uint8_t *data, uint16_t num);

  // hardware configuration setup
  void setup_spi();
  void setup_gpio();

  void set_output_to_spi();
};

class iwm_diskii_ll
{
private:
  // SPI data handling
  // uint8_t *spi_buffer; //[8 * (BLOCK_PACKET_LEN+2)]; //smartport packet buffer
  // int spi_len;
  // spi_device_handle_t spi;
  // int fspi;
  // std::queue<spi_transaction_t> trans;

  // RMT data handling
  fn_rmt_config_t config;
  // where to put the track buffer for translation?

  // tri-state buffer control - copied from sp_ll - probably should make one version only but alas
  void iwm_rddata_set() { GPIO.out_w1ts = ((uint32_t)1 << SP_RDDATA); }; // make RDDATA go hi-z through the tri-state
  void iwm_rddata_clr() { GPIO.out_w1tc = ((uint32_t)1 << SP_RDDATA); }; // enable the tri-state buffer activating RDDATA

  // MC3470 random bit behavior https://applesaucefdc.com/woz/reference2/ 
  /** Of course, coming up with random values like this can be a bit processor intensive, 
   * so it is adequate to create a randomly-filled circular buffer of 32 bytes. 
   * We then just pull bits from this whenever we are in “fake bit mode”. 
   * This buffer should also be used for empty tracks as designated with an 0xFF value 
   * in the TMAP Chunk (see below). You will want to have roughly 30% of the buffer be 1 bits.
  **/
  int MC3470_byte_ctr;
  int MC3470_bit_ctr;

  // track bit information
  uint8_t* track_buffer; // 
  int track_byte_ctr;
  int track_bit_ctr;
  size_t track_numbits;
  size_t track_numbytes;
  size_t track_location() { return track_bit_ctr + 8 * track_byte_ctr; };
 

public:
  // Phase lines and ACK handshaking
  uint8_t iwm_phase_vector() { return (uint8_t)(GPIO.in1.val & (uint32_t)0b1111); };
  uint8_t iwm_enable_states();

  void disable_output() { iwm_rddata_set(); };
  void enable_output()  { iwm_rddata_clr(); };
  
  // Disk II handling by RMT peripheral
  void setup_rmt(); // install the RMT device
  void rmttest();
  // need a function to start the RMT stream
  // need a function to stop the RMT stream
  // need a function to remove the RMT device?

  bool nextbit();
  bool fakebit();
  void copy_track(uint8_t *track, size_t tracklen, size_t trackbits);
//  void encode_spi_packet(uint8_t *track, int tracklen, int trackbits, int indicator);
//  void iwm_queue_track_spi();

//  void spi_end();
void set_output_to_rmt();
};

extern iwm_sp_ll smartport;
extern iwm_diskii_ll diskii_xface;

#endif // IWM_LL_H
#endif // BUILD_APPLE