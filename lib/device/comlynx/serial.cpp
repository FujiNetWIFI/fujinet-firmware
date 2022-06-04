#ifdef BUILD_LYNX

#include "serial.h"

#include <cstring>

#include "../../include/debug.h"

#define SERIAL_BUF_SIZE 16

lynxSerial::lynxSerial()
{
    Debug_printf("Serial Start\n");
    response_len = 0;
    status_response[1] = 0x10;
    status_response[2] = 0x00;
    status_response[3] = 0x00; // character device
    serial_out_queue = xQueueCreate(16, sizeof(SendData));
}

lynxSerial::~lynxSerial()
{
    vQueueDelete(serial_out_queue);
}

void lynxSerial::command_recv()
{
}

void lynxSerial::comlynx_response_status()
{
    status_response[4] = 1;
    virtualDevice::comlynx_response_status();
}

void lynxSerial::comlynx_control_ready()
{
    ComLynx.start_time=esp_timer_get_time();

    if (uxQueueMessagesWaiting(serial_out_queue))
        comlynx_response_nack();
    else
        comlynx_response_ack();
}

void lynxSerial::comlynx_idle()
{
}

void lynxSerial::comlynx_control_send()
{
    next.len = comlynx_recv_length();

    comlynx_recv_buffer(next.data, next.len);
    comlynx_recv();

    ComLynx.start_time = esp_timer_get_time();
    comlynx_response_ack();

    xQueueSend(serial_out_queue,&next,portMAX_DELAY);
}

void lynxSerial::comlynx_process(uint8_t b)
{
    unsigned char c = b >> 4;

    switch (c)
    {
    case MN_STATUS:
        comlynx_control_status();
        break;
    case MN_CLR:
        comlynx_control_clr();
        break;
    case MN_RECEIVE:
        command_recv();
        break;
    case MN_SEND:
        comlynx_control_send();
        break;
    case MN_READY:
        comlynx_control_ready();
        break;
    }
}

#endif /* BUILD_LYNX */