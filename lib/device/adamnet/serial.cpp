#ifdef BUILD_ADAM

#include "serial.h"

#include <cstring>

#include "../../include/debug.h"

#define SERIAL_BUF_SIZE 16

adamSerial::adamSerial()
{
    Debug_printf("Serial Start\n");
    response_len = 0;
    status_response[1] = 0x10;
    status_response[2] = 0x00;
    status_response[3] = 0x00; // character device
    serial_out_queue = xQueueCreate(16, sizeof(SendData));
}

adamSerial::~adamSerial()
{
    vQueueDelete(serial_out_queue);
}

void adamSerial::command_recv()
{
}

void adamSerial::adamnet_response_status()
{
    status_response[4] = 1;
    virtualDevice::adamnet_response_status();
}

void adamSerial::adamnet_control_ready()
{
    SYSTEM_BUS.start_time=esp_timer_get_time();

    if (uxQueueMessagesWaiting(serial_out_queue))
        adamnet_response_nack();
    else
        adamnet_response_ack();
}

void adamSerial::adamnet_idle()
{
}

void adamSerial::adamnet_control_send()
{
    next.len = adamnet_recv_length();

    adamnet_recv_buffer(next.data, next.len);
    adamnet_recv();

    SYSTEM_BUS.start_time = esp_timer_get_time();
    adamnet_response_ack();

    xQueueSend(serial_out_queue,&next,portMAX_DELAY);
}

void adamSerial::adamnet_process(uint8_t b)
{
    unsigned char c = b >> 4;

    switch (c)
    {
    case MN_STATUS:
        adamnet_control_status();
        break;
    case MN_CLR:
        adamnet_control_clr();
        break;
    case MN_RECEIVE:
        command_recv();
        break;
    case MN_SEND:
        adamnet_control_send();
        break;
    case MN_READY:
        adamnet_control_ready();
        break;
    }
}

#endif /* BUILD_ADAM */
