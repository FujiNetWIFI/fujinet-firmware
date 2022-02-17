#ifdef BUILD_ADAM

#include "serial.h"

#include <cstring>

#include "../../include/debug.h"

#define SERIAL_BUF_SIZE 16

static xQueueHandle serial_out_queue = NULL;

void serial_task(void *param)
{
    adamSerial *sp = (adamSerial *)param;
    uint8_t c = 0;

    for (;;)
    {
        if (uxQueueMessagesWaiting(serial_out_queue))
        {
            xQueueReceive(serial_out_queue,&c,portMAX_DELAY);
        }
    }
}

adamSerial::adamSerial()
{
    Debug_printf("Serial Start\n");
    response_len = 0;
    status_response[3] = 0x00; // character device
    serial_out_queue = xQueueCreate(16, sizeof(uint8_t));
    xTaskCreate(serial_task, "adamnet_serial", 2048, this, 0, NULL);
}

adamSerial::~adamSerial()
{
}

void adamSerial::command_recv()
{
}

void adamSerial::adamnet_response_status()
{
}

void adamSerial::adamnet_control_ready()
{
    AdamNet.start_time=esp_timer_get_time();

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
    uint16_t s = adamnet_recv_length();

    adamnet_recv_buffer(response, s);
    adamnet_recv();

    AdamNet.start_time = esp_timer_get_time();
    adamnet_response_ack();

    xQueueSend(serial_out_queue,&response[0],portMAX_DELAY);
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