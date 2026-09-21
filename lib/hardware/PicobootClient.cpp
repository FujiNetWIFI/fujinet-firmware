#include "PicobootClient.h"

#ifdef CONFIG_USB_PICOBOOT_HOST_ENABLED

#include <usb/usb_helpers.h>
#include <cstring>
#include <cstdarg>
#include <cstdio>

// picoboot_protocol/ is a verbatim copy of the pico-sdk's
// boot_picoboot_headers. NO_PICO_PLATFORM skips its pico/platform.h, which
// an ESP-IDF build has not got; these are what it would have supplied.
#define NO_PICO_PLATFORM
#ifndef __packed
#define __packed __attribute__((packed))
#endif
#ifndef __aligned
#define __aligned(n) __attribute__((aligned(n)))
#endif
extern "C" {
#include "picoboot_protocol/picoboot.h"
#include "picoboot_protocol/picoboot_constants.h"
}

#include "../../include/debug.h"

PicobootClient picobootClient;

// Control requests outside the command stream (boot/picoboot.h).
#define PICOBOOT_IF_RESET      0x41
#define PICOBOOT_IF_CMD_STATUS 0x42

// bmRequestType for those: vendor request to an interface.
#define PICOBOOT_REQ_OUT 0x41
#define PICOBOOT_REQ_IN  0xC1

// The data phase can include a flash erase, which is slow.
#define PICOBOOT_CMD_TIMEOUT_MS  3000
#define PICOBOOT_DATA_TIMEOUT_MS 10000
#define PICOBOOT_CTRL_TIMEOUT_MS 2000

// Must be non-zero: the RP2350 bootrom rejects a zero reboot delay.
#define PICOBOOT_REBOOT_DELAY_MS 500

// boot/picoboot.h's picoboot_status, so a failure reads as a reason.
static const char *picoboot_status_name(uint32_t status)
{
    switch (status) {
    case PICOBOOT_OK:                          return "OK";
    case PICOBOOT_UNKNOWN_CMD:                 return "UNKNOWN_CMD";
    case PICOBOOT_INVALID_CMD_LENGTH:          return "INVALID_CMD_LENGTH";
    case PICOBOOT_INVALID_TRANSFER_LENGTH:     return "INVALID_TRANSFER_LENGTH";
    case PICOBOOT_INVALID_ADDRESS:             return "INVALID_ADDRESS";
    case PICOBOOT_BAD_ALIGNMENT:               return "BAD_ALIGNMENT";
    case PICOBOOT_INTERLEAVED_WRITE:           return "INTERLEAVED_WRITE";
    case PICOBOOT_REBOOTING:                   return "REBOOTING";
    case PICOBOOT_UNKNOWN_ERROR:               return "UNKNOWN_ERROR";
    case PICOBOOT_INVALID_STATE:               return "INVALID_STATE";
    case PICOBOOT_NOT_PERMITTED:               return "NOT_PERMITTED";
    case PICOBOOT_INVALID_ARG:                 return "INVALID_ARG";
    case PICOBOOT_BUFFER_TOO_SMALL:            return "BUFFER_TOO_SMALL";
    case PICOBOOT_PRECONDITION_NOT_MET:        return "PRECONDITION_NOT_MET";
    case PICOBOOT_MODIFIED_DATA:               return "MODIFIED_DATA";
    case PICOBOOT_INVALID_DATA:                return "INVALID_DATA";
    case PICOBOOT_NOT_FOUND:                   return "NOT_FOUND";
    case PICOBOOT_UNSUPPORTED_MODIFICATION:    return "UNSUPPORTED_MODIFICATION";
    default:                                   return "?";
    }
}

// ---------------------------------------------------------------------------
// Setup / identification
// ---------------------------------------------------------------------------

