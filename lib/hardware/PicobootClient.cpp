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
#include "fnFsSD.h"
#include "ACMChannel.h" // usbHostEnsureInstalled()

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
    for (int i = 0; i < 2; i++) {
        int ep_offset = offset;
        const usb_ep_desc_t *ep_desc =
            usb_parse_endpoint_descriptor_by_index(intf_desc, i, config_desc->wTotalLength, &ep_offset);
        if (!ep_desc)
            continue;
        if (USB_EP_DESC_GET_EP_DIR(ep_desc))
            in_ep = ep_desc->bEndpointAddress;
        else
            out_ep = ep_desc->bEndpointAddress;
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
    _cmd_xfer->device_handle = dev_hdl;
    _data_xfer->device_handle = dev_hdl;
    _ack_xfer->device_handle = dev_hdl;

    Debug_printv("Picoboot: RP2040 in BOOTSEL attached (interface %d, out=%02x in=%02x)",
                 interface, out_ep, in_ep);
    xSemaphoreGive(_device_ready_sem);
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
            _data_xfer->num_bytes = transfer_length;
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
    // picoboot_cmd(). _ack_xfer's buffer capacity (64B) is just headroom;
    // the device actually sends/expects 0 bytes.
    _ack_xfer->num_bytes = 1;
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

bool PicobootClient::flashBin(const char *raw_bin_path, uint32_t flash_addr, uint32_t wait_ms)
{
    if (xSemaphoreTake(_device_ready_sem, pdMS_TO_TICKS(wait_ms)) != pdTRUE) {
        Debug_printv("Picoboot: no RP2040-in-BOOTSEL attached within %ums", wait_ms);
        return false;
    }

    FILE *f = fnSDFAT.file_open(raw_bin_path, FILE_READ);
    if (!f) {
        Debug_printv("Picoboot: can't open %s", raw_bin_path);
        return false;
    }

    bool ok = exclusiveAccess(EXCLUSIVE) && exitXip();

    static uint8_t sector[PICOBOOT_FLASH_SECTOR_SIZE];
    uint32_t addr = flash_addr;
    size_t n;
    while (ok && (n = fread(sector, 1, sizeof(sector), f)) > 0) {
        if (n < sizeof(sector))
            memset(sector + n, 0xFF, sizeof(sector) - n); // pad final sector
        ok = flashErase(addr, sizeof(sector)) && flashWrite(addr, sector, sizeof(sector));
        addr += sizeof(sector);
    }
    fclose(f);

    if (ok) {
        Debug_printv("Picoboot: flashed %s, rebooting RP2040", raw_bin_path);
        reboot(0, 0, 500); // dPC=0 -> normal boot path, not a RAM image
    } else {
        Debug_printv("Picoboot: flash failed partway through -- RP2040 needs hardware BOOTSEL forcing to recover");
    }

    exclusiveAccess(NOT_EXCLUSIVE);
    return ok;
}

#endif /* CONFIG_USB_PICOBOOT_HOST_ENABLED */
