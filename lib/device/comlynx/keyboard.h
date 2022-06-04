#ifndef LYNX_KEYBOARD_H
#define LYNX_KEYBOARD_H

#include <cstdint>
#include <queue>

#include "bus.h"

#include "fnTcpServer.h"

class lynxKeyboard : public virtualDevice
{
protected:
    // SIO THINGS
    
    virtual void comlynx_control_status() override;
    virtual void comlynx_control_receive();
    virtual void comlynx_control_clr();
    virtual void comlynx_control_ready() override;

    void comlynx_process(uint8_t b) override;
    void shutdown() override;

public:

    lynxKeyboard();
    ~lynxKeyboard();

private:
    fnTcpServer *server;
    fnTcpClient client;
    std::queue<uint8_t> kpQueue;
};

#endif /* LYNX_KEYBOARD_H */