#include "PicobootClient.h"

#ifdef CONFIG_USB_PICOBOOT_HOST_ENABLED

#include <usb/usb_helpers.h>
#include <esp_log.h>
#include <cstring>

// picoboot_protocol/{picoboot.h,picoboot_constants.h} are a verbatim copy of
// the pico-sdk's src/common/boot_picoboot_headers (BSD-3-Clause, Raspberry
// Pi (Trading) Ltd) -- see that directory's own header comments. Vendored
// here rather than pointed at PICO_SDK_PATH because this is an ESP-IDF
// build with no RP2040 toolchain in its include path. NO_PICO_PLATFORM
// skips picoboot.h's own "#include pico/platform.h" (not available here);
// __packed/__aligned are what that header would otherwise have supplied.
#define NO_PICO_PLATFORM
#ifndef __packed
#define __packed __attribute__((packed))
#endif
#ifndef __aligned
#define __aligned(n) __attribute__((aligned(n)))
#endif
extern "C" {
#include "picoboot_protocol/picoboot.h"
}

#include "../../include/debug.h"
#include "ACMChannel.h" // usbHostEnsureInstalled()

// The RP2040 firmware, compiled directly into this binary. build_pico_intv.py
// (a "pre:" extra_script, fujiversal-intv board only) builds fuji_intv.bin
// and generates lib/hardware/fuji_intv_bin_data.cpp -- a real, gitignored
// source file defining these two symbols as an actual byte array + its
// size, picked up by PlatformIO's normal source discovery like any other
// file in this directory. (An ESP-IDF EMBED_FILES / hand-rolled objcopy
// wrapper-object approach was tried first; neither one's linker inputs ever
// reached PlatformIO's own separate SCons-driven final link -- see
// build_pico_intv.py's header comment for why.)
extern "C" const uint8_t fuji_intv_bin_data[];
extern "C" const size_t fuji_intv_bin_size;

#define DEBUG_TAG "Picoboot"

// One RP2040 flash sector -- the granularity PC_FLASH_ERASE requires
// addr/len to be aligned to (see boot/picoboot_constants.h's Log2PageSize
// note and picotool's FLASH_SECTOR_ERASE_SIZE, which isn't in a header this
// firmware can include -- picoboot_connection.h is picotool's own, with a
// libusb dependency).
#define PICOBOOT_FLASH_SECTOR_SIZE 4096u

// From picotool's picoboot_connection.c: RP2040 bootrom's PICOBOOT interface
// is interface 0 if the device has only one interface (PICOBOOT-only, e.g.
// after PC_EXCLUSIVE_ACCESS(EXCLUSIVE_AND_EJECT) hid the MSD interface), or
// interface 1 otherwise (stock cold-boot BOOTSEL exposes MSD on 0, PICOBOOT
// on 1). We always see the stock composite device (nothing has hidden MSD
// yet), so this matches the "else 1" branch in the reference.
#define PICOBOOT_VID       0x2E8Au
#define PICOBOOT_PID_RP2040 0x0003u

PicobootClient picobootClient;

// See the comment in newDevice() for why this exists and runs on its own
// task rather than being called directly.
static void autoFlashTask(void *arg)
{
    PicobootClient *self = (PicobootClient *)arg;
    self->flashEmbedded(0x10000000, 1000);
    vTaskDelete(NULL);
}

static void clientEventForwarder(const usb_host_client_event_msg_t *event_msg, void *arg)
{
    ((PicobootClient *)arg)->clientEvent(event_msg);
}

static void picobootClientTask(void *arg)
{
    usb_host_client_handle_t client_hdl = (usb_host_client_handle_t)arg;
    while (1) {
        // This is also what dispatches usb_host_transfer_submit() completion
        // callbacks for transfers belonging to this client -- see
        // submitAndWait()/xferDone() below. Same architecture as
        // ACMChannel's usb_lib_task / the vendored cdc_acm driver task: one
        // background task owns the event pump, other tasks call the
        // synchronous public API and block on their own semaphore.
        usb_host_client_handle_events(client_hdl, portMAX_DELAY);
    }
}

