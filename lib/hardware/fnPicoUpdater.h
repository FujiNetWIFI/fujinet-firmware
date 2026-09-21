#ifndef FNPICOUPDATER_H
#define FNPICOUPDATER_H

// Keeps the companion MCU's firmware in step with the ESP32's.
//
// On a Fujiversal board the console-facing half of FujiNet is an RP2040 or
// RP2350 cartridge, joined to the ESP32-S3 by USB. Its firmware is built
// alongside the ESP32's and embedded in this binary (see build_pico.py and
// fn_pico_blob.h), so one firmware zip and one flashing run cover both
// chips: the FujiNet-Flasher writes the ESP32 over the UART as it always
// has, and on the next boot this pushes the companion image across the USB
// link via PICOBOOT.
//
// It runs before the bus starts, for two reasons. The bus blocks waiting
// for the companion's USB-CDC endpoint to appear, which a companion sitting
// in BOOTSEL never presents -- so a board whose companion is blank or
// half-flashed would hang there forever. And while the bus owns the link,
// taking the companion away underneath it would be rude.
//
// The companion is only rewritten when the embedded image differs from what
// was last flashed successfully, recorded per-image in NVS. A flasher run
// erases the whole chip, NVS included, so flashing a FujiNet always
// reflashes the companion exactly once; after that, boots are quick and the
// cartridge is left alone.

#ifdef CONFIG_USB_PICOBOOT_HOST_ENABLED

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <usb/usb_host.h>
#include <cstdint>

#include "fn_pico_blob.h"

class PicoUpdater
{
public:
    // Brings up the USB host and this client, then, for a bounded time,
    // looks for a companion and flashes it if it needs flashing. Returns
    // once the companion is up to date, or once it is clear nothing more
    // can be done -- never hangs indefinitely, so a board with nothing
    // attached still boots.
    //
    // Leaves a background task watching for later arrivals: a companion
    // hot-plugged, reset into BOOTSEL by its own firmware, or put there
    // with the BOOTSEL button gets flashed without an ESP32 reboot.
    //
    // A no-op on a build with no embedded image (fn_pico_blob_count == 0).
    void bootCheck();

    // Marks every embedded image as needing a reflash on the next boot, by
    // clearing what NVS remembers. For a future "reflash the cartridge"
    // control in the web UI; nothing calls it yet.
    void forgetFlashedImages();

    // Callbacks. Public because usb_host and FreeRTOS take plain function
    // pointers, so the forwarders have to reach them from outside.
    void clientEvent(const usb_host_client_event_msg_t *event_msg);
    void eventTask();
    void watchTask();

private:
    enum DeviceKind : uint8_t {
        KIND_OTHER = 0,   // nothing we recognise; leave it alone
        KIND_BOOTSEL,     // an RP-series chip in BOOTSEL, ready to be flashed
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

    // Set once the companion has been dealt with, so the watcher task does
    // not immediately repeat the boot-time work.
    bool _flashed_this_boot = false;

    bool startClient();
    void scanExistingDevices();

    // Waits for a device of the given kind, flushing others. Returns its
    // address, or -1 on timeout.
    int waitForDevice(DeviceKind wanted, uint32_t timeout_ms);
    DeviceKind classify(uint8_t address, uint8_t *chip_out, uint8_t *cdc_itf_out);

    // The whole erase/write/verify/reboot for one already-attached BOOTSEL
    // device, plus the NVS bookkeeping on success.
    bool flashAttached(uint8_t address, const fn_pico_blob &blob);

    // Asks a running companion to reboot into BOOTSEL by setting its USB
    // serial line to 1200 baud -- the convention the pico SDK's USB stack
    // implements. Returns false if the request could not even be sent.
    bool requestBootsel(uint8_t address, uint8_t cdc_itf);

    // Last resort on boards wired for it: drive the companion's BOOTSEL and
    // RUN pins directly. Works even when its firmware is bricked or hung.
    // Returns false where the board has no such wiring.
    bool forceBootselViaPins();

    // Per-image record of the last image successfully flashed, so an
    // unchanged companion is left alone.
    bool readFlashedSha(const char *name, char *out, size_t out_len);
    void writeFlashedSha(const char *name, const char *sha);

    void handleCompanion(const fn_pico_blob &blob);
};

extern PicoUpdater fnPicoUpdater;

#endif // CONFIG_USB_PICOBOOT_HOST_ENABLED

#endif // FNPICOUPDATER_H
