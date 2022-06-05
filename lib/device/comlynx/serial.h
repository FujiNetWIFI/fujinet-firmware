#ifndef LYNX_SERIAL_H
#define LYNX_SERIAL_H

#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "bus.h"

#include "fnTcpClient.h"
#include "fnTcpServer.h"

class lynxSerial : public virtualDevice
{
    public:

    /**
     * Constructor
     */
    lynxSerial();

    /**
     * Destructor
     */
    virtual ~lynxSerial();

    /**
     * Process incoming SIO command for device 0x7X
     * @param b first byte of packet
     */
    virtual void comlynx_process(uint8_t b);

    void command_connect(uint16_t s);
    void command_send(uint16_t s);
    void command_recv();

    /**
     * return comlynx status
     */
    virtual void comlynx_idle();
    virtual void comlynx_response_status();
    virtual void comlynx_control_ready();
    
    void comlynx_control_send();

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

#endif /* LYNX_SERIAL_H */