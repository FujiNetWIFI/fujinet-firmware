#ifdef BUILD_ADAM

#include <cstring>
#include "adamnet/serial.h"

#define SERIAL_BUF_SIZE 16

adamSerial::adamSerial()
{
    Debug_printf("Serial Start\n");
    server = new fnTcpServer(1235, 1); // Run a TCP server on port 1235.
    server->begin(1235);
    response_len=0;
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
        response_len=client.available() > SERIAL_BUF_SIZE ? SERIAL_BUF_SIZE : client.available();
        int c = client.read(response,response_len);
        Debug_printf("RCV %d %d\n",response_len,c);
    }
    else
        adamnet_response_ack();
}

void adamSerial::adamnet_control_status()
{
    unsigned short s = SERIAL_BUF_SIZE;
    
    status_msg[0]=s & 0xFF;
    status_msg[1]=s >> 8;

    if (!client.connected() && server->hasClient())
    {
        // Accept waiting connection
        client = server->available();
    }

    if (client.available())
    {
        status_msg[3] = client.available() > SERIAL_BUF_SIZE ? SERIAL_BUF_SIZE : client.available();
    }
    else
        status_msg[3] = 0x00;

    adamnet_send(0x80 | _devnum);
    adamnet_send_buffer(status_msg, 4);
    adamnet_send(adamnet_checksum(status_msg, 4));
}

void adamSerial::adamnet_control_clr()
{
    if (response_len == 0)
    {
        adamnet_response_nack();
    }
    else
    {
        Debug_printf("CLR %d\n",response_len);
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
    adamnet_response_ack();
}

void adamSerial::adamnet_control_send()
{
    uint16_t s = adamnet_recv_length();
    uint8_t b[SERIAL_BUF_SIZE];

    adamnet_recv_buffer(b, s);
    adamnet_recv();
    adamnet_response_ack();
    
    if (client.connected())
        client.write(b, s);
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