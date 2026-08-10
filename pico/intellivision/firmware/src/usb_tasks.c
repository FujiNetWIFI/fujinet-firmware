#include "usb_tasks.h"
#include "fatfs_disk.h"

//--------------------------------------------------------------------+
// USB CDC
//--------------------------------------------------------------------+
void cdc_task(void) {
   // connected() check for DTR bit
   // Most but not all terminal client set this when making connection
   // if ( tud_cdc_connected() )
   {
      // connected and there are data available
      if (tud_cdc_available()) {
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
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts) {
   (void) itf;
   (void) rts;

   // TODO set some indicator
   if (dtr) {
      // Terminal connected
   } else {
      // Terminal disconnected
   }
}

// Invoked when CDC interface received data from host
void tud_cdc_rx_cb(uint8_t itf) {
   (void) itf;
}

//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

#if CONFIG_FLASH_FAT_STORAGE

// Invoked when device is mounted
void tud_mount_cb(void) {
   printf("Device mounted\n");
   if (!mount_fatfs_disk())
      create_fatfs_disk();
}

// Invoked when device is unmounted
void tud_umount_cb(void) {
   printf("Device unmounted\n");
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en) {
   (void) remote_wakeup_en;
//  blink_interval_ms = BLINK_SUSPENDED;
}

// Invoked when usb bus is resumed
void tud_resume_cb(void) {
//  blink_interval_ms = BLINK_MOUNTED;
}

#endif
