#include "fnPicoUpdater.h"

#ifdef CONFIG_USB_PICOBOOT_HOST_ENABLED

#include <usb/usb_helpers.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <cstring>
#include <cstdio>

#include "fnUsbHost.h"
#include "PicobootClient.h"
#include "fnSystem.h"
#include "../../include/pinmap.h"
#include "../../include/debug.h"

PicoUpdater fnPicoUpdater;

// Every line this prints carries this prefix. The FujiNet-Flasher shows the
// serial log straight after it finishes writing the ESP32, so these lines
// are what tells someone watching that a second chip is being flashed and
// that the board should not be unplugged yet.
#define PICOFW "PICOFW: "

// How long to wait, at boot, for a companion to show up at all. Long enough
// to cover USB enumeration from cold (debounce alone is 250ms), short
// enough not to punish a board that has none attached.
#define WAIT_FIRST_DEVICE_MS 3000

// After asking a companion to reboot into BOOTSEL, how long to wait for it
// to come back as a BOOTSEL device.
#define WAIT_BOOTSEL_MS 5000

// After flashing, how long to wait for the companion to reappear running
// its new firmware. Missing this is a warning, not a failure -- the bus is
// about to start and will wait for it anyway.
#define WAIT_REATTACH_MS 5000

#define NVS_NAMESPACE "picofw"

// Depth 8: enumeration can produce a small burst (a device leaving and
// arriving), and the boot path is not always draining.
#define EVENT_QUEUE_DEPTH 8

#define SHA_HEX_LEN 64

// --- USB CDC class constants, for spotting a running companion and asking
// it to reboot. Spelled out from the USB CDC spec rather than taken from
// the CDC-ACM host driver's headers, so this file does not depend on a
// component a board might not build.
#define CDC_SUBCLASS_ACM        0x02  // abstract control model
#define CDC_REQ_SET_LINE_CODING 0x20
#define CDC_REQ_TYPE_OUT        0x21  // host-to-device, class, interface
#define CDC_LINE_CODING_LEN     7
// The baud rate that means "reboot into the bootloader" to a pico SDK USB
// stack (PICO_USB_RESET_MAGIC_BAUD_RATE).
#define CDC_BOOTSEL_BAUD        1200

// ---------------------------------------------------------------------------
// Callback forwarders
// ---------------------------------------------------------------------------

static void clientEventForwarder(const usb_host_client_event_msg_t *event_msg, void *arg)
{
    ((PicoUpdater *)arg)->clientEvent(event_msg);
}

static void eventTaskForwarder(void *arg)
{
    ((PicoUpdater *)arg)->eventTask();
}

static void watchTaskForwarder(void *arg)
{
    ((PicoUpdater *)arg)->watchTask();
}

static void progressForwarder(const char *stage, int pct, void *arg)
{
    Debug_printf(PICOFW "%s %d%%\r\n", stage, pct);
}

void PicoUpdater::clientEvent(const usb_host_client_event_msg_t *event_msg)
{
    // Runs on the event task, which is also what delivers transfer
    // completions -- so this only ever posts to a queue. Opening devices or
    // flashing from here would deadlock against the very task that has to
    // service the transfers.
    DeviceEvent ev = {};
    switch (event_msg->event) {
    case USB_HOST_CLIENT_EVENT_NEW_DEV:
        ev.arrived = true;
        ev.address = event_msg->new_dev.address;
        break;
    case USB_HOST_CLIENT_EVENT_DEV_GONE:
        ev.arrived = false;
        ev.dev_hdl = event_msg->dev_gone.dev_hdl;
        break;
    default:
        return;
    }
    xQueueSend(_events, &ev, 0);
}

void PicoUpdater::eventTask()
{
    while (true)
        usb_host_client_handle_events(_client, portMAX_DELAY);
}

// ---------------------------------------------------------------------------
// NVS bookkeeping
// ---------------------------------------------------------------------------

