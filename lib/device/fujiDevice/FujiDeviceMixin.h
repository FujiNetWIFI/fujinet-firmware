#ifndef FUJIDEVICEMIXIN_H
#define FUJIDEVICEMIXIN_H

#include "bus.h"
#include "fujiCommandID.h"

#include <unordered_map>

class FujiDeviceMixin;
using FujiMixinCommandHandlers = std::unordered_map<
    fujiCommandID_t,
    void (FujiDeviceMixin::*)(const FUJI_COMMAND_PACKET&)
    >;

#define FM_CMD_HANDLER(Method) \
    static_cast<void (FujiDeviceMixin::*)(const FUJI_COMMAND_PACKET&)>(&std::remove_pointer_t<decltype(this)>::Method)


class FujiDeviceMixin : public virtual virtualDevice
{
protected:
    virtual FujiMixinCommandHandlers commandHandlers() = 0;

public:
    bool recognizesCommand(fujiCommandID_t command) {
        auto handlers = commandHandlers();
        return handlers.find(command) != handlers.end();
    }
    virtual bool processCommand(const FUJI_COMMAND_PACKET &packet) {
        auto handlers = commandHandlers();
        auto cmdHandler = handlers.find(packet.command());
        if (cmdHandler == handlers.end())
            return false;
        auto cmdMethod = cmdHandler->second;
        (this->*cmdMethod)(packet);
        return true;
    }
};

#endif /* FUJIDEVICEMIXIN_H */
