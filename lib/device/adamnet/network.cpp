#ifdef BUILD_ADAM

#include <cstring>
#include "adamnet/network.h"

adamNetwork::adamNetwork()
{
    Debug_printf("Network Start\n");
}

adamNetwork::~adamNetwork()
{
}

void adamNetwork::command_connect(uint16_t s)
{
    char hn[256];
    uint16_t p;

    s--; // remove command byte length.

    memset(&hn, 0, 256);

    // Get Port
    adamnet_recv_buffer((uint8_t *)&p, sizeof(uint16_t));

    s--;
    s--; // remove port length.

    // Get hostname
    adamnet_recv_buffer((uint8_t *)hn, s);

    // Get Checksum
    adamnet_recv();

    AdamNet.wait_for_idle();
    adamnet_send(0x9E); // Ack

    Debug_printf("Connecting to: %s port %u\n", hn, p);

    if (client.connected())
    {
        return;
    }

    if (alreadyDoingSomething == false)
    {
        // connect
        if (!client.connect(hn, p))
        {
        }
        else
        {
        }
    }
    Debug_println("Connect done.");
}

void adamNetwork::command_recv()
{
    if (client.available()>0)
    {
        if (response_len == 0)
        {
        int l = (client.available() > 1023 ? 1023 : client.available());
        AdamNet.wait_for_idle();
        adamnet_send(0x9E);
        response_len = client.read(response,l);
        }
        else
        {
            AdamNet.wait_for_idle();
            adamnet_send(0x9E);
        }
    }
    else // No data available.
    {
        AdamNet.wait_for_idle();
        adamnet_send(0xCE); // NAK
    }
}

void adamNetwork::command_send(uint16_t s)
{
    s--;
    adamnet_recv_buffer(response, s);
    adamnet_recv();

    AdamNet.wait_for_idle();
    adamnet_send(0x9E);
    
    client.write(response, s);
}

void adamNetwork::adamnet_control_status()
{
    uint8_t r[6] = {0x8E, 0x00, 0x04, 0x00, 0x00, 0x04};
    adamnet_send_buffer(r, 6);
}

void adamNetwork::adamnet_control_clr()
{
    AdamNet.wait_for_idle();
    adamnet_send(0xBE);
    adamnet_send_length(response_len);
    adamnet_send_buffer(response, response_len);
    adamnet_send(adamnet_checksum(response, response_len));
    memset(response,0,sizeof(response));
    response_len = 0;
}

void adamNetwork::adamnet_control_ready()
{
    AdamNet.wait_for_idle();
    adamnet_send(0x9E); // ACK.
}

void adamNetwork::adamnet_control_send()
{
    uint16_t s = adamnet_recv_length();
    uint8_t c = adamnet_recv();

    switch (c)
    {
    case 0xFF: // CONNECT TO HOST
        command_connect(s);
        break;
    case 0xFE: // SEND TO HOST
        command_send(s);
        break;
    }
}

void adamNetwork::adamnet_process(uint8_t b)
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