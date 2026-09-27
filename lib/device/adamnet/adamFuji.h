#ifndef ADAMFUJI_H
#define ADAMFUJI_H

#include "fujiDevice.h"

#include <cstdint>

#include "adamNetwork.h"
#include "disk.h"
#include "serial.h"

#include "fujiHost.h"
#include "fujiDisk.h"

class adamFuji : public fujiDevice
{
private:
    bool new_disk_completed = false;

protected:
    size_t set_additional_direntry_details(fsdir_entry_t *f, uint8_t *dest,
                                           uint8_t maxlen) override;

    void adamnet_new_disk(const FujiAdamPacket &packet);               // 0xE7
    void adamnet_enable_device(const FujiAdamPacket &packet);          // 0xD5
    void adamnet_disable_device(const FujiAdamPacket &packet);         // 0xD4
    void adamnet_get_time();               // 0xD2
    void adamnet_device_enable_status(const FujiAdamPacket &packet);   // 0xD1

    void adamnet_test_command();

    void adamnet_control_send(const FujiAdamPacket &packet) override;

    AdamNetStatus deviceStatus() override;

    void fujidev_set_device_fullpath(const FUJI_COMMAND_PACKET &packet) override;

    void shutdown() override;

public:
    bool status_wait_enabled = true;

    adamDisk *bootDisk = nullptr; // special disk drive just for configuration

    adamNetwork *network();

    void debug_tape();

    void insert_boot_device(uint8_t d);

    void setup() override;

    adamFuji();

    // ============ Wrapped Fuji commands ============
    void fujicmd_read_directory_entry(size_t maxlen, uint8_t addtl) override;
};

#endif // ADAMFUJI_H
