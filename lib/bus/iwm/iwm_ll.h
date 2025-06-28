#ifdef BUILD_APPLE
#ifndef IWM_LL_H
#define IWM_LL_H

#include <queue>
#include <driver/gpio.h>
#include <driver/rmt_types.h>
#include <soc/lldesc.h>
#include <esp_idf_version.h>
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#include <hal/gpio_ll.h>
#endif
#include <driver/spi_master.h>
#include <freertos/semphr.h>

#include "../../include/pinmap.h"

// #define SPI_II_LEN 27000        // 200 ms at 1 mbps for disk ii + some extra
#define TRACK_LEN 6646          // https://applesaucefdc.com/woz/reference2/
#define SPI_SP_LEN 6000         // should be long enough for 20.1 ms (for SoftSP) + some margin - call it 22 ms. 2051282*.022 =  45128.204 bits / 8 = 5641.0255 bytes
#define BLOCK_PACKET_LEN    604 //606

#define PACKET_TYPE_CMD 0x80
#define PACKET_TYPE_STATUS 0x81
#define PACKET_TYPE_DATA 0x82

#define RMT_TX_CHANNEL tx_chan

extern volatile uint8_t _phases;
extern volatile int isrctr;

#define COMMAND_PACKET_LEN  27 //28     - max length changes suggested by robj
// to do - make block packet compatible up to 767 data bytes?

#define IWM_BIT(pin) ({						  \
      uint32_t _pin = pin;					  \
      (_pin >= 32 ? GPIO.in1.val : GPIO.in) & (1 << (_pin % 32)); \
    })
#define IWM_BIT_CLEAR(pin) ({			\
      uint32_t _pin = pin;			\
      uint32_t _mask = 1 << (_pin % 32);	\
      if (_pin >= 32)				\
	GPIO.out1_w1tc.val = _mask;		\
      else					\
	GPIO.out_w1tc = _mask;			\
    })
#define IWM_BIT_SET(pin) ({			\
      uint32_t _pin = pin;			\
      uint32_t _mask = 1 << (_pin % 32);	\
      if (_pin >= 32)				\
	GPIO.out1_w1ts.val = _mask;		\
      else					\
	GPIO.out_w1ts = _mask;			\
    })
#define IWM_BIT_INPUT(pin) ({			\
      uint32_t _pin = pin;			\
      uint32_t _mask = 1 << (_pin % 32);	\
      if (_pin >= 32)				\
	GPIO.enable1_w1tc.val = _mask;		\
      else					\
	GPIO.enable_w1tc = _mask;		\
    })
#define IWM_BIT_OUTPUT(pin) ({			\
      uint32_t _pin = pin;			\
      uint32_t _mask = 1 << (_pin % 32);	\
      if (_pin >= 32)				\
	GPIO.enable1_w1ts.val = _mask;		\
      else					\
	GPIO.enable_w1ts = _mask;		\
    })

union cmdPacket_t
{
/*
C3 PBEGIN   MARKS BEGINNING OF PACKET 32 micro Sec.
81 DEST     DESTINATION UNIT NUMBER 32 micro Sec.
80 SRC      SOURCE UNIT NUMBER 32 micro Sec.
80 TYPE     PACKET TYPE FIELD 32 micro Sec.
80 AUX      PACKET AUXILLIARY TYPE FIELD 32 micro Sec.
80 STAT     DATA STATUS FIELD 32 micro Sec.
82 ODDCNT   ODD BYTES COUNT 32 micro Sec.
81 GRP7CNT  GROUP OF 7 BYTES COUNT 32 micro Sec.
80 ODDMSB   ODD BYTES MSB's 32 micro Sec.
81 COMMAND  1ST ODD BYTE = Command Byte 32 micro Sec.
83 PARMCNT  2ND ODD BYTE = Parameter Count 32 micro Sec.
80 GRP7MSB  MSB's FOR 1ST GROUP OF 7 32 micro Sec.
80 G7BYTE1  BYTE 1 FOR 1ST GROUP OF 7 32 micro Sec.
98 G7BYTE2  BYTE 2 FOR 1ST GROUP OF 7 32 micro Sec.
82 G7BYTE3  BYTE 3 FOR 1ST GROUP OF 7 32 micro Sec.
80 G7BYTE4  BYTE 4 FOR 1ST GROUP OF 7 32 micro Sec.
80 G7BYTE5  BYTE 5 FOR 1ST GROUP OF 7 32 micro Sec.
80 G7BYTE5  BYTE 6 FOR 1ST GROUP OF 7 32 micro Sec.
80 G7BYTE6  BYTE 7 FOR 1ST GROUP OF 7 32 micro Sec.
BB CHKSUM1  1ST BYTE OF CHECKSUM 32 micro Sec.
EE CHKSUM2  2ND BYTE OF CHECKSUM 32 micro Sec.
C8 PEND     PACKET END BYTE 32 micro Sec.
00 CLEAR    zero after packet for FujiNet use
*/
struct
{
  uint8_t sync1;   // 0
  uint8_t sync2;   // 1
  uint8_t sync3;   // 2
  uint8_t sync4;   // 3
  uint8_t sync5;   // 4
  uint8_t pbegin;  // 5
  uint8_t dest;    // 6
  uint8_t source;  // 7
  uint8_t type;    // 8
  uint8_t aux;     // 9
  uint8_t stat;    // 10
  uint8_t oddcnt;  // 11
  uint8_t grp7cnt; // 12
  uint8_t oddmsb;  // 13
  uint8_t command; // 14
  uint8_t parmcnt; // 15
  uint8_t grp7msb; // 16
  uint8_t g7byte1; // 17
  uint8_t g7byte2; // 18
  uint8_t g7byte3; // 19
  uint8_t g7byte4; // 20
  uint8_t g7byte5; // 21
  uint8_t g7byte6; // 22
  uint8_t g7byte7; // 23
  uint8_t chksum1; // 24
  uint8_t chksum2; // 25
  uint8_t pend;    // 26
  uint8_t clear;   // 27
  };
  uint8_t data[COMMAND_PACKET_LEN + 1];
};

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
  // SPI receiver
  spi_transaction_t rxtrans;

  // low level bit-banging i/o functions
  bool iwm_req_val() { return (IWM_BIT(SP_REQ)); };
  void iwm_extra_set();
  void iwm_extra_clr();
  void disable_output();
  void enable_output();

