#ifndef RS232NETWORK_H
#define RS232NETWORK_H

#include "NDevice.h"

class rs232Network : public NDevice
{
protected:
    void rs232_process(const FujiBusPacket &packet) { NDevice::processCommand(packet); }
    void fujidev_set_query(const FUJI_COMMAND_PACKET &packet) override;
};

#endif /* RS232NETWORK_H */
