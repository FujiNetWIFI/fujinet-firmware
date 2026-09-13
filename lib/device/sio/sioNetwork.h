#ifndef SIONETWORK_H
#define SIONETWORK_H

#include "NDevice.h"

class sioNetwork : public NDevice
{
public:
    void sio_status(const FujiSIOPacket &packet) override { return fujidev_status(packet); }
    void sio_process(const FujiSIOPacket &packet) override;

private:
    /**
     * @brief Get DSTATS value for a given network command
     * Allows programs to query the data direction for any command.
     */
    void sio_get_dstats_value(const FujiSIOPacket &packet);

protected:
    /**
     * Get the DSTATS value for a given network command
     * @param command The network command code
     * @return The DSTATS byte value (0x00, 0x40, 0x80, or 0xFF for invalid)
     */
    AtariSIODirection get_dstats_for_command(fujiCommandID_t command);

    void fujidev_get_prefix(const FUJI_COMMAND_PACKET &packet) override;
    void fujidev_set_prefix(const FUJI_COMMAND_PACKET &packet) override;
    void fujidev_seek(const FUJI_COMMAND_PACKET &packet) override;
    void fujidev_tell(const FUJI_COMMAND_PACKET &packet) override;
};

#endif /* SIONETWORK_H */
