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
    // READ here sends what is available and has no length to carry the sequence bit.
    void fujidev_set_sequence(const FUJI_COMMAND_PACKET &packet) override {
        SYSTEM_BUS.transaction_error();
    }
};

#endif /* BUILD_LYNX */

#endif /* LYNXNETWORK_H */