bool PicoUpdater::readFlashedSha(const char *name, char *out, size_t out_len)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK)
        return false; // namespace absent: nothing has ever been flashed

    size_t len = out_len;
    esp_err_t err = nvs_get_str(handle, name, out, &len);
    nvs_close(handle);
    return err == ESP_OK;
}

void PicoUpdater::writeFlashedSha(const char *name, const char *sha)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        Debug_printf(PICOFW "WARN could not open NVS to record the flash (%s); "
                     "the companion will be reflashed next boot\r\n",
                     esp_err_to_name(err));
        return;
    }
    if (nvs_set_str(handle, name, sha) != ESP_OK || nvs_commit(handle) != ESP_OK)
        Debug_printf(PICOFW "WARN could not record the flash in NVS; the "
                     "companion will be reflashed next boot\r\n");
    nvs_close(handle);
}

void PicoUpdater::forgetFlashedImages()
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK)
        return;
    nvs_erase_all(handle);
    nvs_commit(handle);
    nvs_close(handle);
    Debug_printf(PICOFW "cleared the flashed-image record; the companion will "
                 "be reflashed on the next boot\r\n");
}

// ---------------------------------------------------------------------------
// Device discovery and classification
// ---------------------------------------------------------------------------

bool PicoUpdater::startClient()
{
    if (_started)
        return true;

    _events = xQueueCreate(EVENT_QUEUE_DEPTH, sizeof(DeviceEvent));
    if (!_events)
        return false;

    if (!picobootClient.init()) {
        Debug_printf(PICOFW "FAIL could not allocate USB transfers\r\n");
        return false;
    }

    // Must come before registering, and as early as possible: a device
    // already attached at power-on can finish enumerating before a late
    // client is listening. scanExistingDevices() covers the rest.
    usbHostEnsureInstalled(FN_USB_HOST_BOOT_PRIORITY);

    usb_host_client_config_t config = {};
    config.is_synchronous = false;
    config.max_num_event_msg = EVENT_QUEUE_DEPTH;
    config.async.client_event_callback = clientEventForwarder;
    config.async.callback_arg = this;
    esp_err_t err = usb_host_client_register(&config, &_client);
    if (err != ESP_OK) {
        Debug_printf(PICOFW "FAIL usb_host_client_register: %s\r\n",
                     esp_err_to_name(err));
        return false;
    }

    // Priority 18 matches the other USB service tasks: high enough to keep
    // up with the host controller while WiFi is associating.
    if (xTaskCreate(eventTaskForwarder, "picofw-evt", 5120, this, 18, NULL) != pdTRUE) {
        Debug_printf(PICOFW "FAIL could not start the USB event task\r\n");
        usb_host_client_deregister(_client);
        _client = nullptr;
        return false;
    }

    _started = true;
    return true;
}

void PicoUpdater::scanExistingDevices()
{
    // NEW_DEV is sent when a device enumerates and is never replayed, so a
    // companion that came up before this client registered would otherwise
    // be invisible. Treat anything already on the bus as having just
    // arrived.
    uint8_t addresses[8];
    int count = 0;
    if (usb_host_device_addr_list_fill(sizeof(addresses), addresses, &count) != ESP_OK)
        return;

    for (int i = 0; i < count; i++) {
        DeviceEvent ev = {};
        ev.arrived = true;
        ev.address = addresses[i];
        xQueueSend(_events, &ev, 0);
    }
    if (count)
        Debug_printf(PICOFW "%d device(s) already attached\r\n", count);
}

