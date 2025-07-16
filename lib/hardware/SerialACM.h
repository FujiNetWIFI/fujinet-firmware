#ifndef SERIALACM_H
#define SERIALACM_H

#include "SerialInterface.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <usb/cdc_acm_host.h>

class SerialACM : public SerialInterface
{
private:
    SemaphoreHandle_t device_disconnected_sem;
    cdc_acm_dev_hdl_t cdc_dev = NULL;
    bool _initialized;
    
public:
    using SerialInterface::read;
    using SerialInterface::write;

    void begin(int baud) override;
    void end() override;
    size_t read(void *buffer, size_t length) override;
    size_t write(const void *buffer, size_t length) override;

    size_t available() override;
    void flush() override;
    void flush_input() override;
    
    uint32_t get_baudrate() override;
    void set_baudrate(uint32_t baud) override;

    bool dtrState() override;

    void handle_event(const cdc_acm_host_dev_event_data_t *event);
};

#endif /* SERIALACM_H */