public:
  void setup_gpio();
  uint8_t iwm_decode_byte(uint8_t *src, size_t src_size, unsigned int sample_frequency,
			  int timeout, size_t *bit_offset, bool *more_avail);
  size_t iwm_decode_buffer(uint8_t *src, size_t src_size, unsigned int sample_frequency,
			   int timeout, uint8_t *dest, size_t *used);
};

class iwm_sp_ll : public iwm_ll
{
private:
  void set_output_to_spi();

  // SPI data handling
  uint8_t *spi_buffer = nullptr; //[8 * (BLOCK_PACKET_LEN+2)]; //smartport packet buffer
  spi_device_handle_t spi;

public:
  /** SPI data clock
   * N  Clock MHz   /8 Bit rate (kHz)    Bit/Byte period (us)
   * 39 2.051282051     256.4102564             3.9     31.2          256410 is only 0.3% faster than 255682
   * 40 2                 250.                  4.0     32
   * 41 1.951219512     243.902439                4.1   32.8
  **/
  // const int f_spirx = APB_CLK_FREQ / 39; // 2051282 Hz or 2052kHz or 2.052 MHz - works for NTSC but ...
  const int f_spirx = APB_CLK_FREQ / 40; // 2 MHz - need slower rate for PAL
  spi_device_handle_t spirx;

  //uint8_t packet_buffer[BLOCK_PACKET_LEN]; //smartport packet buffer
  uint16_t packet_len = 0;

public:
  SemaphoreHandle_t spiMutex;
  // Phase lines and ACK handshaking
  void iwm_ack_set() { IWM_BIT_INPUT(SP_ACK); }; // disable the line so it goes hi-z
  void iwm_ack_clr() { IWM_BIT_OUTPUT(SP_ACK); };  // enable the line already set to low
  bool req_wait_for_falling_timeout(int t);
  bool req_wait_for_rising_timeout(int t);
  uint8_t iwm_phase_vector() { return IWM_PHASE_COMBINE(); };

  // Smartport Bus handling by SPI interface
  int encode_spi_packet();
  int iwm_send_packet_spi();
  int iwm_read_packet_spi(uint8_t *buffer, int packet_len);
  int iwm_read_packet_spi(int packet_len);
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
  rmt_channel_handle_t tx_chan;
  rmt_encoder_handle_t tx_encoder;

  // track bit information
  uint8_t* track_buffer = nullptr; //
  size_t track_numbits = 6400 * 8;
  size_t track_numbytes = 6400;
  size_t track_location = 0;
  int track_bit_period = 4000;

  bool rmt_started = false;

  // write state
  bool d2w_writing = false, d2w_started = false;
  uint8_t *d2w_buffer;
  lldesc_t *d2w_desc;
  size_t d2w_buflen, d2w_begin;
  size_t d2w_position;

public:
  QueueHandle_t iwm_write_queue;
  uint8_t d2_enable_seen = 0;

  // Phase lines and ACK handshaking
  uint8_t iwm_phase_vector() { return (uint8_t)(GPIO.in1.val & (uint32_t)0b1111); };
  uint8_t iwm_active_drive();

  // Disk II handling by RMT peripheral
  size_t encode_rmt_bitstream(const void *src, size_t src_size,
			      size_t symbols_written, size_t symbols_free,
			      rmt_symbol_word_t *dest, bool *done);
  void setup_rmt(); // install the RMT device
  void diskii_write_handler();
  void start(uint8_t drive, bool write_protect);
  void stop();
  // need a function to remove the RMT device?

  bool nextbit();
  bool fakebit();
  void copy_track(uint8_t *track, size_t tracklen, size_t trackbits, int bitperiod);

  void set_output_to_low();
};

extern iwm_sp_ll smartport;
extern iwm_diskii_ll diskii_xface;

typedef struct {
  int quarter_track;
  size_t track_begin, track_end, track_numbits;
  uint8_t *buffer;
  size_t length;
} iwm_write_data;

#define D2W_CHUNK_SIZE 128

#endif // IWM_LL_H
#endif // BUILD_APPLE
