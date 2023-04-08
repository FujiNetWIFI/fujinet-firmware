#ifdef BUILD_APPLE
#ifndef IWM_LL_H
#define IWM_LL_H

#include <queue>
// #include <driver/gpio.h>
#include <driver/gpio.h>
#include <esp_idf_version.h>
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#include <hal/gpio_ll.h>
#endif
#include <driver/spi_master.h>
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

#define RMT_TX_CHANNEL rmt_channel_t::RMT_CHANNEL_0

extern volatile uint8_t _phases;
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

enum class sp_cmd_state_t
{
  standby = 0,
  rxdata,
  command
};
extern volatile sp_cmd_state_t sp_command_mode;

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

class iwm_ll
{
protected:
  // low level bit-banging i/o functions
  bool iwm_req_val() { return (GPIO.in1.val & (0x01 << (SP_REQ-32))); };
  void iwm_extra_set();
  void iwm_extra_clr();
  void disable_output();
  void enable_output();
  
public:
  void setup_gpio();
};

class iwm_sp_ll : public iwm_ll
{
private:  
  void set_output_to_spi();

  // SPI data handling
  uint8_t *spi_buffer; //[8 * (BLOCK_PACKET_LEN+2)]; //smartport packet buffer
  uint16_t spi_len;
  spi_bus_config_t bus_cfg;
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

  //uint8_t packet_buffer[BLOCK_PACKET_LEN]; //smartport packet buffer
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

  size_t decode_data_packet(uint8_t* input_data, uint8_t* output_data); //decode smartport data packet
  size_t decode_data_packet(uint8_t* output_data); //decode smartport data packet
  void encode_packet(uint8_t source, iwm_packet_type_t packet_type, uint8_t status, const uint8_t *data, uint16_t num);

  uint8_t packet_buffer[BLOCK_PACKET_LEN]; //smartport packet buffer

  // For debug printing the checksum
  uint8_t calc_checksum;
  uint8_t pkt_checksum;

  // for tracking last checksum received for Liron bug
  uint8_t last_checksum;

  // hardware configuration setup
  void setup_spi();
  
};

// TO DO - enable/disable output
// done - create enable/disable that do either RDDATA for old or disable/enable GPIO for new
// done - move all enable/disables into the "switch output to SPI/RMT routines"
// move "swithc to SPI" into send data spi routine (enable / disable output using spi fix - no external tristate)
// done - move disable output into disk ii stop
// figure out how to make it all work for three cases: (1) original, (2) spi fix, (3) bypassed buffer
class iwm_diskii_ll : public iwm_ll
{
private:
  // RMT data handling
  fn_rmt_config_t config;

  // track bit information
  uint8_t* track_buffer; // 
  size_t track_numbits = 6400 * 8;
  size_t track_numbytes = 6400;
  size_t track_location = 0;

  void set_output_to_rmt();

public:
  // Phase lines and ACK handshaking
  uint8_t iwm_phase_vector() { return (uint8_t)(GPIO.in1.val & (uint32_t)0b1111); };
  uint8_t iwm_enable_states();

  // Disk II handling by RMT peripheral
  void setup_rmt(); // install the RMT device
  void start();
  void stop();
  // need a function to remove the RMT device?

  bool nextbit();
  bool fakebit();
  void copy_track(uint8_t *track, size_t tracklen, size_t trackbits);

  void set_output_to_low();
};

extern iwm_sp_ll smartport;
extern iwm_diskii_ll diskii_xface;

#endif // IWM_LL_H
#endif // BUILD_APPLE