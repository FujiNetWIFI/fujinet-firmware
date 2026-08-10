#ifndef PICOBOOTCLIENT_H
#define PICOBOOTCLIENT_H

// PICOBOOT USB host client: reflashes an RP2040 sitting in BOOTSEL mode over
// the ESP32-S3's USB host port, without picotool/libusb and without a
// second usb_host_install() (ACMChannel already owns that -- see its
// usb_lib_task comment). This is the PiRTOII-Fuji board's RP2040-flashing
// path now that there's no external RP2040 USB port: the RP2040 enters
// BOOTSEL either cooperatively (FUJI_MB_BOOTSEL_DOORBELL in
// pico/intellivision/firmware/fuji_mailbox.h, serviced by
// fuji_mailbox_service() -> reset_usb_boot()) or via hardware forcing
// (PIN_RP2040_RUN / PIN_RP2040_BOOTSEL in pinmap/fujiversal-intv.h, for
// recovery when the RP2040 firmware is bricked or hung).
//
// Protocol is PICOBOOT as documented in the RP2040 bootrom and implemented
// by picotool -- see /usr/share/pico-sdk/src/common/boot_picoboot_headers
// and picotool's picoboot_connection/picoboot_connection.c, which this is a
// direct ESP-IDF usb_host port of (that reference uses libusb; this uses
// usb_host_client_register()+usb_host_transfer_submit() instead). Command
// packet layout, endpoint discovery rule (interface 0 if the device has
// exactly one interface, else interface 1; vendor class 0xFF, 2 bulk
// endpoints), and the "data phase then 1-byte-capacity ACK phase in the
// opposite direction" sequencing are all taken from that source, not
// reverse-engineered -- see boot/picoboot.h for the exact struct layouts.
//
// Validated end-to-end on real hardware: a manually-BOOTSEL'd RP2040
// attaches, gets erased/written/rebooted, and comes back up running the
// flashed image. forceReflash() below extends that to RP2040s currently
// running normal firmware, with no physical BOOTSEL button press needed.

#ifdef CONFIG_USB_PICOBOOT_HOST_ENABLED

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <usb/usb_host.h>
#include <cstdint>
#include <cstddef>

class ACMChannel; // lib/hardware/ACMChannel.h -- only used as a pointer here

class PicobootClient
{
public:
    // Starts the USB host client and its background event-pump task. Safe to
    // call even if no RP2040-in-BOOTSEL is attached yet -- flashBin() below
    // is what actually waits for one.
    void begin();

    // Gives forceReflash() below a way to ask the RP2040 (while it's running
    // normal firmware) to reboot into BOOTSEL, via ACMChannel's
    // triggerBootselTouch(). Set once from rs232.cpp's systemBus::setup(),
    // which owns the ACMChannel instance (_serial) this needs a pointer to.
    void setAcmChannel(ACMChannel *acm) { _acm = acm; }

    // Full automatic reflash cycle, no physical BOOTSEL button needed: asks
    // the RP2040 (assumed currently running normal fuji_intv firmware, via
    // ACMChannel's 1200-baud touch -- see setAcmChannel()) to reboot into
    // BOOTSEL, then flashEmbedded()'s normal wait-for-attach-and-flash
    // takes over. If no ACMChannel was set, or the touch request itself
    // fails (e.g. nothing currently attached), still falls through to
    // flashEmbedded()'s wait -- covers the case where the RP2040 was
    // already sitting in BOOTSEL (physical button, hardware forcing, or a
    // previous mailbox doorbell) when this is called.
    bool forceReflash(uint32_t flash_addr, uint32_t wait_ms);

    // High-level: erase and write the RP2040 firmware that's linked directly
    // into this ESP32-S3 binary (see build_pico_intv.py + src/CMakeLists.txt
    // -- fuji_intv.bin, wrapped by objcopy into a .o and linked in, exposing
    // _binary_fuji_intv_bin_start/_end/_size) starting at flash_addr
    // (normally 0x10000000, RP2040 XIP flash base), then reboot the RP2040
    // back into its normal boot path. Blocks until the device attaches
    // (wait_ms timeout) and the whole transfer completes. Returns false on
    // any failure; a partial/failed flash is expected to be recoverable via
    // hardware BOOTSEL forcing (see pinmap), never fatal.
    //
    // Deliberately not filesystem-based (an earlier SD-card-backed flashBin()
    // was replaced by this): the SD read intermittently timed out
    // (sdmmc_send_cmd ESP_ERR_TIMEOUT) right at BOOTSEL attach, almost
    // certainly racing the USB host's own enumeration interrupt activity.
    // Reading the image out of this binary's own .rodata sidesteps that
    // whole class of problem -- no filesystem, no SPI transaction, nothing
    // to race, at the moment of flashing.
    bool flashEmbedded(uint32_t flash_addr, uint32_t wait_ms);

    // Public because the C-linkage forwarder functions need them.
    void clientEvent(const usb_host_client_event_msg_t *event_msg);
    void xferDone(usb_transfer_t *transfer);

private:
    usb_host_client_handle_t _client_hdl = nullptr;
    usb_device_handle_t _dev_hdl = nullptr;
    uint8_t _interface = 0;
    uint8_t _out_ep = 0, _in_ep = 0;
    uint16_t _in_ep_mps = 64; // RP2040 BOOTSEL is full-speed only -> bulk MPS is
                              // always 64 by spec; captured from the endpoint
                              // descriptor anyway rather than assumed outright.
    SemaphoreHandle_t _device_ready_sem = nullptr;
    ACMChannel *_acm = nullptr; // see setAcmChannel()/forceReflash()

    usb_transfer_t *_cmd_xfer = nullptr;   // 32B picoboot_cmd
    usb_transfer_t *_data_xfer = nullptr;  // up to one flash sector (4096B)
    usb_transfer_t *_ack_xfer = nullptr;   // capacity 64B; device sends a ZLP, but
                                            // ESP-IDF's usb_host requires non-control
                                            // IN requests to be an exact multiple of
                                            // the endpoint's MPS -- unlike libusb,
                                            // which tolerates a 1-byte ACK request.
    SemaphoreHandle_t _xfer_done_sem = nullptr;

    void newDevice(uint8_t dev_addr);
    void deviceGone();

    // Submits transfer and blocks (via _xfer_done_sem, signaled by
    // xferDone()) until it completes or times out. Returns the transfer's
    // usb_transfer_status_t.
    int submitAndWait(usb_transfer_t *transfer, uint32_t timeout_ms);

    // One PICOBOOT command round-trip: send the 32B command, then the data
    // phase (buffer/len, direction implied by cmd_id's top bit), then the
    // 1-byte-capacity ACK phase in the opposite direction. Returns true on
    // success (a zero-length ACK arrived, i.e. the device didn't stall).
    bool cmd(uint8_t cmd_id, uint8_t cmd_size, const void *args, uint32_t transfer_length,
              uint8_t *buffer);

    bool exclusiveAccess(uint8_t exclusive);
    bool exitXip();
    bool flashErase(uint32_t addr, uint32_t len);
    bool flashWrite(uint32_t addr, const uint8_t *buffer, uint32_t len);
    bool reboot(uint32_t pc, uint32_t sp, uint32_t delay_ms);
};

extern PicobootClient picobootClient;

#endif /* CONFIG_USB_PICOBOOT_HOST_ENABLED */

#endif /* PICOBOOTCLIENT_H */
