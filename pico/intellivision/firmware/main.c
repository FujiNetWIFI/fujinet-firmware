/*
//                       PiRTO II Flash MultiCART by Andrea Ottaviani 2024
//
//  Intellivision flash multicart based on Raspberry Pico board -
//
//  More info on https://github.com/aotta/ 
//
//   parts of code are directly from the A8PicoCart project by Robin Edwards 2023
//  
//   Needs to be a release NOT debug build for the cartridge emulation to work
// 
//   Edit myboard.h depending on the type of flash memory on the pico clone//
//
//   v. 1.0 2024-05-04 : Initial version for Pi Pico 
//
*/



#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/time.h"
#include "tusb.h"

#include "inty_cart.h"
#include "fujibus_usb.h"

//void cdc_task(void);

//--------------------------------------------------------------------+
// FujiNet bring-up self-test (headless, no Intellivision needed)
//
// Runs only in USB-MSC mode (MSYNC low / console off, see main()) -- that's
// already the branch that pumps tud_task(), so it's the natural place to
// validate the USB link to the ESP32-S3 before the cart-mode mailbox
// (added separately) exists to drive it from the Intellivision side.
//--------------------------------------------------------------------+
#define FUJI_SELFTEST_TRIGGER_PIN 22 // pulled up; grounding it fires a live transact

static void fuji_hexdump(const uint8_t *buf, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        printf("%02X ", buf[i]);
        if ((i & 0xF) == 0xF)
            printf("\n");
    }
    if (len & 0xF)
        printf("\n");
}

static void fuji_selftest_init(void)
{
    gpio_init(FUJI_SELFTEST_TRIGGER_PIN);
    gpio_set_dir(FUJI_SELFTEST_TRIGGER_PIN, GPIO_IN);
    gpio_pull_up(FUJI_SELFTEST_TRIGGER_PIN);

    printf("fujibus codec self-test: %s\n", fujibus_selftest() ? "PASS" : "FAIL");
}

// Call every iteration of the MSC tud_task() loop. One-shot per press:
// requires the pin to go high again before it will re-fire.
static void fuji_selftest_poll(void)
{
    static bool armed = true;
    static uint8_t rxbuf[640];

    if (gpio_get(FUJI_SELFTEST_TRIGGER_PIN) != 0) {
        armed = true;
        return;
    }
    if (!armed)
        return;
    armed = false;

    printf("fujibus: GET_ADAPTERCONFIG_EXTENDED -> device 0x%02X...\n", FUJI_DEVICEID_FUJINET);
    fb_reply_t reply;
    fb_status_t st = fujibus_transact(FUJI_DEVICEID_FUJINET, FUJICMD_GET_ADAPTERCONFIG_EXTENDED,
                                       NULL, 0, NULL, 0,
                                       rxbuf, sizeof(rxbuf), &reply, 5000);
    switch (st) {
    case FB_OK:
        printf("reply: device=0x%02X command=0x%02X (%s) data_len=%u\n",
               reply.device, reply.command,
               reply.command == FUJICMD_ACK ? "ACK" : "NAK", reply.data_len);
        fuji_hexdump(reply.data, reply.data_len);
        break;
    case FB_ENOLINK:
        printf("no USB link to the ESP32-S3 (not enumerated / no DTR)\n");
        break;
    case FB_ETIMEOUT:
        printf("timed out waiting for a reply\n");
        break;
    case FB_EBADFRAME:
        printf("reply failed SLIP/length/checksum validation\n");
        break;
    default:
        printf("transact error %d\n", (int)st);
        break;
    }
}


//--------------------------------------------------------------------+
// USB CDC
//--------------------------------------------------------------------+
void cdc_task(void)
{
  // connected() check for DTR bit
  // Most but not all terminal client set this when making connection
  // if ( tud_cdc_connected() )
  {
    // connected and there are data available
    if ( tud_cdc_available() )
    {
      // read data
      char buf[64];
      uint32_t count = tud_cdc_read(buf, sizeof(buf));
      (void) count;

      // Echo back
      // Note: Skip echo by commenting out write() and write_flush()
      // for throughput test e.g
      //    $ dd if=/dev/zero of=/dev/ttyACM0 count=10000
      tud_cdc_write(buf, count);
      tud_cdc_write_flush();
    }
  }
}

// Invoked when cdc when line state changed e.g connected/disconnected
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
  (void) itf;
  (void) rts;

  // TODO set some indicator
  if ( dtr )
  {
    // Terminal connected
  }else
  {
    // Terminal disconnected
  }
}

// Invoked when CDC interface received data from host
void tud_cdc_rx_cb(uint8_t itf)
{
  (void) itf;
}


int main(void)
{
    // UART0/GPIO28 debug console at the default clock. Cart mode overclocks
    // to 270 MHz in Inty_cart_main() and refreshes the baud divisor there;
    // MSC mode never changes the clock, so this is all it needs.
    stdio_init_all();

    tusb_init();

    tud_init(BOARD_TUD_RHPORT);
    gpio_init(MSYNC_PIN);
    gpio_set_dir(MSYNC_PIN,false);
    gpio_init(RST_PIN);
    gpio_set_dir( RST_PIN,true);
    //gpio_put(RST_PIN,true);
    sleep_ms(2);

    fuji_selftest_init();

    while(1) {

    while (to_ms_since_boot(get_absolute_time()) < 200)
    {
      if ((gpio_get(MSYNC_PIN)==1))
   //      gpio_put(RST_PIN,false);
        Inty_cart_main();
    }


       // init device stack on configured roothub port
       // we are presumably powered from USB
       // enter USB mass storage mode
     //  tud_init(BOARD_TUD_RHPORT);

       while (1) {
          tud_task(); // tinyusb device task
          fuji_selftest_poll(); // ground GPIO22 to fire a live FujiBus transact
       }

    }
   return 0;
}

//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void)
{
  printf("Device mounted\n");
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
  printf("Device unmounted\n");  
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en)
{
  (void) remote_wakeup_en;
//  blink_interval_ms = BLINK_SUSPENDED;
}

// Invoked when usb bus is resumed
void tud_resume_cb(void)
{
//  blink_interval_ms = BLINK_MOUNTED;
}

