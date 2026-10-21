#ifdef BUILD_COCO

#include "drivewireDBCLink.h"

#include "../../include/debug.h"
#include "../../include/pinmap.h"

#include "drivewire.h"

std::unique_ptr<FujiBusPacket> drivewireDBCLink::exchange(FujiBusPacket &packet)
{
#ifdef FN_DBC_COMPANION
    if (SYSTEM_BUS.isBoIP())
    {
        Debug_printv("DBC needs a real pico; not supported over BoIP");
        return nullptr;
    }

    SYSTEM_BUS.writeBusPacket(packet);
    return SYSTEM_BUS.readBusPacket();
#else
    (void)packet;
    Debug_printv("DBC not supported on this FujiNet hardware.");
    return nullptr;
#endif
}

#endif /* BUILD_COCO */