void PicobootClient::begin()
{
    _device_ready_sem = xSemaphoreCreateBinary();
    _xfer_done_sem = xSemaphoreCreateBinary();
    assert(_device_ready_sem && _xfer_done_sem);

    // usbHostEnsureInstalled() is shared with ACMChannel -- whichever of the
    // two begin()s runs first does the real usb_host_install(); only one
    // caller may ever call that directly. rs232.cpp calls this begin()
    // BEFORE ACMChannel's (which blocks until a CDC-ACM device attaches, and
    // an RP2040 sitting in BOOTSEL never presents one) specifically so this
    // client is registered and listening before that block, not after.
    usbHostEnsureInstalled();

    usb_host_client_config_t client_config = {};
    client_config.is_synchronous = false;
    client_config.max_num_event_msg = 5;
    client_config.async.client_event_callback = clientEventForwarder;
    client_config.async.callback_arg = this;
    ESP_ERROR_CHECK(usb_host_client_register(&client_config, &_client_hdl));

    ESP_ERROR_CHECK(usb_host_transfer_alloc(sizeof(picoboot_cmd), 0, &_cmd_xfer));
    ESP_ERROR_CHECK(usb_host_transfer_alloc(4096, 0, &_data_xfer));
    ESP_ERROR_CHECK(usb_host_transfer_alloc(64, 0, &_ack_xfer));
    _cmd_xfer->context = this;
    _data_xfer->context = this;
    _ack_xfer->context = this;

    BaseType_t task_created = xTaskCreate(picobootClientTask, "picoboot", 4096,
                                           _client_hdl, 18, NULL);
    assert(task_created == pdTRUE);

    Debug_printv("Picoboot: client registered, watching for RP2040 in BOOTSEL (%04X:%04X)",
                 PICOBOOT_VID, PICOBOOT_PID_RP2040);
}

void PicobootClient::clientEvent(const usb_host_client_event_msg_t *event_msg)
{
    Debug_printv("Picoboot: client event %d", (int)event_msg->event);
    switch (event_msg->event) {
    case USB_HOST_CLIENT_EVENT_NEW_DEV:
        newDevice(event_msg->new_dev.address);
        break;
    case USB_HOST_CLIENT_EVENT_DEV_GONE:
        deviceGone();
        break;
    default:
        break;
    }
}