PicoUpdater::DeviceKind PicoUpdater::classify(uint8_t address, uint8_t *chip_out,
                                               uint8_t *cdc_itf_out)
{
    usb_device_handle_t dev_hdl;
    if (usb_host_device_open(_client, address, &dev_hdl) != ESP_OK)
        return KIND_OTHER;

    const usb_device_desc_t *dev_desc = nullptr;
    usb_host_get_device_descriptor(dev_hdl, &dev_desc);

    DeviceKind kind = KIND_OTHER;
    uint8_t chip = FN_PICO_CHIP_UNKNOWN;

    if (PicobootClient::isBootselDevice(dev_desc, &chip)) {
        kind = KIND_BOOTSEL;
    } else {
        // Anything presenting a CDC-ACM function is treated as a running
        // companion. The same test ACMChannel uses, so the two agree on
        // what the bus link looks like.
        const usb_config_desc_t *config_desc = nullptr;
        if (usb_host_get_active_config_descriptor(dev_hdl, &config_desc) == ESP_OK &&
            config_desc != nullptr)
        {
            int offset = 0;
            const usb_standard_desc_t *desc = (const usb_standard_desc_t *)config_desc;
            uint16_t total_len = config_desc->wTotalLength;
            while ((desc = usb_parse_next_descriptor_of_type(
                        desc, total_len, USB_B_DESCRIPTOR_TYPE_INTERFACE_ASSOCIATION,
                        &offset)) != NULL)
            {
                const usb_iad_desc_t *iad = (const usb_iad_desc_t *)desc;
                if (iad->bFunctionClass == USB_CLASS_COMM &&
                    iad->bFunctionSubClass == CDC_SUBCLASS_ACM)
                {
                    kind = KIND_CDC;
                    if (cdc_itf_out)
                        *cdc_itf_out = iad->bFirstInterface;
                    break;
                }
            }
        }
    }

    if (kind != KIND_OTHER && dev_desc != nullptr)
        Debug_printf(PICOFW "device %04X:%04X is %s\r\n",
                     dev_desc->idVendor, dev_desc->idProduct,
                     kind == KIND_BOOTSEL ? "in BOOTSEL" : "a running companion");

    usb_host_device_close(_client, dev_hdl);

    if (chip_out)
        *chip_out = chip;
    return kind;
}

int PicoUpdater::waitForDevice(DeviceKind wanted, uint32_t timeout_ms)
{
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    while (true) {
        TickType_t now = xTaskGetTickCount();
        if (now >= deadline)
            return -1;

        DeviceEvent ev;
        if (xQueueReceive(_events, &ev, deadline - now) != pdTRUE)
            return -1;
        if (!ev.arrived)
            continue;

        uint8_t chip = FN_PICO_CHIP_UNKNOWN;
        uint8_t cdc_itf = 0;
        if (classify(ev.address, &chip, &cdc_itf) == wanted)
            return (int)ev.address;
    }
}

PicoUpdater::DeviceKind PicoUpdater::waitForAny(uint32_t timeout_ms,
                                                 uint8_t *address_out,
                                                 uint8_t *cdc_itf_out)
{
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    while (true) {
        TickType_t now = xTaskGetTickCount();
        if (now >= deadline)
            return KIND_OTHER;

        DeviceEvent ev;
        if (xQueueReceive(_events, &ev, deadline - now) != pdTRUE)
            return KIND_OTHER;
        if (!ev.arrived)
            continue;

        uint8_t chip = FN_PICO_CHIP_UNKNOWN;
        DeviceKind kind = classify(ev.address, &chip, cdc_itf_out);
        if (kind != KIND_OTHER) {
            if (address_out)
                *address_out = ev.address;
            return kind;
        }
    }
}

// ---------------------------------------------------------------------------
// Getting a running companion into BOOTSEL
// ---------------------------------------------------------------------------

