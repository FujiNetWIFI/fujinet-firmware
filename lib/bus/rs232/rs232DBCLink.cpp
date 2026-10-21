#ifdef BUILD_RS232

#include "rs232DBCLink.h"

#include "rs232.h"

std::unique_ptr<FujiBusPacket> rs232DBCLink::exchange(FujiBusPacket &packet)
{
    SYSTEM_BUS.writeBusPacket(packet);
    return SYSTEM_BUS.readBusPacket();
}

#endif /* BUILD_RS232 */
