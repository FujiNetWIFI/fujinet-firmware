#ifdef BUILD_LYNX

#include "keyboard.h"


TaskHandle_t kbTask;

// ctor
lynxKeyboard::lynxKeyboard()
{
    server = new fnTcpServer(1234, 1); // Run a TCP server on port 1234.
    server->begin(1234);
    // xTaskCreatePinnedToCore(&timer_task,"KBTask",4096,NULL,10,&kbTask,1);
}

// dtor
lynxKeyboard::~lynxKeyboard()
{
    // vTaskDelete(kbTask);
    server->stop();
    delete server;
    server = nullptr;
}

void lynxKeyboard::comlynx_control_status()
{
    uint8_t r[6] = {0x81, 0x01, 0x00, 0x00, 0x00, 0x01};
    SYSTEM_BUS.wait_for_idle();
    comlynx_send_buffer(r, sizeof(r));
}

void lynxKeyboard::comlynx_control_receive()
{
    if (!client.connected() && server->hasClient())
    {
        SYSTEM_BUS.wait_for_idle();
        comlynx_send(0xC1); // NAK
        client = server->client();
    }
    else if (!client.connected())
    {
        SYSTEM_BUS.wait_for_idle();
        comlynx_send(0xC1); // NAK
    }
    else if (!kpQueue.empty())
    {
        SYSTEM_BUS.wait_for_idle();
        comlynx_send(0x91); // ACK
    }
    else if (client.available() > 0)
    {
        SYSTEM_BUS.wait_for_idle();
        comlynx_send(0x91); // ACK
        kpQueue.push(client.read());
    }
    else
    {
        SYSTEM_BUS.wait_for_idle();
        comlynx_send(0xC1); // NAK
    }
}

void lynxKeyboard::comlynx_control_clr()
{
    uint8_t r[5] = {0xB1, 0x00, 0x01, 0x00, 0x00};

    r[3] = r[4] = kpQueue.front();
    comlynx_send_buffer(r, sizeof(r));
    kpQueue.pop();
}

void lynxKeyboard::comlynx_control_ready()
{
    SYSTEM_BUS.wait_for_idle();
    comlynx_send(0x91); // Ack
}

void lynxKeyboard::comlynx_process(uint8_t b)
{
    unsigned char c = b >> 4;

    switch (c)
    {
    case MN_STATUS:
        comlynx_control_status();
        break;
    case MN_RECEIVE:
        comlynx_control_receive();
        break;
    case MN_CLR:
        comlynx_control_clr();
        break;
    case MN_READY:
        comlynx_control_ready();
        break;
    }
}

void lynxKeyboard::shutdown()
{
}
#endif /* BUILD_LYNX */