bool PicoUpdater::requestBootsel(uint8_t address, uint8_t cdc_itf)
{
    usb_device_handle_t dev_hdl;
    if (usb_host_device_open(_client, address, &dev_hdl) != ESP_OK) {
        Debug_printf(PICOFW "could not open the companion to ask it for BOOTSEL\r\n");
        return false;
    }

    usb_transfer_t *xfer = nullptr;
    if (usb_host_transfer_alloc(USB_SETUP_PACKET_SIZE + CDC_LINE_CODING_LEN, 0, &xfer) != ESP_OK) {
        usb_host_device_close(_client, dev_hdl);
        return false;
    }

    // SET_LINE_CODING with a baud rate of 1200. A pico SDK USB stack takes
    // that as a request to reboot into BOOTSEL and does so from inside the
    // request handler, so the status stage may never come back -- which is
    // why the result below is logged rather than trusted.
    usb_setup_packet_t *setup = (usb_setup_packet_t *)xfer->data_buffer;
    setup->bmRequestType = CDC_REQ_TYPE_OUT;
    setup->bRequest = CDC_REQ_SET_LINE_CODING;
    setup->wValue = 0;
    setup->wIndex = cdc_itf;
    setup->wLength = CDC_LINE_CODING_LEN;

    uint8_t *coding = xfer->data_buffer + USB_SETUP_PACKET_SIZE;
    uint32_t baud = CDC_BOOTSEL_BAUD;
    memcpy(coding, &baud, 4);   // dwDTERate, little endian
    coding[4] = 0;              // bCharFormat: 1 stop bit
    coding[5] = 0;              // bParityType: none
    coding[6] = 8;              // bDataBits

    xfer->device_handle = dev_hdl;
    xfer->bEndpointAddress = 0;
    xfer->num_bytes = USB_SETUP_PACKET_SIZE + CDC_LINE_CODING_LEN;
    xfer->timeout_ms = 1000;
    xfer->context = nullptr;
    xfer->callback = [](usb_transfer_t *t) { /* result is not waited on */ };

    esp_err_t err = usb_host_transfer_submit_control(_client, xfer);
    if (err != ESP_OK)
        Debug_printf(PICOFW "the BOOTSEL request could not be sent (%s)\r\n",
                     esp_err_to_name(err));

    // Give the request a moment to go out before the handle is dropped.
    vTaskDelay(pdMS_TO_TICKS(100));

    // Close proactively rather than waiting for a disconnect event. The
    // reboot is a soft reset with VBUS still up, which this host controller
    // does not reliably report as a clean disconnect; holding a stale
    // handle open only makes the port slower to notice the device return.
    usb_host_transfer_free(xfer);
    usb_host_device_close(_client, dev_hdl);

    return err == ESP_OK;
}

bool PicoUpdater::forceBootselViaPins()
{
#if defined(PIN_RP2040_BOOTSEL) && defined(PIN_RP2040_RUN)
    // Both lines drive a transistor each, active low, and are left as
    // inputs when idle so ordinary booting and flashing are undisturbed.
    // Hold BOOTSEL down across a reset pulse on RUN: the bootrom samples it
    // as it comes out of reset and stays in BOOTSEL if it is low.
    Debug_printf(PICOFW "forcing BOOTSEL with the RUN and BOOTSEL lines\r\n");

    fnSystem.set_pin_mode(PIN_RP2040_BOOTSEL, gpio_mode_t::GPIO_MODE_OUTPUT);
    fnSystem.digital_write(PIN_RP2040_BOOTSEL, DIGI_LOW);

    fnSystem.set_pin_mode(PIN_RP2040_RUN, gpio_mode_t::GPIO_MODE_OUTPUT);
    fnSystem.digital_write(PIN_RP2040_RUN, DIGI_LOW);
    vTaskDelay(pdMS_TO_TICKS(10));

    // Release RUN, leaving BOOTSEL held while the chip boots and samples it.
    fnSystem.set_pin_mode(PIN_RP2040_RUN, gpio_mode_t::GPIO_MODE_INPUT);
    vTaskDelay(pdMS_TO_TICKS(50));

    fnSystem.set_pin_mode(PIN_RP2040_BOOTSEL, gpio_mode_t::GPIO_MODE_INPUT);
    return true;
#else
    // This board has no wiring to the companion's RUN/BOOTSEL pins, so a
    // companion whose firmware will not answer has to be recovered with its
    // own BOOTSEL button.
    return false;
#endif
}

// ---------------------------------------------------------------------------
// Flashing
// ---------------------------------------------------------------------------

