#ifndef LYNXNETWORK_H
#define LYNXNETWORK_H

#ifdef BUILD_LYNX

#include "NDevice.h"

class lynxNetwork : public NDevice
{
protected:
    void comlynx_process(const FujiLynxPacket &packet) override {
        processCommand(packet);
    }
    void fujidev_read(const FUJI_COMMAND_PACKET &packet) override;
};

#endif /* BUILD_LYNX */

#endif /* LYNXNETWORK_H */
