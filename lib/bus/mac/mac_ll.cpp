#ifdef BUILD_MAC

#include <string.h>

#include "mac_ll.h"
#include "fnSystem.h"
#include "../../include/debug.h"
#include "led.h"

#include <driver/rmt_tx.h>
#include <driver/rmt_encoder.h>

#define MHZ (1000*1000)

void mac_ll::setup_gpio()
{
  fnSystem.set_pin_mode(MCI_HDSEL, gpio_mode_t::GPIO_MODE_INPUT);

  if (!fnSystem.no3state())
  {
    fnSystem.set_pin_mode(SP_RDDATA, gpio_mode_t::GPIO_MODE_OUTPUT); // tri-state buffer control
    fnSystem.digital_write(SP_RDDATA, DIGI_LOW); // Turn tristate buffer ON by default
  }

#ifdef EXTRA
  fnSystem.set_pin_mode(SP_EXTRA, gpio_mode_t::GPIO_MODE_OUTPUT);
  fnSystem.digital_write(SP_EXTRA, DIGI_LOW);
  fnSystem.digital_write(SP_EXTRA, DIGI_HIGH); // ID extra for logic analyzer
  fnSystem.digital_write(SP_EXTRA, DIGI_LOW);
  fnSystem.digital_write(SP_EXTRA, DIGI_HIGH); // ID extra for logic analyzer
  fnSystem.digital_write(SP_EXTRA, DIGI_LOW);
  Debug_printf("\nEXTRA signaling line configured");
#endif
}

// =========================================================================================
// ===== MAC Microfloppy Control Interface (MCI) below ================ DCD above ==========
// =========================================================================================

// https://docs.espressif.com/projects/esp-idf/en/v5.4.3/esp32/api-reference/peripherals/rmt.html

#define RMT_USEC (APB_CLK_FREQ / MHZ)

void mac_floppy_ll::start() // TO UPDATE TO REV 5
{
    if (!rmt_started)
    {
        rmt_tx_channel_config_t tx_chan_config;

        tx_chan_config.gpio_num = (gpio_num_t)
            SP_WRDATA; /*!< GPIO number used by RMT TX channel. Set to -1 if unused */
        tx_chan_config.clk_src =
            RMT_CLK_SRC_APB; /*!< Clock source of RMT TX channel, channels in the same group
                                must use the same clock source */
        tx_chan_config.resolution_hz = APB_CLK_FREQ; /*!< Channel clock resolution, in Hz */
        tx_chan_config.mem_block_symbols = 64 * 8;   // MAYBE CHANGE TO 1
        /*!< Size of memory block, in number of `rmt_symbol_word_t`, must be an even.
      In the DMA mode, this field controls the DMA buffer size, it can be set to a large
      value; In the normal mode, this field controls the number of RMT memory block that will
      be used by the channel. */
        tx_chan_config.trans_queue_depth = 4;
        /*!< Depth of internal transfer queue, increase this value can support more
                  transfers pending in the background */
        tx_chan_config.intr_priority = 0; /*!< RMT interrupt priority,
                                     if set to 0, the driver will try to allocate an interrupt
                                     with a relative low priority (1,2,3) */
        tx_chan_config.flags.invert_out =
            false; /*!< Whether to invert the RMT channel signal before output to GPIO pad */
        tx_chan_config.flags.with_dma =
            false; /*!< If set, the driver will allocate an RMT channel with DMA capability */
        tx_chan_config.flags.io_loop_back = false; /*!< The signal output from the GPIO will be
                                                      fed to the input path as well */
        tx_chan_config.flags.io_od_mode = false; /*!< Configure the GPIO as open-drain mode */
        tx_chan_config.flags.allow_pd =
            false; /*!< If set, driver allows the power domain to be powered off when system
            enters sleep mode. This can save power, but at the expense of more RAM being
            consumed to save register context. */

        ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &RMT_TX_CHANNEL));

        rmt_transmit_config_t tx_config = {
            .loop_count = 0, //-1,
            .flags =
                {
                    .eot_level = 0, /*!< Set the output level for the "End Of Transmission" */
                    .queue_nonblocking =
                        false, /*!< If set, when the transaction queue is full, driver will not
                                  block the thread but return directly */
                },
        };

        ESP_ERROR_CHECK(rmt_enable(RMT_TX_CHANNEL));
        ESP_ERROR_CHECK(
            rmt_transmit(RMT_TX_CHANNEL, tx_encoder, 
              track_buffer, track_numbits[0], &tx_config)); // I DON'T KNOW IF THIS WORKS
        rmt_started = true;

    }; // if not started yet

    fnLedManager.set(LED_BUS, true);
    Debug_printf("\nstart floppy");
}

void mac_floppy_ll::stop() // TO UPDATE TO REV 5
{
  if (rmt_started) {
    ESP_ERROR_CHECK(rmt_disable(RMT_TX_CHANNEL));
    ESP_ERROR_CHECK(rmt_del_channel(RMT_TX_CHANNEL));
    rmt_started = false;
  }
  // floppy_ll.disable_output();
  fnLedManager.set(LED_BUS, false);
  Debug_printf("\nstop floppy");
}

size_t IRAM_ATTR encode_rmt_bitstream_forwarder(const void *src, size_t src_size,
                                      size_t symbols_written, size_t symbols_free,
                                      rmt_symbol_word_t *dest, bool *done, void *arg)
{
  mac_floppy_ll *mci = (mac_floppy_ll *) arg;
  return mci->encode_rmt_bitstream(src, src_size, symbols_written, symbols_free, dest, done);
}