bool PicoUpdater::flashAttached(uint8_t address, const fn_pico_blob &blob)
{
    usb_device_handle_t dev_hdl;
    if (usb_host_device_open(_client, address, &dev_hdl) != ESP_OK) {
        Debug_printf(PICOFW "FAIL could not open the BOOTSEL device\r\n");
        return false;
    }

    const usb_config_desc_t *config_desc = nullptr;
    const usb_device_desc_t *dev_desc = nullptr;
    usb_host_get_device_descriptor(dev_hdl, &dev_desc);
    usb_host_get_active_config_descriptor(dev_hdl, &config_desc);

    uint8_t chip = FN_PICO_CHIP_UNKNOWN;
    PicobootClient::isBootselDevice(dev_desc, &chip);

    uint8_t itf = 0, out_ep = 0, in_ep = 0;
    uint16_t in_mps = 64;
    if (!PicobootClient::findInterface(config_desc, &itf, &out_ep, &in_ep, &in_mps)) {
        Debug_printf(PICOFW "FAIL no PICOBOOT interface on the BOOTSEL device\r\n");
        usb_host_device_close(_client, dev_hdl);
        return false;
    }

    if (usb_host_interface_claim(_client, dev_hdl, itf, 0) != ESP_OK) {
        Debug_printf(PICOFW "FAIL could not claim the PICOBOOT interface\r\n");
        usb_host_device_close(_client, dev_hdl);
        return false;
    }

    uint32_t sectors = (blob.size + PICOBOOT_FLASH_SECTOR_SIZE - 1) / PICOBOOT_FLASH_SECTOR_SIZE;
    Debug_printf(PICOFW "flashing %s (%u bytes, %u sectors from 0x%08x)\r\n",
                 blob.name, (unsigned)blob.size, (unsigned)sectors,
                 (unsigned)blob.flash_base);

    picobootClient.bind(_client, dev_hdl, itf, out_ep, in_ep, in_mps, chip);

    uint32_t started = fnSystem.millis();
    bool ok = picobootClient.flashImage(blob, progressForwarder, this);
    uint32_t elapsed = fnSystem.millis() - started;

    picobootClient.unbind();
    usb_host_interface_release(_client, dev_hdl, itf);
    usb_host_device_close(_client, dev_hdl);

    if (!ok) {
        // The companion is left in BOOTSEL, which is recoverable: the next
        // boot finds it there and flashes it without any prompting.
        Debug_printf(PICOFW "FAIL %s -- the companion is still in BOOTSEL and "
                     "will be retried on the next boot\r\n",
                     picobootClient.lastError());
        return false;
    }

    Debug_printf(PICOFW "OK %s flashed and verified (%u bytes in %u.%us); "
                 "rebooting the companion\r\n",
                 blob.name, (unsigned)blob.size,
                 (unsigned)(elapsed / 1000), (unsigned)((elapsed % 1000) / 100));

    writeFlashedSha(blob.name, blob.sha256);
    return true;
}

// ---------------------------------------------------------------------------
// Boot-time flow
// ---------------------------------------------------------------------------

