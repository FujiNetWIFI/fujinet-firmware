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
            sp->client.write(c);
        }
    }
}

adamSerial::adamSerial()
{
    Debug_printf("Serial Start\n");
    server = new fnTcpServer(1235, 1); // Run a TCP server on port 1235.
    server->begin(1235);
    response_len = 0;
    status_response[3] = 0x00; // character device
    serial_out_queue = xQueueCreate(16, sizeof(uint8_t));
    xTaskCreate(serial_task, "adamnet_serial", 2048, this, 0, NULL);
}

adamSerial::~adamSerial()
{
    server->stop();
    delete server;
}

void adamSerial::command_recv()
{
    if ((response_len == 0) && client.available())
    {
        adamnet_response_nack();
        response_len = client.available() > SERIAL_BUF_SIZE ? SERIAL_BUF_SIZE : client.available();
        int c = client.read(response, response_len);
    }
    else
        adamnet_response_ack();
}

void adamSerial::adamnet_response_status()
{
    uint8_t s[6] = {0x8E,0x10,0x00,0x00,0x00,0x00};

    if (!client.connected() && server->hasClient())
        client = server->available();

    if (client.available())
        s[4] = client.available();

    s[5]=adamnet_checksum(&s[1],4);

    adamnet_send_buffer(s,6);
}

void adamSerial::adamnet_control_clr()
{
    if (response_len == 0)
    {
        adamnet_response_nack();
    }
    else
    {
        adamnet_send(0xB0 | _devnum);
        adamnet_send_length(response_len);
        adamnet_send_buffer(response, response_len);
        adamnet_send(adamnet_checksum(response, response_len));
        memset(response, 0, sizeof(response));
        response_len = 0;
    }
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
    uint8_t ck;

    adamnet_recv_buffer(sendbuf, s);
    ck = adamnet_recv();

    AdamNet.start_time = esp_timer_get_time();
    adamnet_response_ack();

    xQueueSend(serial_out_queue,&sendbuf[0],portMAX_DELAY);
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