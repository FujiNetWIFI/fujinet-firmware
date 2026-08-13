#ifdef BUILD_ADAM

#include "serial.h"

#include <cstring>

#include "../../include/debug.h"
#include "fuji_endian.h"

#define SERIAL_BUF_SIZE 16

adamSerial::adamSerial()
{
    Debug_printf("Serial Start\n");
    response_len = 0;
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
    SYSTEM_BUS.start_time=GET_TIMESTAMP();

#ifdef ESP_PLATFORM
    if (uxQueueMessagesWaiting(serial_out_queue))
        adamnet_response_nack();
    else
#endif /* ESP_PLATFORM */
        adamnet_response_ack();
}

void adamSerial::adamnet_idle()
{
}

void adamSerial::adamnet_control_send(const FujiAdamPacket &packet)
{
    next.len = adamnet_recv_length();

    if (next.len > sizeof(next.data)) // clamp wire length to buffer
        next.len = sizeof(next.data);

    adamnet_recv_buffer(next.data, next.len);
    adamnet_recv();

    SYSTEM_BUS.start_time = GET_TIMESTAMP();
    adamnet_response_ack();

#ifdef UNUSED
    // There is no matching xQueueReceive()
    xQueueSend(serial_out_queue,&next,portMAX_DELAY);
#endif /* UNUSED */
}

#endif /* BUILD_ADAM */
