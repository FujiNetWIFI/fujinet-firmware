#ifdef BUILD_IEC

#include "iecNetwork.h"

void iecNetwork::fujidev_write(const FUJI_COMMAND_PACKET &packet)
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    NDevice::fujicore_write(packet.data().value_or(ByteBuffer{}));
    SYSTEM_BUS.transaction_success();
}

#endif /* BUILD_IEC */
