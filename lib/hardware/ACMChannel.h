#ifndef ACMCHANNEL_H
#define ACMCHANNEL_H

#ifdef CONFIG_USB_CDC_ACM_HOST_ENABLED

#include "IOChannel.h"
#include "RS232ChannelProtocol.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <usb/cdc_acm_host.h>

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

protected:
    void updateFIFO() override;
    size_t dataOut(const void *buffer, size_t length) override;

public:
    void begin();
    void end() override;

    void flushOutput() override;

    uint32_t getBaudrate() override { return 0; }
    void setBaudrate(uint32_t baud) override { return; }

    // public because forwarder function needs it
    void eventReceived(const cdc_acm_host_dev_event_data_t *event);
    void dataReceived(const uint8_t *data, size_t length);
    void newDevice(usb_device_handle_t usb_dev);

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
