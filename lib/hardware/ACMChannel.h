#ifndef ACMCHANNEL_H
#define ACMCHANNEL_H

#ifdef CONFIG_USB_CDC_ACM_HOST_ENABLED

#include "IOChannel.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <usb/cdc_acm_host.h>

class ACMChannel : public IOChannel, public RS232ChannelProtocol
{
private:
    SemaphoreHandle_t device_disconnected_sem;
    cdc_acm_dev_hdl_t cdc_dev = NULL;
    QueueHandle_t rxQueue;

protected:
    void update_fifo() override;
    size_t si_send(const void *buffer, size_t length) override;

public:
    void begin();
    void end() override;

    void flushOutput() override;

    uint32_t getBaudrate() override { return 0; }
    void setBaudrate(uint32_t baud) override { return }

    // public because forwarder function needs it
    void eventReceived(const cdc_acm_host_dev_event_data_t *event);
    void dataReceived(const uint8_t *data, size_t length);
};

#endif /* CONFIG_USB_CDC_ACM_HOST_ENABLED */

#endif /* ACMCHANNEL_H */
