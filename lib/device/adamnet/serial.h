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

    void adamnet_control_send(const FujiAdamPacket &packet) override;
    void adamnet_control_ready() override;
    void adamnet_control_receive() override;

    void adamnet_idle();
    void adamnet_response_status();

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
