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
    status_response.length = htole16(SERIAL_BUF_SIZE);
    status_response.devtype = ADAMNET_DEVTYPE_CHAR;
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

void adamSerial::command_recv()
{
}

void adamSerial::adamnet_response_status()
{
    status_response.status = 1;
    virtualDevice::adamnet_response_status();
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

void adamSerial::adamnet_control_send()
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

void adamSerial::adamnet_process(const FujiAdamPacket &packet)
{
    switch (packet.type())
    {
    case APT::MN_STATUS:
        adamnet_control_status();
        break;
    case APT::MN_CLR:
        adamnet_control_clr();
        break;
    case APT::MN_RECEIVE:
        command_recv();
        break;
    case APT::MN_SEND:
        adamnet_control_send();
        break;
    case APT::MN_READY:
        adamnet_control_ready();
        break;
    default:
        break;
    }
}

#endif /* BUILD_ADAM */
