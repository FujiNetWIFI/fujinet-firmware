#ifndef ADAM_KEYBOARD_H
#define ADAM_KEYBOARD_H

#include <cstdint>
#include <queue>

#include "bus.h"

#include "fnTcpServer.h"

class adamKeyboard : public virtualDevice
{
protected:
    // SIO THINGS
    
    virtual void adamnet_control_status();
    virtual void adamnet_control_receive();
    virtual void adamnet_control_clr();
    virtual void adamnet_control_ready();

    void adamnet_process(uint8_t b) override;
    void shutdown() override;

public:

    adamKeyboard();
    ~adamKeyboard();

private:
    fnTcpServer *server;
    fnTcpClient client;
    std::queue<uint8_t> kpQueue;
};

#endif /* ADAM_KEYBOARD_H */