void PicobootClient::newDevice(uint8_t dev_addr)
{
    usb_device_handle_t dev_hdl;
    esp_err_t err = usb_host_device_open(_client_hdl, dev_addr, &dev_hdl);
    if (err != ESP_OK) {
        Debug_printv("Picoboot: usb_host_device_open(addr=%d) failed: %s", dev_addr, esp_err_to_name(err));
        return;
    }

    const usb_device_desc_t *dev_desc;
    usb_host_get_device_descriptor(dev_hdl, &dev_desc);

    Debug_printv("Picoboot: new device addr=%d VID:PID=%04X:%04X", dev_addr, dev_desc->idVendor, dev_desc->idProduct);

    if (dev_desc->idVendor != PICOBOOT_VID || dev_desc->idProduct != PICOBOOT_PID_RP2040) {
        // Not an RP2040-in-BOOTSEL. Leave it for ACMChannel (or whatever
        // else registered a new_dev_cb) -- we just close our own handle.
        usb_host_device_close(_client_hdl, dev_hdl);
        return;
    }

    const usb_config_desc_t *config_desc;
    usb_host_get_active_config_descriptor(dev_hdl, &config_desc);

    uint8_t interface = (config_desc->bNumInterfaces == 1) ? 0 : 1;
    int offset = 0;
    const usb_intf_desc_t *intf_desc =
        usb_parse_interface_descriptor(config_desc, interface, 0, &offset);
    if (!intf_desc || intf_desc->bInterfaceClass != USB_CLASS_VENDOR_SPEC ||
        intf_desc->bNumEndpoints != 2)
    {
        Debug_printv("Picoboot: interface %d isn't the PICOBOOT vendor interface we expected", interface);
        usb_host_device_close(_client_hdl, dev_hdl);
        return;
    }

    uint8_t out_ep = 0, in_ep = 0;
    uint16_t in_ep_mps = 64;
    for (int i = 0; i < 2; i++) {
        int ep_offset = offset;
        const usb_ep_desc_t *ep_desc =
            usb_parse_endpoint_descriptor_by_index(intf_desc, i, config_desc->wTotalLength, &ep_offset);
        if (!ep_desc)
            continue;
        if (USB_EP_DESC_GET_EP_DIR(ep_desc)) {
            in_ep = ep_desc->bEndpointAddress;
            in_ep_mps = ep_desc->wMaxPacketSize;
        } else {
            out_ep = ep_desc->bEndpointAddress;
        }
    }
    if (!out_ep || !in_ep) {
        Debug_printv("Picoboot: PICOBOOT interface missing bulk IN/OUT endpoints");
        usb_host_device_close(_client_hdl, dev_hdl);
        return;
    }

    if (usb_host_interface_claim(_client_hdl, dev_hdl, interface, 0) != ESP_OK) {
        Debug_printv("Picoboot: failed to claim PICOBOOT interface");
        usb_host_device_close(_client_hdl, dev_hdl);
        return;
    }

    _dev_hdl = dev_hdl;
    _interface = interface;
    _out_ep = out_ep;
    _in_ep = in_ep;
    _in_ep_mps = in_ep_mps ? in_ep_mps : 64;
    _cmd_xfer->device_handle = dev_hdl;
    _data_xfer->device_handle = dev_hdl;
    _ack_xfer->device_handle = dev_hdl;

    Debug_printv("Picoboot: RP2040 in BOOTSEL attached (interface %d, out=%02x in=%02x)",
                 interface, out_ep, in_ep);
    xSemaphoreGive(_device_ready_sem);

    // Auto-flash, since "picoboot-flash" (the intended real trigger, see
    // PicobootCommands.cpp) is unreachable right now: ENABLE_CONSOLE (which
    // Console::begin() -- and so the whole command REPL -- requires) doesn't
    // build cleanly anywhere else in this tree (Console/`console` is a
    // main.cpp-local global with no extern declaration, so every other TU
    // that includes debug.h with ENABLE_CONSOLE set fails to find it). This
    // is a temporary bring-up hook, not the intended interface -- swap for
    // the console command (or a mailbox-driven trigger) once that's fixed.
    // MUST run on a different task than this one: newDevice() executes on
    // the "picoboot" client task, which is also what pumps
    // usb_host_client_handle_events() -- and that's what delivers the
    // transfer-complete callbacks flashBin() blocks on. Calling it directly
    // from here would deadlock the very task that has to service it.
    xTaskCreate(autoFlashTask, "picoboot-autoflash", 4096, this, 10, NULL);
}

void PicobootClient::deviceGone()
{
    if (_dev_hdl) {
        usb_host_interface_release(_client_hdl, _dev_hdl, _interface);
        usb_host_device_close(_client_hdl, _dev_hdl);
        _dev_hdl = nullptr;
    }
}

static void xferDoneForwarder(usb_transfer_t *transfer)
{
    ((PicobootClient *)transfer->context)->xferDone(transfer);
}

void PicobootClient::xferDone(usb_transfer_t *transfer)
{
    xSemaphoreGive(_xfer_done_sem);
}

