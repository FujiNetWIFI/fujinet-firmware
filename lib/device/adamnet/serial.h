#ifndef ADAM_SERIAL_H
#define ADAM_SERIAL_H

#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "bus.h"

#include "fnTcpClient.h"
#include "fnTcpServer.h"

class adamSerial : public virtualDevice
{
    public:

    /**
     * Constructor
     */
    adamSerial();

    /**
     * Destructor
     */
    virtual ~adamSerial();

    /**
     * Process incoming SIO command for device 0x7X
     * @param b first byte of packet
     */
    virtual void adamnet_process(uint8_t b);

    void command_connect(uint16_t s);
    void command_send(uint16_t s);
    void command_recv();

    /**
     * return adamnet status
     */
    virtual void adamnet_idle();
    virtual void adamnet_response_status();
    virtual void adamnet_control_ready();
    
    void adamnet_control_send();

    /**
     * Queue Handle
     */
    xQueueHandle serial_out_queue;

private:

    /**
     * Task handle for TX task
     */
    TaskHandle_t thSerial;

    /**
     * Send Structure
     */
    typedef struct _sendData
    {
        uint8_t len;
        uint8_t data[16];
    } SendData;

    SendData next;

};

#endif /* ADAM_SERIAL_H */