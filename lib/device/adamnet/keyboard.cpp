#ifdef BUILD_ADAM

#include "keyboard.h"


TaskHandle_t kbTask;

// ctor
adamKeyboard::adamKeyboard()
{
    server = new fnTcpServer(1234, 1); // Run a TCP server on port 1234.
    server->begin(1234);
    // xTaskCreatePinnedToCore(&timer_task,"KBTask",4096,NULL,10,&kbTask,1);
}

// dtor
adamKeyboard::~adamKeyboard()
{
    // vTaskDelete(kbTask);
    server->stop();
    delete server;
    server = nullptr;
}

void adamKeyboard::adamnet_control_status()
{
    uint8_t r[6] = {0x81, 0x01, 0x00, 0x00, 0x00, 0x01};
    AdamNet.wait_for_idle();
    adamnet_send_buffer(r, sizeof(r));
}

void adamKeyboard::adamnet_control_receive()
{
    if (!client.connected() && server->hasClient())
    {
        AdamNet.wait_for_idle();
        adamnet_send(0xC1); // NAK
        client = server->client();
    }
    else if (!client.connected())
    {
        AdamNet.wait_for_idle();
        adamnet_send(0xC1); // NAK
    }
    else if (!kpQueue.empty())
    {
        AdamNet.wait_for_idle();
        adamnet_send(0x91); // ACK
    }
    else if (client.available() > 0)
    {
        AdamNet.wait_for_idle();
        adamnet_send(0x91); // ACK
        kpQueue.push(client.read());
    }
    else
    {
        AdamNet.wait_for_idle();
        adamnet_send(0xC1); // NAK
    }
}

void adamKeyboard::adamnet_control_clr()
{
    uint8_t r[5] = {0xB1, 0x00, 0x01, 0x00, 0x00};

    r[3] = r[4] = kpQueue.front();
    adamnet_send_buffer(r, sizeof(r));
    kpQueue.pop();
}

void adamKeyboard::adamnet_control_ready()
{
    AdamNet.wait_for_idle();
    adamnet_send(0x91); // Ack
}

void adamKeyboard::adamnet_process(uint8_t b)
{
    unsigned char c = b >> 4;

    switch (c)
    {
    case MN_STATUS:
        adamnet_control_status();
        break;
    case MN_RECEIVE:
        adamnet_control_receive();
        break;
    case MN_CLR:
        adamnet_control_clr();
        break;
    case MN_READY:
        adamnet_control_ready();
        break;
    }
}

void adamKeyboard::shutdown()
{
}
#endif /* BUILD_ADAM */
