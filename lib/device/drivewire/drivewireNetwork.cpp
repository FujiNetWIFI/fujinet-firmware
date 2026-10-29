#ifdef BUILD_COCO

#include "drivewireNetwork.h"

#ifdef ENABLE_OLED_DISPLAY
#include <cstdio>
#include "oled_display.h"
#endif

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

void drivewireNetwork::fujidev_open(const FUJI_COMMAND_PACKET &packet)
{
    NDevice::fujidev_open(packet);

#ifdef ENABLE_OLED_DISPLAY
    if (_protocol != nullptr && urlParser != nullptr)
    {
        char line[24];
        snprintf(line, sizeof(line), "N:%.18s", urlParser->host.c_str());
        fnOledDisplay.show_activity(line);
    }
#endif
}

#endif /* BUILD_COCO */
