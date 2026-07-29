#ifndef FUJIDEVICEMIXIN_H
#define FUJIDEVICEMIXIN_H

#include "bus.h"

#include <unordered_map>

#ifdef FUJI_COMMAND_PACKET
#define FUJI_MIXINS_ENABLED
#endif

#ifdef FUJI_MIXINS_ENABLED

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
    bool recognizesCommand(const FUJI_COMMAND_PACKET &packet) {
        auto handlers = commandHandlers();
        return handlers.find(packet.command()) != handlers.end();
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

#endif // FUJI_MIXINS_ENABLED

#endif /* FUJIDEVICEMIXIN_H */
