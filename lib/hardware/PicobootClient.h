#ifndef PICOBOOTCLIENT_H
#define PICOBOOTCLIENT_H

// PICOBOOT: the USB protocol an RP-series bootrom speaks while the chip sits
// in BOOTSEL mode. This is enough of it to erase, write, verify and reboot
// the companion MCU's flash, ported from picotool's picoboot_connection.c
// onto ESP-IDF's usb_host API (no libusb, no picotool).
//
// This class is protocol only: it never opens a device, registers a client,
// or decides when to flash anything. PicoUpdater owns all of that and hands
// an already-opened, already-claimed device here via bind(). That split is
// what lets one usb_host client serve both the discovery/policy side and
// this, and keeps the USB bookkeeping in one place.
//
// Chip differences that matter and are handled here: RP2040 and RP2350
// enumerate in BOOTSEL under different product IDs, and reboot with
// different commands (PC_REBOOT vs PC_REBOOT2). RP2354 is an RP2350.

#ifdef CONFIG_USB_PICOBOOT_HOST_ENABLED

#include <usb/usb_host.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <cstdint>

#include "fn_pico_blob.h"

// The RP-series BOOTSEL vendor ID, and the product IDs per family. Taken
// from picotool's own udev rules (0003 = RP2040, 000f = RP2350/RP2354); the
// other two IDs in that file, 0009/000a, are what an application built with
// pico_stdio_usb enumerates as and are deliberately NOT matched here.
#define PICOBOOT_USB_VID        0x2E8Au
#define PICOBOOT_USB_PID_RP2040 0x0003u
#define PICOBOOT_USB_PID_RP2350 0x000Fu

// Erase granularity on every RP-series part, and the chunk size used for
// write and verify. Mirrored as PICO_FLASH_SECTOR_SIZE in build_pico.py,
// which bounds-checks images against pico_flash_limit with it.
#define PICOBOOT_FLASH_SECTOR_SIZE 4096u

class PicobootClient
{
public:
    // Progress during a flash, so a user watching the serial log sees
    // something move. stage is "write" or "verify"; pct is 0-100.
    typedef void (*progress_fn)(const char *stage, int pct, void *arg);

    // Allocates the USB transfers. Call once, before any bind().
    bool init();

    // --- device identification, used before binding -------------------

    // True if this descriptor is an RP-series chip sitting in BOOTSEL.
    // Fills chip_out with FN_PICO_CHIP_RP2040 / FN_PICO_CHIP_RP2350.
    static bool isBootselDevice(const usb_device_desc_t *desc, uint8_t *chip_out);

    // Locates the PICOBOOT vendor interface and its two bulk endpoints.
    // The bootrom puts PICOBOOT on interface 1 of a stock composite device
    // (mass storage takes interface 0), or on interface 0 once mass storage
    // has been hidden -- the same rule picotool uses.
    static bool findInterface(const usb_config_desc_t *config_desc,
                              uint8_t *itf_out, uint8_t *out_ep_out,
                              uint8_t *in_ep_out, uint16_t *in_mps_out);

    // --- binding ------------------------------------------------------

    // Adopt a device the caller has opened and whose interface it has
    // claimed. The caller keeps ownership: it must unbind() before
    // releasing or closing.
    void bind(usb_host_client_handle_t client_hdl, usb_device_handle_t dev_hdl,
              uint8_t interface, uint8_t out_ep, uint8_t in_ep,
              uint16_t in_mps, uint8_t chip);
    void unbind();
    bool bound() const { return _dev_hdl != nullptr; }

    // --- the whole job ------------------------------------------------

    // Erase, write, verify and reboot into the freshly written image.
    // Refuses an image whose chip does not match the attached device, or
    // one that would run past blob.flash_limit.
    //
    // On failure returns false and leaves a description in lastError(); the
    // companion is then left sitting in BOOTSEL, which is recoverable (the
    // next boot, or a hot-plug, tries again) rather than bricked.
    bool flashImage(const fn_pico_blob &blob, progress_fn progress, void *progress_arg);

    // Human-readable reason the last flashImage() failed. Empty on success.
    const char *lastError() const { return _last_error; }

    // Transfer-complete callback. Public only because usb_transfer_t takes a
    // plain C function pointer, so the forwarder has to reach it from
    // outside the class.
    void xferDone(usb_transfer_t *transfer);

private:
    usb_host_client_handle_t _client_hdl = nullptr;
    usb_device_handle_t _dev_hdl = nullptr;
    uint8_t _interface = 0;
    uint8_t _out_ep = 0;
    uint8_t _in_ep = 0;
    // BOOTSEL is full speed, so this is 64 by spec, but it is read from the
    // descriptor anyway -- ESP-IDF rejects a bulk IN request that is not a
    // multiple of it, so a wrong value here fails every read.
    uint16_t _in_mps = 64;
    uint8_t _chip = FN_PICO_CHIP_UNKNOWN;

    SemaphoreHandle_t _xfer_done_sem = nullptr;
    usb_transfer_t *_cmd_xfer = nullptr;   // 32B: one picoboot_cmd
    usb_transfer_t *_data_xfer = nullptr;  // one flash sector
    usb_transfer_t *_ack_xfer = nullptr;   // the zero-length ack
    usb_transfer_t *_ctrl_xfer = nullptr;  // setup packet + 16B status

    // Staging for the image's final, partial sector, which has to be padded
    // out to a whole sector before it can be written. Whole sectors go
    // straight from the embedded image, so this is touched once per flash.
    uint8_t _tail_sector[PICOBOOT_FLASH_SECTOR_SIZE];

    char _last_error[160] = {0};

    void setError(const char *fmt, ...) __attribute__((format(printf, 2, 3)));

    int submitAndWait(usb_transfer_t *transfer, uint32_t timeout_ms, bool control);
    bool cmd(uint8_t cmd_id, uint8_t cmd_size, const void *args,
             uint32_t transfer_length, uint8_t *buffer);

    // Control requests on the PICOBOOT interface itself, outside the
    // command stream: clear a stalled endpoint, and read back why the last
    // command failed.
    bool ifReset();
    bool cmdStatus(uint32_t *status_code_out);

    bool exclusiveAccess(uint8_t exclusive);
    bool exitXip();
    bool enterCmdXip();
    bool flashErase(uint32_t addr, uint32_t len);
    bool flashWrite(uint32_t addr, const uint8_t *buffer, uint32_t len);
    bool flashRead(uint32_t addr, uint8_t *buffer, uint32_t len);
    bool reboot();

    // Reports the bootrom's own status code for a failed command, so a
    // failure says "bad alignment" rather than just "it stalled".
    void describeFailure(const char *stage, uint32_t addr);
};

extern PicobootClient picobootClient;

#endif // CONFIG_USB_PICOBOOT_HOST_ENABLED

#endif // PICOBOOTCLIENT_H
