#ifdef BUILD_ADAM

#include "serial.h"

#include <cstring>

#include "../../include/debug.h"
#include "fuji_endian.h"

#define SERIAL_BUF_SIZE 16

adamSerial::adamSerial()
{
    Debug_printf("Serial Start\n");
#ifdef ESP_PLATFORM
    serial_out_queue = xQueueCreate(16, sizeof(SendData));
#endif /* ESP_PLATFORM */
}

adamSerial::~adamSerial()
{
#ifdef ESP_PLATFORM
    vQueueDelete(serial_out_queue);
#endif /* ESP_PLATFORM */
}

void adamSerial::adamnet_control_receive()
{
}

AdamNetStatus adamSerial::deviceStatus()
{
    AdamNetStatus status;

    status.length = SERIAL_BUF_SIZE;
    status.devtype = ADAMNET_DEVTYPE::CHAR;
    status.status = 1;

    return status;
}

void adamSerial::adamnet_control_ready()
{
#ifdef ESP_PLATFORM
    if (uxQueueMessagesWaiting(serial_out_queue))
        SYSTEM_BUS.sendNakPacket();
    else
#endif /* ESP_PLATFORM */
        SYSTEM_BUS.sendAckPacket();
}

void adamSerial::adamnet_idle()
{
}

void adamSerial::adamnet_control_send(const FujiAdamPacket &packet)
{
    memcpy(next.data, packet.data()->data(),
           std::min(sizeof(next.data), packet.data()->size()));

#ifdef UNUSED
    // There is no matching xQueueReceive()
    xQueueSend(serial_out_queue,&next,portMAX_DELAY);
#endif /* UNUSED */
}

#endif /* BUILD_ADAM */
