#ifndef PICOBOOTCLIENT_H
#define PICOBOOTCLIENT_H

// PICOBOOT, the protocol an RP-series bootrom speaks in BOOTSEL mode.
// Ported from picotool's picoboot_connection.c onto ESP-IDF usb_host.
//
// Protocol only: PicoUpdater owns the device and hands it over via bind(),
// so one usb_host client serves both discovery and flashing.

#ifdef CONFIG_USB_PICOBOOT_HOST_ENABLED

#include <usb/usb_host.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <cstdint>

#include "fn_pico_blob.h"

// From picotool's udev rules. 0009/000a (pico_stdio_usb apps) are not BOOTSEL.
#define PICOBOOT_USB_VID        0x2E8Au
#define PICOBOOT_USB_PID_RP2040 0x0003u
#define PICOBOOT_USB_PID_RP2350 0x000Fu

// Erase granularity; mirrored as PICO_FLASH_SECTOR_SIZE in build_pico.py.
#define PICOBOOT_FLASH_SECTOR_SIZE 4096u

class PicobootClient
{
public:
    // stage is "write" or "verify"; pct is 0-100.
    typedef void (*progress_fn)(const char *stage, int pct, void *arg);

    // Allocates the USB transfers. Call once, before any bind().
    bool init();

    // --- device identification, used before binding -------------------

    // True for an RP-series chip in BOOTSEL; fills chip_out.
    static bool isBootselDevice(const usb_device_desc_t *desc, uint8_t *chip_out);

    // Locates the PICOBOOT vendor interface and its two bulk endpoints.
    static bool findInterface(const usb_config_desc_t *config_desc,
                              uint8_t *itf_out, uint8_t *out_ep_out,
                              uint8_t *in_ep_out, uint16_t *in_mps_out);

    // --- binding ------------------------------------------------------

    // Adopt a device the caller opened and claimed; it must unbind()
    // before releasing or closing.
    void bind(usb_host_client_handle_t client_hdl, usb_device_handle_t dev_hdl,
              uint8_t interface, uint8_t out_ep, uint8_t in_ep,
              uint16_t in_mps, uint8_t chip);
    void unbind();
    bool bound() const { return _dev_hdl != nullptr; }

    // --- the whole job ------------------------------------------------

    // Erase, write, verify and reboot into the new image. Refuses a chip
    // mismatch or an image past blob.flash_limit. On failure the companion
    // is left in BOOTSEL, which the next boot recovers from.
    bool flashImage(const fn_pico_blob &blob, progress_fn progress, void *progress_arg);

    // Why the last flashImage() failed. Empty on success.
    const char *lastError() const { return _last_error; }

    // Public for the C forwarder usb_transfer_t::callback needs.
    void xferDone(usb_transfer_t *transfer);

private:
    usb_host_client_handle_t _client_hdl = nullptr;
    usb_device_handle_t _dev_hdl = nullptr;
    uint8_t _interface = 0;
    uint8_t _out_ep = 0;
    uint8_t _in_ep = 0;
    // Read from the descriptor: ESP-IDF rejects a bulk IN request that is
    // not a multiple of it.
    uint16_t _in_mps = 64;
    uint8_t _chip = FN_PICO_CHIP_UNKNOWN;

    SemaphoreHandle_t _xfer_done_sem = nullptr;
    usb_transfer_t *_cmd_xfer = nullptr;   // 32B: one picoboot_cmd
    usb_transfer_t *_data_xfer = nullptr;  // one flash sector
    usb_transfer_t *_ack_xfer = nullptr;   // the zero-length ack
    usb_transfer_t *_ctrl_xfer = nullptr;  // setup packet + 16B status

    // Pads the image's final partial sector; whole sectors go straight
    // from the embedded image.
    uint8_t _tail_sector[PICOBOOT_FLASH_SECTOR_SIZE];

    char _last_error[160] = {0};

    void setError(const char *fmt, ...) __attribute__((format(printf, 2, 3)));

    int submitAndWait(usb_transfer_t *transfer, uint32_t timeout_ms, bool control);
    bool cmd(uint8_t cmd_id, uint8_t cmd_size, const void *args,
             uint32_t transfer_length, uint8_t *buffer);

    // Control requests outside the command stream.
    bool ifReset();
    bool cmdStatus(uint32_t *status_code_out);

    bool exclusiveAccess(uint8_t exclusive);
    bool exitXip();
    bool enterCmdXip();
    bool flashErase(uint32_t addr, uint32_t len);
    bool flashWrite(uint32_t addr, const uint8_t *buffer, uint32_t len);
    bool flashRead(uint32_t addr, uint8_t *buffer, uint32_t len);
    bool reboot();

    // Appends the bootrom's own status code to the error.
    void describeFailure(const char *stage, uint32_t addr);
};

extern PicobootClient picobootClient;

#endif // CONFIG_USB_PICOBOOT_HOST_ENABLED

#endif // PICOBOOTCLIENT_H
