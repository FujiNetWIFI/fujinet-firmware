#ifndef ACMCHANNEL_H
#define ACMCHANNEL_H

#ifdef CONFIG_USB_CDC_ACM_HOST_ENABLED

#include "IOChannel.h"
#include "RS232ChannelProtocol.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <usb/cdc_acm_host.h>

// Installs the USB host library (usb_host_install() + its event-pump task)
// exactly once, however many of ACMChannel/PicobootClient/etc call it. Only
// one caller may ever call usb_host_install() directly -- a second call
// asserts. Whichever of ACMChannel::begin() / PicobootClient::begin() runs
// first does the real install; callers must invoke this BEFORE registering
// their own usb_host_client, and should do so as early as possible (ideally
// before systemBus::setup() blocks on anything), since a USB device already
// attached at boot can finish enumerating -- and its NEW_DEV event go out --
// before a late-registering client is listening for it.
void usbHostEnsureInstalled();

class ACMChannel : public IOChannel, public RS232ChannelProtocol
{
private:
    SemaphoreHandle_t device_disconnected_sem;
    SemaphoreHandle_t device_connected_sem;
    uint16_t found_vid, found_pid, found_interface;

    cdc_acm_dev_hdl_t cdc_dev = NULL;
    QueueHandle_t rxQueue;
    cdc_acm_uart_state_t _serial_state;
    bool _dsr, _cts;
    cdc_acm_host_device_config_t dev_config = {};

    // FreeRTOS priority for the USB worker tasks; the owner may override it
    // (e.g. to win CPU against WiFi during startup) via setServicePriority().
    UBaseType_t _service_priority = 20;

    // Optional VID/PID filter, see setExpectedDevice(). 0/0 means "accept
    // any CDC-ACM function", the historical behavior.
    uint16_t _expected_vid = 0, _expected_pid = 0;

    // Opens cdc_dev for found_vid/found_pid/found_interface and negotiates
    // line coding. Returns false if the open itself failed (nothing else to
    // clean up); does not touch device_connected_sem.
    bool openDevice();

protected:
    void updateFIFO() override;
    size_t dataOut(const void *buffer, size_t length) override;

public:
    void begin();
    void end() override;

    // Set the FreeRTOS priority of the USB worker tasks. Takes effect at the
    // next begin(), and is applied immediately if they are already running.
    void setServicePriority(UBaseType_t priority);

    // Restrict newDevice() to a known VID/PID, e.g. the RP2040's own
    // (0xCafe/0x4001 for fuji_intv with MSC removed) on boards where the far
    // end of the link is a specific, soldered-down chip -- so a device in
    // BOOTSEL/PICOBOOT mode (a different PID) or anything else stray on the
    // bus can never be mistaken for the real link. Call before begin().
    // Leave unset (default) for boards where the far end is any generic
    // USB-serial adapter, e.g. plain rs232/drivewire.
    void setExpectedDevice(uint16_t vid, uint16_t pid) { _expected_vid = vid; _expected_pid = pid; }

    void flushOutput() override;

    uint32_t getBaudrate() override { return 0; }
    void setBaudrate(uint32_t baud) override { return; }

    // public because forwarder function needs it
    void eventReceived(const cdc_acm_host_dev_event_data_t *event);
    void dataReceived(const uint8_t *data, size_t length);
    void newDevice(usb_device_handle_t usb_dev);

    // Runs for the life of the program on its own task: reopens the device
    // whenever newDevice() signals device_connected_sem again after the
    // first connection. Without this, a disconnect/replug (e.g. the RP2040
    // resetting) is never noticed -- begin()'s one-shot wait loop stops
    // listening the moment it succeeds once. Public for the same reason as
    // the callbacks above: reconnectTaskForwarder() needs it.
    void reconnectTask();

    // FujiNet acts as modem (DCE), computer serial ports are DTE.
    // API names follow the modem (DCE) view, but the actual RS-232 pin differs.
    bool getDTR() override;           // modem DTR input  → actually reads RS-232 DSR pin
    void setDSR(bool state) override; // modem DSR output → actually drives RS-232 DTR pin
    bool getRTS() override;           // modem RTS input  → actually reads RS-232 CTS pin
    void setCTS(bool state) override; // modem CTS output → actually drives RS-232 RTS pin
    bool getDCD() override;           // DTE DCD input
    bool getRI() override;            // DTE RI input

    void setDCD(bool state) override { return; } // DCD is not an output on DTE
    void setRI(bool state) override { return; }  // RI is not an output on DTE
};

#endif /* CONFIG_USB_CDC_ACM_HOST_ENABLED */

#endif /* ACMCHANNEL_H */
