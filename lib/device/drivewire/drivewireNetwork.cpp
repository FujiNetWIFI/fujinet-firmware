#ifdef BUILD_COCO

#include "drivewireNetwork.h"

// fujinet-lib sends the mode in aux1, fujinet-lib-experimental in both.
void drivewireNetwork::fujidev_set_parser(const FUJI_COMMAND_PACKET &packet)
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);

    parserMode_t mode = param_cast<parserMode_t>(packet, 0);

    if (fujicore_set_parser(mode).is_error())
        SYSTEM_BUS.transaction_error();
    else
        SYSTEM_BUS.transaction_success();
}

#endif /* BUILD_COCO */