bool PicobootClient::init()
{
    if (_xfer_done_sem)
        return true; // already initialised

    _xfer_done_sem = xSemaphoreCreateBinary();
    if (!_xfer_done_sem)
        return false;

    if (usb_host_transfer_alloc(sizeof(picoboot_cmd), 0, &_cmd_xfer) != ESP_OK ||
        usb_host_transfer_alloc(PICOBOOT_FLASH_SECTOR_SIZE, 0, &_data_xfer) != ESP_OK ||
        usb_host_transfer_alloc(64, 0, &_ack_xfer) != ESP_OK ||
        // Setup packet plus the 16-byte command status it can carry back.
        usb_host_transfer_alloc(USB_SETUP_PACKET_SIZE + 16, 0, &_ctrl_xfer) != ESP_OK)
    {
        Debug_printv("Picoboot: transfer allocation failed");
        return false;
    }

    _cmd_xfer->context = this;
    _data_xfer->context = this;
    _ack_xfer->context = this;
    _ctrl_xfer->context = this;
    return true;
}

bool PicobootClient::isBootselDevice(const usb_device_desc_t *desc, uint8_t *chip_out)
{
    if (!desc || desc->idVendor != PICOBOOT_USB_VID)
        return false;

    uint8_t chip;
    switch (desc->idProduct) {
    case PICOBOOT_USB_PID_RP2040: chip = FN_PICO_CHIP_RP2040; break;
    case PICOBOOT_USB_PID_RP2350: chip = FN_PICO_CHIP_RP2350; break;
    default:
        // Vendor matches but not BOOTSEL: a pico_stdio_usb app (0009/000a).
        return false;
    }

    if (chip_out)
        *chip_out = chip;
    return true;
}

bool PicobootClient::findInterface(const usb_config_desc_t *config_desc,
                                    uint8_t *itf_out, uint8_t *out_ep_out,
                                    uint8_t *in_ep_out, uint16_t *in_mps_out)
{
    if (!config_desc)
        return false;

    // Mass storage takes interface 0 unless it has been hidden.
    uint8_t interface = (config_desc->bNumInterfaces == 1) ? 0 : 1;

    int offset = 0;
    const usb_intf_desc_t *intf_desc =
        usb_parse_interface_descriptor(config_desc, interface, 0, &offset);
    if (!intf_desc || intf_desc->bInterfaceClass != USB_CLASS_VENDOR_SPEC ||
        intf_desc->bNumEndpoints != 2)
    {
        return false;
    }

    uint8_t out_ep = 0, in_ep = 0;
    uint16_t in_mps = 64;
    for (int i = 0; i < 2; i++) {
        int ep_offset = offset;
        const usb_ep_desc_t *ep_desc =
            usb_parse_endpoint_descriptor_by_index(intf_desc, i,
                                                    config_desc->wTotalLength,
                                                    &ep_offset);
        if (!ep_desc)
            continue;
        if (USB_EP_DESC_GET_EP_DIR(ep_desc)) {
            in_ep = ep_desc->bEndpointAddress;
            if (ep_desc->wMaxPacketSize)
                in_mps = ep_desc->wMaxPacketSize;
        } else {
            out_ep = ep_desc->bEndpointAddress;
        }
    }

    if (!out_ep || !in_ep)
        return false;

    if (itf_out)    *itf_out = interface;
    if (out_ep_out) *out_ep_out = out_ep;
    if (in_ep_out)  *in_ep_out = in_ep;
    if (in_mps_out) *in_mps_out = in_mps;
    return true;
}

void PicobootClient::bind(usb_host_client_handle_t client_hdl,
                           usb_device_handle_t dev_hdl, uint8_t interface,
                           uint8_t out_ep, uint8_t in_ep, uint16_t in_mps,
                           uint8_t chip)
{
    _client_hdl = client_hdl;
    _dev_hdl = dev_hdl;
    _interface = interface;
    _out_ep = out_ep;
    _in_ep = in_ep;
    _in_mps = in_mps ? in_mps : 64;
    _chip = chip;

    _cmd_xfer->device_handle = dev_hdl;
    _data_xfer->device_handle = dev_hdl;
    _ack_xfer->device_handle = dev_hdl;
    _ctrl_xfer->device_handle = dev_hdl;
}

