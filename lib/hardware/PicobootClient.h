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
// NOT YET VALIDATED AGAINST REAL HARDWARE. Wire-format correctness rests on
// the picoboot.h headers matching what a real RP2040 bootrom expects (they
// should -- same header the SDK/picotool ship), but the ESP-IDF usb_host
// transfer sequencing here has only been reviewed against the ACMChannel/
// cdc_acm_host reference patterns, not exercised on a board. Test order per
// the design plan: (1) hardware BOOTSEL forcing brings up a 2E8A:0003
// device the host stack can see at all, before trusting any of the command
// logic below.

#ifdef CONFIG_USB_PICOBOOT_HOST_ENABLED

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <usb/usb_host.h>
#include <cstdint>
#include <cstddef>

class PicobootClient
{
public:
    // Starts the USB host client and its background event-pump task. Safe to
    // call even if no RP2040-in-BOOTSEL is attached yet -- flashBin() below
    // is what actually waits for one.
    void begin();

    // High-level: erase and write raw_bin_path (a raw .bin image, no UF2
    // wrapper) starting at flash_addr (normally 0x10000000, RP2040 XIP
    // flash base) via the FileSystem abstraction (SD or TNFS both work --
    // see fnSDFAT.file_open() pattern in fujiDevice.cpp), then reboot the
    // RP2040 back into its normal boot path. Blocks until the device
    // attaches (wait_ms timeout) and the whole transfer completes. Returns
    // false on any failure; a partial/failed flash is expected to be
    // recoverable via hardware BOOTSEL forcing (see pinmap), never fatal.
    bool flashBin(const char *raw_bin_path, uint32_t flash_addr, uint32_t wait_ms);

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
