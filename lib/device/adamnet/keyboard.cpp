#ifdef BUILD_ADAM

#include "keyboard.h"


//TaskHandle_t kbTask;

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

AdamNetStatus deviceStatus()
{
    AdamNetStatus status;

    status.length = 1;
    status.devtype = ADAMNET_DEVTYPE::CHAR;
    status.status = 0;

    return status;
}

void adamKeyboard::adamnet_control_receive()
{
    if (!client.connected() && server->hasClient())
    {
        SYSTEM_BUS.wait_for_idle();
        adamnet_send(0xC1); // NAK
        client = server->client();
    }
    else if (!client.connected())
    {
        SYSTEM_BUS.wait_for_idle();
        adamnet_send(0xC1); // NAK
    }
    else if (!kpQueue.empty())
    {
        SYSTEM_BUS.wait_for_idle();
        adamnet_send(0x91); // ACK
    }
    else if (client.available() > 0)
    {
        SYSTEM_BUS.wait_for_idle();
        adamnet_send(0x91); // ACK
        kpQueue.push(client.read());
    }
    else
    {
        SYSTEM_BUS.wait_for_idle();
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
    SYSTEM_BUS.wait_for_idle();
    adamnet_send(0x91); // Ack
}

void adamKeyboard::shutdown()
{
}
#endif /* BUILD_ADAM */