void PicobootClient::unbind()
{
    _dev_hdl = nullptr;
    _client_hdl = nullptr;
    _chip = FN_PICO_CHIP_UNKNOWN;
    // A later submit against a closed device would be undefined.
    if (_cmd_xfer)  _cmd_xfer->device_handle = nullptr;
    if (_data_xfer) _data_xfer->device_handle = nullptr;
    if (_ack_xfer)  _ack_xfer->device_handle = nullptr;
    if (_ctrl_xfer) _ctrl_xfer->device_handle = nullptr;
}

void PicobootClient::setError(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(_last_error, sizeof(_last_error), fmt, args);
    va_end(args);
}

// ---------------------------------------------------------------------------
// Transfers
// ---------------------------------------------------------------------------

// The instance travels in the transfer's context (set in submitAndWait()).
static void xferDoneForwarder(usb_transfer_t *transfer)
{
    ((PicobootClient *)transfer->context)->xferDone(transfer);
}

void PicobootClient::xferDone(usb_transfer_t *transfer)
{
    xSemaphoreGive(_xfer_done_sem);
}

int PicobootClient::submitAndWait(usb_transfer_t *transfer, uint32_t timeout_ms,
                                   bool control)
{
    transfer->callback = xferDoneForwarder;
    transfer->context = this;

    xSemaphoreTake(_xfer_done_sem, 0); // drop any stale completion

    esp_err_t err = control
        ? usb_host_transfer_submit_control(_client_hdl, transfer)
        : usb_host_transfer_submit(transfer);
    if (err != ESP_OK) {
        setError("transfer submit failed: %s", esp_err_to_name(err));
        return -1;
    }

    if (xSemaphoreTake(_xfer_done_sem, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        // A timed-out transfer is still owned by the host stack, so its
        // callback would fire later into a freed transfer. Halt/flush/clear
        // reclaims it; the flush cancels it, which gives the semaphore back.
        // Not a rare path: the reboot ack times out on every good flash.
        usb_host_endpoint_halt(transfer->device_handle, transfer->bEndpointAddress);
        usb_host_endpoint_flush(transfer->device_handle, transfer->bEndpointAddress);
        usb_host_endpoint_clear(transfer->device_handle, transfer->bEndpointAddress);
        xSemaphoreTake(_xfer_done_sem, pdMS_TO_TICKS(200));

        setError("transfer timed out after %ums", (unsigned)timeout_ms);
        return -1;
    }

    return (int)transfer->status;
}

bool PicobootClient::cmd(uint8_t cmd_id, uint8_t cmd_size, const void *args,
                          uint32_t transfer_length, uint8_t *buffer)
{
    if (!_dev_hdl) {
        setError("no device bound");
        return false;
    }
    if (transfer_length > PICOBOOT_FLASH_SECTOR_SIZE) {
        setError("transfer length %u exceeds the %u byte buffer",
                 (unsigned)transfer_length, (unsigned)PICOBOOT_FLASH_SECTOR_SIZE);
        return false;
    }

    static uint32_t token = 1;

    picoboot_cmd pb = {};
    pb.dMagic = PICOBOOT_MAGIC;
    pb.dToken = token++;
    pb.bCmdId = cmd_id;
    pb.bCmdSize = cmd_size;
    pb.dTransferLength = transfer_length;
    if (args && cmd_size)
        memcpy(pb.args, args, cmd_size);

    memcpy(_cmd_xfer->data_buffer, &pb, sizeof(pb));
    _cmd_xfer->num_bytes = sizeof(pb);
    _cmd_xfer->bEndpointAddress = _out_ep;
    if (submitAndWait(_cmd_xfer, PICOBOOT_CMD_TIMEOUT_MS, false)
            != USB_TRANSFER_STATUS_COMPLETED)
    {
        if (!_last_error[0])
            setError("command phase failed");
        return false;
    }

    // Top bit of the command id = data phase is device-to-host.
    bool is_in = (cmd_id & 0x80) != 0;

    if (transfer_length != 0) {
        if (is_in) {
            // ESP-IDF requires a bulk IN request to be a whole number of
            // max packets, unlike libusb. The device still short-packets
            // after transfer_length, so nothing extra is read.
            uint32_t req = ((transfer_length + _in_mps - 1) / _in_mps) * _in_mps;
            if (req > PICOBOOT_FLASH_SECTOR_SIZE) {
                setError("rounded IN request %u exceeds the buffer", (unsigned)req);
                return false;
            }
            _data_xfer->num_bytes = req;
            _data_xfer->bEndpointAddress = _in_ep;
            if (submitAndWait(_data_xfer, PICOBOOT_DATA_TIMEOUT_MS, false)
                    != USB_TRANSFER_STATUS_COMPLETED)
            {
                if (!_last_error[0])
                    setError("data-in phase failed");
                return false;
            }
            if (buffer)
                memcpy(buffer, _data_xfer->data_buffer, transfer_length);
        } else {
            if (buffer)
                memcpy(_data_xfer->data_buffer, buffer, transfer_length);
            _data_xfer->num_bytes = transfer_length;
            _data_xfer->bEndpointAddress = _out_ep;
            if (submitAndWait(_data_xfer, PICOBOOT_DATA_TIMEOUT_MS, false)
                    != USB_TRANSFER_STATUS_COMPLETED)
            {
                if (!_last_error[0])
                    setError("data-out phase failed");
                return false;
            }
        }
    }

    // Zero-length ack, in the opposite direction from the data phase (or
    // the command, if there was none). An IN ack must request a max packet.
    bool ack_is_in = !is_in;
    _ack_xfer->num_bytes = ack_is_in ? _in_mps : 1;
    _ack_xfer->bEndpointAddress = ack_is_in ? _in_ep : _out_ep;
    if (submitAndWait(_ack_xfer,
                      transfer_length == 0 ? PICOBOOT_DATA_TIMEOUT_MS
                                           : PICOBOOT_CMD_TIMEOUT_MS,
                      false) != USB_TRANSFER_STATUS_COMPLETED)
    {
        if (!_last_error[0])
            setError("ack phase failed");
        return false;
    }

    return true;
}

bool PicobootClient::ifReset()
{
    usb_setup_packet_t *setup = (usb_setup_packet_t *)_ctrl_xfer->data_buffer;
    setup->bmRequestType = PICOBOOT_REQ_OUT;
    setup->bRequest = PICOBOOT_IF_RESET;
    setup->wValue = 0;
    setup->wIndex = _interface;
    setup->wLength = 0;

    _ctrl_xfer->num_bytes = USB_SETUP_PACKET_SIZE;
    _ctrl_xfer->bEndpointAddress = 0;
    return submitAndWait(_ctrl_xfer, PICOBOOT_CTRL_TIMEOUT_MS, true)
            == USB_TRANSFER_STATUS_COMPLETED;
}

bool PicobootClient::cmdStatus(uint32_t *status_code_out)
{
    usb_setup_packet_t *setup = (usb_setup_packet_t *)_ctrl_xfer->data_buffer;
    setup->bmRequestType = PICOBOOT_REQ_IN;
    setup->bRequest = PICOBOOT_IF_CMD_STATUS;
    setup->wValue = 0;
    setup->wIndex = _interface;
    setup->wLength = sizeof(picoboot_cmd_status);

    _ctrl_xfer->num_bytes = USB_SETUP_PACKET_SIZE + sizeof(picoboot_cmd_status);
    _ctrl_xfer->bEndpointAddress = 0;
    if (submitAndWait(_ctrl_xfer, PICOBOOT_CTRL_TIMEOUT_MS, true)
            != USB_TRANSFER_STATUS_COMPLETED)
    {
        return false;
    }

    picoboot_cmd_status status;
    memcpy(&status, _ctrl_xfer->data_buffer + USB_SETUP_PACKET_SIZE, sizeof(status));
    if (status_code_out)
        *status_code_out = status.dStatusCode;
    return true;
}

void PicobootClient::describeFailure(const char *stage, uint32_t addr)
{
    // Keep the USB-level failure and append the bootrom's view of it.
    char usb_reason[sizeof(_last_error)];
    snprintf(usb_reason, sizeof(usb_reason), "%s",
             _last_error[0] ? _last_error : "command failed");

    uint32_t status_code = 0;
    if (cmdStatus(&status_code)) {
        setError("%s at 0x%08x: %s (bootrom status %u %s)", stage,
                 (unsigned)addr, usb_reason, (unsigned)status_code,
                 picoboot_status_name(status_code));
    } else {
        setError("%s at 0x%08x: %s (bootrom status unavailable)", stage,
                 (unsigned)addr, usb_reason);
    }

    // Clear the stall so a retry starts clean.
    ifReset();
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

bool PicobootClient::exclusiveAccess(uint8_t exclusive)
{
    picoboot_exclusive_cmd args = {};
    args.bExclusive = exclusive;
    return cmd(PC_EXCLUSIVE_ACCESS, sizeof(args), &args, 0, nullptr);
}

bool PicobootClient::exitXip()
{
    return cmd(PC_EXIT_XIP, 0, nullptr, 0, nullptr);
}

bool PicobootClient::enterCmdXip()
{
    return cmd(PC_ENTER_CMD_XIP, 0, nullptr, 0, nullptr);
}

bool PicobootClient::flashErase(uint32_t addr, uint32_t len)
{
    picoboot_range_cmd args = {};
    args.dAddr = addr;
    args.dSize = len;
    return cmd(PC_FLASH_ERASE, sizeof(args), &args, 0, nullptr);
}

bool PicobootClient::flashWrite(uint32_t addr, const uint8_t *buffer, uint32_t len)
{
    picoboot_range_cmd args = {};
    args.dAddr = addr;
    args.dSize = len;
    return cmd(PC_WRITE, sizeof(args), &args, len, (uint8_t *)buffer);
}

bool PicobootClient::flashRead(uint32_t addr, uint8_t *buffer, uint32_t len)
{
    picoboot_range_cmd args = {};
    args.dAddr = addr;
    args.dSize = len;
    return cmd(PC_READ, sizeof(args), &args, len, buffer);
}

bool PicobootClient::reboot()
{
    if (_chip == FN_PICO_CHIP_RP2350) {
        // PC_REBOOT is RP2040 only; NORMAL boots as if power-cycled.
        picoboot_reboot2_cmd args = {};
        args.dFlags = REBOOT2_FLAG_REBOOT_TYPE_NORMAL;
        args.dDelayMS = PICOBOOT_REBOOT_DELAY_MS;
        args.dParam0 = 0;
        args.dParam1 = 0;
        cmd(PC_REBOOT2, sizeof(args), &args, 0, nullptr);
    } else {
        // dPC = 0: normal boot, not entry into a RAM image.
        picoboot_reboot_cmd args = {};
        args.dPC = 0;
        args.dSP = 0;
        args.dDelayMS = PICOBOOT_REBOOT_DELAY_MS;
        cmd(PC_REBOOT, sizeof(args), &args, 0, nullptr);
    }

    // The ack races the reboot, so a timeout here is expected; whether the
    // companion came back is decided by watching it re-enumerate.
    _last_error[0] = '\0';
    return true;
}

// ---------------------------------------------------------------------------
// The whole job
// ---------------------------------------------------------------------------

bool PicobootClient::flashImage(const fn_pico_blob &blob, progress_fn progress,
                                 void *progress_arg)
{
    _last_error[0] = '\0';

    if (!_dev_hdl) {
        setError("no device bound");
        return false;
    }
    if (!blob.data || !blob.size) {
        setError("image '%s' is empty", blob.name ? blob.name : "?");
        return false;
    }
    if (blob.chip != _chip) {
        // An RP2040 image on an RP2350 writes fine and then does not boot.
        setError("image '%s' is for chip %u but the attached device is chip %u",
                 blob.name, (unsigned)blob.chip, (unsigned)_chip);
        return false;
    }

    uint32_t sectors = (blob.size + PICOBOOT_FLASH_SECTOR_SIZE - 1)
                       / PICOBOOT_FLASH_SECTOR_SIZE;
    uint32_t end = blob.flash_base + sectors * PICOBOOT_FLASH_SECTOR_SIZE;
    if (blob.flash_limit && end > blob.flash_limit) {
        // build_pico.py checks this too; this is the code that would do it.
        setError("image '%s' would be written up to 0x%08x, past its limit 0x%08x",
                 blob.name, (unsigned)end, (unsigned)blob.flash_limit);
        return false;
    }

    // Clear anything left over from a previous session.
    ifReset();

    // EJECT hides mass storage, so a host auto-mounting RPI-RP2 cannot
    // race us mid-write.
    if (!exclusiveAccess(EXCLUSIVE_AND_EJECT)) {
        describeFailure("exclusive-access", blob.flash_base);
        return false;
    }
    if (!exitXip()) {
        describeFailure("exit-xip", blob.flash_base);
        return false;
    }

    // Erase and write a sector at a time, straight out of the embedded
    // image. Only the final partial sector is staged, padded with 0xFF.
    int last_pct = -1;
    for (uint32_t i = 0; i < sectors; i++) {
        uint32_t offset = i * PICOBOOT_FLASH_SECTOR_SIZE;
        uint32_t addr = blob.flash_base + offset;
        uint32_t n = blob.size - offset;

        const uint8_t *source;
        if (n >= PICOBOOT_FLASH_SECTOR_SIZE) {
            n = PICOBOOT_FLASH_SECTOR_SIZE;
            source = blob.data + offset;
        } else {
            memcpy(_tail_sector, blob.data + offset, n);
            memset(_tail_sector + n, 0xFF, PICOBOOT_FLASH_SECTOR_SIZE - n);
            source = _tail_sector;
        }

        if (!flashErase(addr, PICOBOOT_FLASH_SECTOR_SIZE)) {
            describeFailure("erase", addr);
            return false;
        }
        if (!flashWrite(addr, source, PICOBOOT_FLASH_SECTOR_SIZE)) {
            describeFailure("write", addr);
            return false;
        }

        if (progress) {
            int pct = (int)(((i + 1) * 100) / sectors);
            if (pct / 10 != last_pct / 10) {
                progress("write", pct, progress_arg);
                last_pct = pct;
            }
        }
    }

    // Read back and compare, or a bad write is only noticed when the
    // companion fails to boot with nothing left to report why. The null
    // destination compares in the USB transfer buffer: no second 4K.
    last_pct = -1;
    for (uint32_t i = 0; i < sectors; i++) {
        uint32_t offset = i * PICOBOOT_FLASH_SECTOR_SIZE;
        uint32_t addr = blob.flash_base + offset;
        uint32_t n = blob.size - offset;
        if (n > PICOBOOT_FLASH_SECTOR_SIZE)
            n = PICOBOOT_FLASH_SECTOR_SIZE;

        if (!flashRead(addr, nullptr, PICOBOOT_FLASH_SECTOR_SIZE)) {
            describeFailure("verify-read", addr);
            return false;
        }

        const uint8_t *got = (const uint8_t *)_data_xfer->data_buffer;
        if (memcmp(got, blob.data + offset, n) != 0) {
            setError("verify mismatch at 0x%08x", (unsigned)addr);
            return false;
        }
        // Padding not erased means the erase silently did not take.
        for (uint32_t j = n; j < PICOBOOT_FLASH_SECTOR_SIZE; j++) {
            if (got[j] != 0xFF) {
                setError("verify mismatch in sector padding at 0x%08x",
                         (unsigned)(addr + j));
                return false;
            }
        }

        if (progress) {
            int pct = (int)(((i + 1) * 100) / sectors);
            if (pct / 10 != last_pct / 10) {
                progress("verify", pct, progress_arg);
                last_pct = pct;
            }
        }
    }

    reboot();
    return true;
}

#endif // CONFIG_USB_PICOBOOT_HOST_ENABLED