void PicoUpdater::handleCompanion(const fn_pico_blob &blob)
{
    Debug_printf(PICOFW "image %s %u bytes sha256 %.8s chip %s base 0x%08x limit 0x%08x\r\n",
                 blob.name, (unsigned)blob.size, blob.sha256,
                 blob.chip == FN_PICO_CHIP_RP2350 ? "rp2350" : "rp2040",
                 (unsigned)blob.flash_base, (unsigned)blob.flash_limit);

    // Take whichever turns up first, rather than waiting out the whole
    // timeout looking for one kind: a healthy cartridge answers at once, and
    // that is the common case on every boot after the first.
    uint8_t cdc_address = 0;
    uint8_t cdc_itf = 0;
    DeviceKind kind = waitForAny(WAIT_FIRST_DEVICE_MS, &cdc_address, &cdc_itf);

    if (kind == KIND_OTHER) {
        Debug_printf(PICOFW "no companion attached within %ums; continuing boot\r\n",
                     (unsigned)WAIT_FIRST_DEVICE_MS);
        return;
    }

    if (kind == KIND_BOOTSEL) {
        // Flashed regardless of what NVS says: a chip sitting in BOOTSEL is
        // either blank or was deliberately put there, and in both cases what
        // it should get is this image.
        if (flashAttached(cdc_address, blob))
            _flashed_this_boot = true;
        return;
    }

    // A running companion: only disturb it if the image actually changed.
    char flashed[SHA_HEX_LEN + 1] = {0};
    bool known = readFlashedSha(blob.name, flashed, sizeof(flashed));
    if (known && blob.sha256 && strncmp(flashed, blob.sha256, SHA_HEX_LEN) == 0) {
        // Deliberately does not set _flashed_this_boot: nothing was
        // disturbed, so there is no reattach to wait for -- the cartridge is
        // already up and the bus can have it immediately.
        Debug_printf(PICOFW "up to date (%s %.8s); no reflash needed\r\n",
                     blob.name, blob.sha256);
        return;
    }

    Debug_printf(PICOFW "companion is running %.8s, image is %.8s; asking for "
                 "BOOTSEL\r\n", known ? flashed : "unknown ", blob.sha256);

    requestBootsel(cdc_address, cdc_itf);

    int address = waitForDevice(KIND_BOOTSEL, WAIT_BOOTSEL_MS);
    if (address < 0) {
        // The cooperative route did not work -- the firmware may be hung,
        // or may not implement the 1200 baud convention at all.
        if (forceBootselViaPins())
            address = waitForDevice(KIND_BOOTSEL, WAIT_BOOTSEL_MS);
    }

    if (address < 0) {
        Debug_printf(PICOFW "FAIL the companion did not enter BOOTSEL; leaving "
                     "it alone and continuing boot\r\n");
        return;
    }

    if (flashAttached((uint8_t)address, blob))
        _flashed_this_boot = true;
}

void PicoUpdater::bootCheck()
{
    if (fn_pico_blob_total() == 0) {
        // Nothing embedded: either this board has no companion, or the
        // build skipped it (FUJINET_SKIP_PICO). Do not even bring up the
        // USB client -- ACMChannel should own the host exactly as it does
        // on every other board.
        Debug_printf(PICOFW "SKIP no companion image embedded in this build\r\n");
        return;
    }

    if (!startClient())
        return;

    scanExistingDevices();

    for (size_t i = 0; i < fn_pico_blob_total(); i++) {
        const fn_pico_blob *blob = fn_pico_blob_at(i);
        if (blob && blob->data)
            handleCompanion(*blob);
    }

    // If the companion was just flashed it is rebooting now. Waiting for it
    // here means the bus finds it already up instead of timing out on its
    // first read.
    if (_flashed_this_boot) {
        int address = waitForDevice(KIND_CDC, WAIT_REATTACH_MS);
        if (address >= 0)
            Debug_printf(PICOFW "companion is up\r\n");
        else
            Debug_printf(PICOFW "WARN the companion has not reappeared yet; the "
                         "bus will keep waiting for it\r\n");
    }

    _boot_phase = false;

    // Keep watching. A companion hot-plugged later, or one that put itself
    // into BOOTSEL, gets flashed without an ESP32 reboot.
    xTaskCreate(watchTaskForwarder, "picofw", 8192, this, 10, NULL);
}

void PicoUpdater::watchTask()
{
    while (true) {
        DeviceEvent ev;
        if (xQueueReceive(_events, &ev, portMAX_DELAY) != pdTRUE)
            continue;
        if (!ev.arrived)
            continue;

        uint8_t chip = FN_PICO_CHIP_UNKNOWN;
        uint8_t cdc_itf = 0;
        if (classify(ev.address, &chip, &cdc_itf) != KIND_BOOTSEL)
            continue; // a running companion out here belongs to the bus

        // Flash whatever image matches this chip.
        for (size_t i = 0; i < fn_pico_blob_total(); i++) {
            const fn_pico_blob *blob = fn_pico_blob_at(i);
            if (blob && blob->data && blob->chip == chip) {
                Debug_printf(PICOFW "a companion appeared in BOOTSEL\r\n");
                flashAttached(ev.address, *blob);
                break;
            }
        }
    }
}

#endif // CONFIG_USB_PICOBOOT_HOST_ENABLED
