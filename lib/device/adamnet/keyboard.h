#ifndef ADAM_KEYBOARD_H
#define ADAM_KEYBOARD_H

#include <cstdint>
#include <queue>

#include "bus.h"

#include "fnTcpServer.h"

class adamKeyboard : public virtualDevice
{
protected:
    void adamnet_control_receive() override;
    void adamnet_control_ready() override;

    void shutdown() override;

public:

    adamKeyboard();
    ~adamKeyboard();

private:
    fnTcpServer *server;
    fnTcpClient client;
};

#endif /* ADAM_KEYBOARD_H */