int PicobootClient::submitAndWait(usb_transfer_t *transfer, uint32_t timeout_ms)
{
    transfer->callback = xferDoneForwarder;
    xSemaphoreTake(_xfer_done_sem, 0); // drain any stale give
    esp_err_t err = usb_host_transfer_submit(transfer);
    if (err != ESP_OK) {
        Debug_printv("Picoboot: transfer submit failed: %s", esp_err_to_name(err));
        return -1;
    }
    if (xSemaphoreTake(_xfer_done_sem, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        Debug_printv("Picoboot: transfer timed out");
        return -1;
    }
    return (int)transfer->status;
}

bool PicobootClient::cmd(uint8_t cmd_id, uint8_t cmd_size, const void *args, uint32_t transfer_length,
                          uint8_t *buffer)
{
    if (!_dev_hdl)
        return false;

    static uint32_t token = 1;

    picoboot_cmd pb = {};
    pb.dMagic = PICOBOOT_MAGIC;
    pb.dToken = token++;
    pb.bCmdId = cmd_id;
    pb.bCmdSize = cmd_size;
    pb.dTransferLength = transfer_length;
    if (args)
        memcpy(pb.args, args, cmd_size);

    memcpy(_cmd_xfer->data_buffer, &pb, sizeof(pb));
    _cmd_xfer->num_bytes = sizeof(pb);
    _cmd_xfer->bEndpointAddress = _out_ep;
    if (submitAndWait(_cmd_xfer, 3000) != USB_TRANSFER_STATUS_COMPLETED)
        return false;

    bool is_in = (cmd_id & 0x80) != 0;

    if (transfer_length != 0) {
        // buf_size is always <= the 4096B _data_xfer allocation -- callers
        // never ask for more than one flash sector at a time.
        assert(transfer_length <= 4096);
        if (is_in) {
            // ESP-IDF's usb_host requires non-control IN requests to be an
            // exact multiple of the endpoint's MPS (libusb has no such
            // restriction, which is why picotool can just ask for exactly
            // transfer_length bytes). Round the USB-level request up; the
            // device only ever sends the real transfer_length bytes
            // (short packet), so this doesn't change what's read.
            uint32_t req = ((transfer_length + _in_ep_mps - 1) / _in_ep_mps) * _in_ep_mps;
            assert(req <= 4096);
            _data_xfer->num_bytes = req;
            _data_xfer->bEndpointAddress = _in_ep;
            if (submitAndWait(_data_xfer, 10000) != USB_TRANSFER_STATUS_COMPLETED)
                return false;
            if (buffer)
                memcpy(buffer, _data_xfer->data_buffer, transfer_length);
        } else {
            if (buffer)
                memcpy(_data_xfer->data_buffer, buffer, transfer_length);
            _data_xfer->num_bytes = transfer_length;
            _data_xfer->bEndpointAddress = _out_ep;
            if (submitAndWait(_data_xfer, 10000) != USB_TRANSFER_STATUS_COMPLETED)
                return false;
        }
    }

    // ACK is a zero-length packet in the OPPOSITE direction from the data
    // phase (or from the command itself, if there was no data phase) --
    // see boot/picoboot.h's picoboot_cmd comment and picoboot_connection.c's
    // picoboot_cmd(). The device actually sends/expects 0 bytes; picotool
    // (libusb) just requests 1 byte and gets a 0-byte completion back. On
    // ESP-IDF a non-control IN request must be an exact multiple of the
    // endpoint's MPS (OUT has no such restriction), so when the ACK is IN
    // (is_in false -> ack via _in_ep) request a full MPS -- the device still
    // only sends the ZLP it always sends, this is purely about satisfying
    // usb_host's request-size validation.
    bool ack_is_in = !is_in;
    _ack_xfer->num_bytes = ack_is_in ? _in_ep_mps : 1;
    _ack_xfer->bEndpointAddress = is_in ? _out_ep : _in_ep;
    if (submitAndWait(_ack_xfer, transfer_length == 0 ? 10000 : 3000) != USB_TRANSFER_STATUS_COMPLETED)
        return false;

    return true;
}

bool PicobootClient::exclusiveAccess(uint8_t exclusive)
{
    picoboot_exclusive_cmd args = { .bExclusive = exclusive };
    return cmd(PC_EXCLUSIVE_ACCESS, sizeof(args), &args, 0, nullptr);
}

bool PicobootClient::exitXip()
{
    return cmd(PC_EXIT_XIP, 0, nullptr, 0, nullptr);
}

bool PicobootClient::flashErase(uint32_t addr, uint32_t len)
{
    picoboot_range_cmd args = { .dAddr = addr, .dSize = len };
    return cmd(PC_FLASH_ERASE, sizeof(args), &args, 0, nullptr);
}

bool PicobootClient::flashWrite(uint32_t addr, const uint8_t *buffer, uint32_t len)
{
    picoboot_range_cmd args = { .dAddr = addr, .dSize = len };
    return cmd(PC_WRITE, sizeof(args), &args, len, (uint8_t *)buffer);
}

bool PicobootClient::reboot(uint32_t pc, uint32_t sp, uint32_t delay_ms)
{
    picoboot_reboot_cmd args = { .dPC = pc, .dSP = sp, .dDelayMS = delay_ms };
    // REBOOT's ACK races the actual reboot -- a timeout/short-read here is
    // expected and not a failure, unlike every other command.
    cmd(PC_REBOOT, sizeof(args), &args, 0, nullptr);
    return true;
}

bool PicobootClient::forceReflash(uint32_t flash_addr, uint32_t wait_ms)
{
    // Drain any stale give from a previous attach -- otherwise a leftover
    // signal could make the flashEmbedded() call below think a BOOTSEL
    // device is already present when it's actually the *old* attach event.
    xSemaphoreTake(_device_ready_sem, 0);

    if (_acm) {
        if (_acm->triggerBootselTouch()) {
            Debug_printv("Picoboot: BOOTSEL touch sent, waiting for RP2040 to re-attach");
        } else {
            Debug_printv("Picoboot: BOOTSEL touch request failed (nothing attached to ACMChannel right now?) "
                         "-- proceeding to wait anyway, in case the RP2040 is already in BOOTSEL");
        }
    } else {
        Debug_printv("Picoboot: no ACMChannel set (setAcmChannel() never called) -- "
                     "can only catch an RP2040 already in BOOTSEL, not force one into it");
    }

    return flashEmbedded(flash_addr, wait_ms);
}

bool PicobootClient::flashEmbedded(uint32_t flash_addr, uint32_t wait_ms)
{
    if (xSemaphoreTake(_device_ready_sem, pdMS_TO_TICKS(wait_ms)) != pdTRUE) {
        Debug_printv("Picoboot: no RP2040-in-BOOTSEL attached within %ums", wait_ms);
        return false;
    }

    const uint8_t *image = fuji_intv_bin_data;
    size_t image_len = fuji_intv_bin_size;
    Debug_printv("Picoboot: flashing embedded fuji_intv.bin (%u bytes)", (unsigned)image_len);

    bool ok = exclusiveAccess(EXCLUSIVE) && exitXip();

    static uint8_t sector[PICOBOOT_FLASH_SECTOR_SIZE];
    uint32_t addr = flash_addr;
    size_t remaining = image_len;
    const uint8_t *src = image;
    while (ok && remaining > 0) {
        size_t n = remaining < sizeof(sector) ? remaining : sizeof(sector);
        memcpy(sector, src, n);
        if (n < sizeof(sector))
            memset(sector + n, 0xFF, sizeof(sector) - n); // pad final sector
        ok = flashErase(addr, sizeof(sector)) && flashWrite(addr, sector, sizeof(sector));
        addr += sizeof(sector);
        src += n;
        remaining -= n;
    }

    if (ok) {
        Debug_printv("Picoboot: flashed embedded fuji_intv.bin, rebooting RP2040");
        reboot(0, 0, 500); // dPC=0 -> normal boot path, not a RAM image
    } else {
        Debug_printv("Picoboot: flash failed partway through -- RP2040 needs hardware BOOTSEL forcing to recover");
    }

    exclusiveAccess(NOT_EXCLUSIVE);
    return ok;
}

#endif /* CONFIG_USB_PICOBOOT_HOST_ENABLED */
