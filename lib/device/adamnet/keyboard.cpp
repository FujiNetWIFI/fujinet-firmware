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
        SYSTEM_BUS.sendNakPacket();
        client = server->client();
    }
    else if (!client.connected())
    {
        SYSTEM_BUS.sendNakPacket();
    }
    else if (client.available() > 0)
    {
        SYSTEM_BUS.sendAckPacket();
        SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
        SYSTEM_BUS.transaction_send(client.read());
    }
    else
    {
        SYSTEM_BUS.sendNakPacket();
    }
}

void adamKeyboard::adamnet_control_ready()
{
    SYSTEM_BUS.sendAckPacket();
}

void adamKeyboard::shutdown()
{
}
#endif /* BUILD_ADAM */
