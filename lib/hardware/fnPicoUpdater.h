#ifndef FNPICOUPDATER_H
#define FNPICOUPDATER_H

// Keeps the Fujiversal cartridge MCU's firmware in step with the ESP32's.
//
// Its image is built alongside this one and embedded here (build_pico.py,
// fn_pico_blob.h), so one firmware zip covers both chips: the flasher
// writes the ESP32, and the next boot pushes the cartridge image over USB
// with PICOBOOT.
//
// Runs before the bus starts: the bus blocks waiting for the companion's
// CDC endpoint, which a companion in BOOTSEL never presents, and it owns
// the link once it has it.
//
// Only reflashes when the embedded image differs from what NVS records as
// last flashed. A flasher run erases NVS too, so flashing a FujiNet always
// reflashes the cartridge exactly once.

#ifdef CONFIG_USB_PICOBOOT_HOST_ENABLED

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <usb/usb_host.h>
#include <cstdint>

#include "fn_pico_blob.h"

class PicoUpdater
{
public:
    // Looks for a companion for a bounded time and flashes it if it needs
    // it, then leaves a task watching for later arrivals. Never hangs, so a
    // board with nothing attached still boots. A no-op with no image.
    void bootCheck();

    // Clears what NVS remembers, forcing a reflash next boot. For a web UI
    // control; nothing calls it yet.
    void forgetFlashedImages();

    // Public for the C forwarders usb_host and FreeRTOS need.
    void clientEvent(const usb_host_client_event_msg_t *event_msg);
    void eventTask();
    void watchTask();

private:
    enum DeviceKind : uint8_t {
        KIND_OTHER = 0,   // not ours; leave it alone
        KIND_BOOTSEL,     // an RP-series chip in BOOTSEL
        KIND_CDC,         // a running companion, reachable over CDC-ACM
    };

    struct DeviceEvent {
        bool arrived;                 // false = the device went away
        uint8_t address;
        usb_device_handle_t dev_hdl;  // only meaningful when arrived == false
    };

    usb_host_client_handle_t _client = nullptr;
    QueueHandle_t _events = nullptr;
    bool _started = false;
    bool _boot_phase = true;

    // Keeps the watcher from repeating the boot-time work.
    bool _flashed_this_boot = false;

    bool startClient();
    void scanExistingDevices();

    // Waits for a device of this kind, discarding others. -1 on timeout.
    int waitForDevice(DeviceKind wanted, uint32_t timeout_ms);

    // Waits for the first device of either kind, so a healthy cartridge is
    // not made to wait out the whole timeout. KIND_OTHER on timeout.
    DeviceKind waitForAny(uint32_t timeout_ms, uint8_t *address_out,
                          uint8_t *cdc_itf_out);

    DeviceKind classify(uint8_t address, uint8_t *chip_out, uint8_t *cdc_itf_out);

    // Erase/write/verify/reboot one attached BOOTSEL device, plus the NVS
    // bookkeeping on success.
    bool flashAttached(uint8_t address, const fn_pico_blob &blob);

    // Asks a running companion for BOOTSEL by setting 1200 baud, the pico
    // SDK convention. False if the request could not be sent.
    bool requestBootsel(uint8_t address, uint8_t cdc_itf);

    // Last resort: drive the BOOTSEL and RUN pins, which works on a hung
    // companion. False where the board has no such wiring.
    bool forceBootselViaPins();

    // Per-image record of what was last flashed.
    bool readFlashedSha(const char *name, char *out, size_t out_len);
    void writeFlashedSha(const char *name, const char *sha);

    void handleCompanion(const fn_pico_blob &blob);
};

extern PicoUpdater fnPicoUpdater;

#endif // CONFIG_USB_PICOBOOT_HOST_ENABLED

#endif // FNPICOUPDATER_H
