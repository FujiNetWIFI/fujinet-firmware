#ifdef BUILD_MAC

#include <string.h>

#include "mac_ll.h"
#include "fnSystem.h"
#include "../../include/debug.h"
#include "led.h"

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

// https://docs.espressif.com/projects/esp-idf/en/v3.3.5/api-reference/peripherals/rmt.html
#ifdef NOT_IWM_LL_SUBCLASSS
#define RMT_TX_CHANNEL rmt_channel_t::RMT_CHANNEL_0
#define RMT_USEC (APB_CLK_FREQ / MHZ)
#endif /* NOT_IWM_LL_SUBCLASS */

void mac_floppy_ll::start()
{
  // floppy_ll.set_output_to_rmt();
  // floppy_ll.enable_output();
#ifdef NOT_IWM_LL_SUBCLASSS
  ESP_ERROR_CHECK(fnRMT.rmt_write_bitstream(RMT_TX_CHANNEL, track_buffer[0], track_numbits[0], track_bit_period));
#endif /* NOT_IWM_LL_SUBCLASS */
  fnLedManager.set(LED_BUS, true);
  Debug_printf("\nstart floppy");
}

void mac_floppy_ll::stop()
{
#ifdef NOT_IWM_LL_SUBCLASSS
  fnRMT.rmt_tx_stop(RMT_TX_CHANNEL);
#endif /* NOT_IWM_LL_SUBCLASS */
  // floppy_ll.disable_output();
  fnLedManager.set(LED_BUS, false);
  Debug_printf("\nstop floppy");
}

//Convert track data to rmt format data.
void IRAM_ATTR encode_rmt_bitstream(const void* src, rmt_item32_t* dest, size_t src_size,
                         size_t wanted_num, size_t* translated_size, size_t* item_num, int bit_period)
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

    // TODO: allow adjustment of bit timing per WOZ optimal bit timing
    //
    uint32_t bit_ticks = RMT_USEC; // ticks per microsecond (1000 ns)
    bit_ticks *= bit_period; // now units are ticks * ns /us
    bit_ticks /= 1000; // now units are ticks

    const rmt_item32_t bit0 = {{{ (3 * bit_ticks) / 4, 0, bit_ticks / 4, 0 }}}; //Logical 0
    const rmt_item32_t bit1 = {{{ (3 * bit_ticks) / 4, 0, bit_ticks / 4, 1 }}}; //Logical 1
    static uint8_t window = 0;
    uint8_t outbit = 0;
    size_t num = 0;
    rmt_item32_t* pdest = dest;
    while (num < wanted_num)
    {
       // hold over from DISK][ bit stream generation
       // move this to nextbit()
       // MC34780 behavior for random bit insertion
       // https://applesaucefdc.com/woz/reference2/
      // window <<= 1;
      // window |= (uint8_t)floppy_ll.nextbit();
      // window &= 0x0f;
      // // outbit = (window != 0) ? window & 0x02 : floppy_ll.fakebit();
      // outbit = (window != 0) ? window & 0x02 : 0; // turn off random bits
      // pdest->val = (outbit != 0) ? bit1.val : bit0.val;

      pdest->val = floppy_ll.nextbit() ? bit1.val : bit0.val;

      num++;
      pdest++;
    }
    *translated_size = wanted_num;
    *item_num = wanted_num;
}

/*
 * Initialize the RMT Tx channel
 */
void mac_floppy_ll::setup_rmt()
{
#define RMT_TX_CHANNEL rmt_channel_t::RMT_CHANNEL_0
  track_buffer[0] = (uint8_t *)heap_caps_malloc(TRACK_LEN, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (track_buffer[0] == NULL)
    Debug_println("could not allocate track buffer 0");
  track_buffer[1] = (uint8_t *)heap_caps_malloc(TRACK_LEN, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (track_buffer[1] == NULL)
    Debug_println("could not allocate track buffer 1");

  config.rmt_mode = rmt_mode_t::RMT_MODE_TX;
  config.channel = RMT_TX_CHANNEL;

 config.gpio_num = (gpio_num_t)SP_WRDATA;

  config.mem_block_num = 1;
  config.tx_config.loop_en = false;
  config.tx_config.carrier_en = false;
  config.tx_config.idle_output_en = true;
  config.tx_config.idle_level = rmt_idle_level_t::RMT_IDLE_LEVEL_LOW;
  config.clk_div = 1; // use full 80 MHz resolution of APB clock

  ESP_ERROR_CHECK(fnRMT.rmt_config(&config));
  ESP_ERROR_CHECK(fnRMT.rmt_driver_install(config.channel, 0, ESP_INTR_FLAG_IRAM));
  ESP_ERROR_CHECK(fnRMT.rmt_translator_init(config.channel, encode_rmt_bitstream));
}
#endif /* NOT_IWM_LL_SUBCLASS */

bool IRAM_ATTR mac_floppy_ll::nextbit()
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

bool IRAM_ATTR mac_floppy_ll::fakebit()
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

// todo: copy both top and bottom tracks on 800k disk
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
