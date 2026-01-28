#ifdef BUILD_MAC
#ifndef MAC_LL_H
#define MAC_LL_H

// #include <queue>
#include <driver/gpio.h>
#include <esp_idf_version.h>
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
  #include <hal/gpio_ll.h>
#endif
// #include <driver/spi_master.h>
// #include <freertos/semphr.h>

#include "../../include/pinmap.h"
// #include "fnRMTstream.h"
#include <driver/rmt_types.h>
// #include <driver/rmt_common.h>
// #include <driver/rmt_tx.h>
// #include <driver/rmt_encoder.h>


// // #define SPI_II_LEN 27000        // 200 ms at 1 mbps for disk ii + some extra
#define TRACK_LEN 10000              // guess for MOOF - should probably read it from MOOF file
// #define SPI_SP_LEN 6000         // should be long enough for 20.1 ms (for SoftSP) + some margin - call it 22 ms. 2051282*.022 =  45128.204 bits / 8 = 5641.0255 bytes
// #define BLOCK_PACKET_LEN    604 //606

// #define PACKET_TYPE_CMD 0x80
// #define PACKET_TYPE_STATUS 0x81
// #define PACKET_TYPE_DATA 0x82

#define RMT_TX_CHANNEL tx_chan

class mac_ll
{
protected:
  // low level bit-banging i/o functions
  // bool iwm_req_val() { return (GPIO.in1.val & (0x01 << (SP_REQ-32))); };
  // void iwm_extra_set();
  // void iwm_extra_clr();
  // void disable_output();
  // void enable_output();
  bool mac_headsel_val() { return ((GPIO.in1.val) & (0x01U << (MCI_HDSEL-32))); }
public:
  void setup_gpio();
};


// TO DO - enable/disable output
// done - create enable/disable that do either RDDATA for old or disable/enable GPIO for new
// done - move all enable/disables into the "switch output to SPI/RMT routines"
// move "swithc to SPI" into send data spi routine (enable / disable output using spi fix - no external tristate)
// done - move disable output into disk ii stop
// figure out how to make it all work for three cases: (1) original, (2) spi fix, (3) bypassed buffer
class mac_floppy_ll : public mac_ll
{
private:
  // RMT data handling
  rmt_channel_handle_t RMT_TX_CHANNEL;
  rmt_encoder_handle_t tx_encoder;

  // track bit information
  uint8_t *track_buffer[2] = {nullptr, nullptr}; //
  size_t track_numbits[2] = {TRACK_LEN * 8, TRACK_LEN * 8};
  size_t track_numbytes[2] = {TRACK_LEN, TRACK_LEN};
  size_t track_location[2] = {0, 0};
  int track_bit_period = 2000;

  bool rmt_started = false;

  // void set_output_to_rmt();

public:
  // Phase lines and ACK handshaking
  // uint8_t iwm_phase_vector() { return (uint8_t)(GPIO.in1.val & (uint32_t)0b1111); };
  // uint8_t iwm_enable_states();

  // MCI handling by RMT peripheral
  size_t encode_rmt_bitstream(const void *src, size_t src_size,
			      size_t symbols_written, size_t symbols_free,
			      rmt_symbol_word_t *dest, bool *done);
  void setup_rmt(); // install the RMT device
  void start();
  void stop();
  // need a function to remove the RMT device?

  bool nextbit();
  bool fakebit();
  void copy_track(uint8_t *track, int side, size_t tracklen, size_t trackbits, int bitperiod);

  // void set_output_to_low();
};

extern mac_floppy_ll floppy_ll;

#endif // MAC_LL_H
#endif // BUILD_MAC
