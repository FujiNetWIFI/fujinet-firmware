#ifdef BUILD_RS232

#include "rs232Network.h"

// RS232 uses variable length queries
void rs232Network::fujidev_set_query(const FUJI_COMMAND_PACKET &packet)
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    NDevice::fujicore_set_query(packet.dataAsString().value_or(""), 0);
    SYSTEM_BUS.transaction_success();
}

#endif /* BUILD_RS232 */
