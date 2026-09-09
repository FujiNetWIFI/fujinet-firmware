#ifdef BUILD_LYNX

#include "lynxNetwork.h"

void lynxNetwork::fujidev_read(const FUJI_COMMAND_PACKET &packet)
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);

    // Packet payload can't be larger than 16bit unsigned
    size_t num_bytes = std::min<size_t>(65535, fujicore_available());

    if (!num_bytes)
    {
        SYSTEM_BUS.transaction_error();
        return;
    }

    ByteBuffer buf;
    if (fujicore_read(buf, num_bytes).is_error())
    {
        SYSTEM_BUS.transaction_error();
        return;
    }

    SYSTEM_BUS.transaction_send(buf);
}

#endif /* BUILD_LYNX */
