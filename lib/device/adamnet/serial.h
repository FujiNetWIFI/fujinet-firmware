#ifndef ADAM_SERIAL_H
#define ADAM_SERIAL_H

#include <cstdint>

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
    void adamnet_process(const FujiAdamPacket &packet);

    void command_connect(uint16_t s);
    void command_send(uint16_t s);
    void command_recv();

    /**
     * return adamnet status
     */
    void adamnet_idle();
    void adamnet_response_status();
    void adamnet_control_ready();

    void adamnet_control_send();

#ifdef ESP_PLATFORM
    /**
     * Queue Handle
     */
    QueueHandle_t serial_out_queue;
#endif /* ESP_PLATFORM */

private:

#ifdef ESP_PLATFORM
    /**
     * Task handle for TX task
     */
    TaskHandle_t thSerial;
#endif /* ESP_PLATFORM */

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
