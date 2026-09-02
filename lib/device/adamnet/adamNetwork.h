#ifndef ADAMNETWORK_H
#define ADAMNETWORK_H

#include "NDevice.h"
#include "bus.h"

class adamNetwork : public NDevice
{
protected:
    union _status
    {
        struct _statusbits
        {
            bool client_data_available : 1;
            bool client_connected : 1;
            bool client_error : 1;
            bool server_connection_available : 1;
            bool server_error : 1;
        } bits;
        unsigned char byte;
    } statusByte;

    AdamNetStatus deviceStatus() override;
    void adamnet_control_send(const FujiAdamPacket &packet) override {
        NDevice::processCommand(packet);
    }
    void adamnet_control_receive() override;

    // Adam uses variable length packets, no length parameter is passed
    void fujidev_write(const FUJI_COMMAND_PACKET &packet) override;
};

#endif /* ADAMNETWORK_H */
