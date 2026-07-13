#ifndef FUJIDEVICEMIXIN_H
#define FUJIDEVICEMIXIN_H

#include "bus.h"

#ifdef FUJI_COMMAND_PACKET
#define FUJI_MIXINS_ENABLED
#endif

#ifdef FUJI_MIXINS_ENABLED

#include "bus.h"

class FujiDeviceMixin : public virtual virtualDevice
{
public:
    virtual bool processCommand(const FUJI_COMMAND_PACKET &packet) { return false; }
};

#endif // FUJI_MIXINS_ENABLED

#endif /* FUJIDEVICEMIXIN_H */