//Convert track data to rmt format data. // TO UPDATE TO REV 5
size_t IRAM_ATTR mac_floppy_ll::encode_rmt_bitstream(const void *src, size_t src_size,
                                      size_t symbols_written, size_t symbols_free,
                                      rmt_symbol_word_t *dest, bool *done)
{
    // legacy RMT notes:
    // *src is equal to *track_buffer
    // src_size is equal to numbits
    // translated_size is not used
    // item_num will equal wanted_num at end

    if (src == NULL || dest == NULL)
    {
       return 0;
    }

    // TODO: allow adjustment of bit timing per MOOF optimal bit timing
    //
    uint32_t bit_ticks = RMT_USEC; // ticks per microsecond (1000 ns)
    bit_ticks *= track_bit_period; // now units are ticks * ns /us
    bit_ticks /= 1000; // now units are ticks

#define BIT_TICK_34 ((uint16_t)((3 * bit_ticks) / 4))
#define BIT_TICK_14 ((uint16_t)(bit_ticks / 4))

    const rmt_symbol_word_t bits[] = {
        {{BIT_TICK_34, 0, BIT_TICK_14, 0}},
        {{BIT_TICK_34, 0, BIT_TICK_14, 1}},
    };
    static uint8_t window = 0;
    uint8_t outbit = 0;
    size_t num = 0;

  for (num = 0; num < symbols_free; num++, dest++)
    {
      outbit = floppy_ll.nextbit();
      dest->val = bits[!!outbit].val;
    }
    *done = false;
    return num;
}


/*
 * Initialize the RMT Tx channel
 */
void mac_floppy_ll::setup_rmt() // TO UPDATE TO REV 5
{
  track_buffer[0] = (uint8_t *)heap_caps_malloc(TRACK_LEN, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (track_buffer[0] == NULL)
    Debug_println("could not allocate track buffer 0");
  track_buffer[1] = (uint8_t *)heap_caps_malloc(TRACK_LEN, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (track_buffer[1] == NULL)
    Debug_println("could not allocate track buffer 1");

  rmt_simple_encoder_config_t tx_encoder_config = {
    .callback = encode_rmt_bitstream_forwarder,
    .arg = (void *) this,
    .min_chunk_size = 0,
  };

  ESP_ERROR_CHECK(rmt_new_simple_encoder(&tx_encoder_config, &tx_encoder));

  //---


// #ifdef RMTTEST
//   config.gpio_num = (gpio_num_t)SP_EXTRA;
// #else
//   if(fnSystem.hasbuffer())
//     config.gpio_num = (gpio_num_t)SP_RDDATA;
//   else
//     config.gpio_num = (gpio_num_t)PIN_SD_HOST_MOSI;
// #endif


  // ESP_ERROR_CHECK(fnRMT.rmt_config(&config));
  // ESP_ERROR_CHECK(fnRMT.rmt_driver_install(config.channel, 0, ESP_INTR_FLAG_IRAM));
  // ESP_ERROR_CHECK(fnRMT.rmt_translator_init(config.channel, encode_rmt_bitstream));
}

bool IRAM_ATTR mac_floppy_ll::nextbit() // TO UPDATE TO REV 5
{

  bool outbit[2];
  int track_byte_ctr[2];
  int track_bit_ctr[2];

  for (int side = 0; side < 2; side++)
  {
    track_byte_ctr[side] = track_location[side] / 8;
    track_bit_ctr[side] = track_location[side] % 8;

    outbit[side] = (track_buffer[side][track_byte_ctr[side]] & (0x80 >> track_bit_ctr[side])) != 0; // bits go MSB first

    track_location[side]++;
    if (track_location[side] >= track_numbits[side])
    {
      track_location[side] = 0;
    }
  }
  // choose side based on HDSEL!
  bool side = mac_headsel_val();
  return outbit[(int)side];
}

bool IRAM_ATTR mac_floppy_ll::fakebit() // TO UPDATE TO REV 5
{
  // just a straight copy from Apple Disk II emulator. Have no idea if this
  // behavior is close enough to the MCI floppy or not.
  //
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

// todo: copy both top and bottom tracks on 800k disk // TO UPDATE TO REV 5
void IRAM_ATTR mac_floppy_ll::copy_track(uint8_t *track, int side, size_t tracklen, size_t trackbits, int bitperiod)
{
  if (track_buffer[side] == nullptr)
  {
    Debug_printf("\nerror track buffer not allocated");
    return;
  }
  // Debug_printf("\ncopying track:");
  // Debug_printf("\nside %d, length %d", side, tracklen);
  // side is 0 or 1
  // copy track from SPIRAM to INTERNAL RAM
  if (side == 0 || side == 1)
  {
    if (track != nullptr)
      memcpy(track_buffer[side], track, tracklen);
    else
      memset(track_buffer[side], 0, tracklen);
  }
  else
  {
    Debug_printf("\nSide out of range in copy_track()");
    return;
  }
  // new_position = current_position * new_track_length / current_track_length
  track_location[side] *= trackbits;
  track_location[side] /= track_numbits[side];
  // update track info
  track_numbytes[side] = tracklen;
  track_numbits[side] = trackbits;
  track_bit_period = bitperiod;
}

mac_floppy_ll floppy_ll;

#endif // BUILD_MAC
