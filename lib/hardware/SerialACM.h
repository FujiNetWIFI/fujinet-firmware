#ifndef SERIALACM_H
#define SERIALACM_H

#ifdef CONFIG_USB_CDC_ACM_HOST_ENABLED

#include "SerialInterface.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <usb/cdc_acm_host.h>

class SerialACM : public SerialInterface
{
private:
    SemaphoreHandle_t device_disconnected_sem;
    cdc_acm_dev_hdl_t cdc_dev = NULL;
    QueueHandle_t rxQueue;

protected:
    void checkRXQueue() override;

public:
    void begin();
    void end() override;
    size_t send(const void *buffer, size_t length) override;

    // Reference only — inherited from base class, do not override.
    // Provided here for visibility; this is NOT a declaration.
    // size_t recv(void *buffer, size_t length) override;

    void flush() override;

    uint32_t getBaudrate() override;
    void setBaudrate(uint32_t baud) override;

    bool dtrState() override;

    // public because forwarder function needs it
    void eventReceived(const cdc_acm_host_dev_event_data_t *event);
    void dataReceived(const uint8_t *data, size_t length);
};

#endif /* CONFIG_USB_CDC_ACM_HOST_ENABLED */

#endif /* SERIALACM_H */
