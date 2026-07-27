#ifndef SIOFUJI_H
#define SIOFUJI_H

#include "fujiDevice.h"
#include "cassette.h"
#include "netstream.h"
#include "hash.h"

#include <cassert>

class sioFuji : public fujiDevice
{
private:
    sioCassette _cassetteDev;
    sioNetStream _streamDev;

protected:
    size_t set_additional_direntry_details(fsdir_entry_t *f, uint8_t *dest,
                                           uint8_t maxlen) override;

    void sio_net_set_ssid(const FujiSIOPacket &packet);                  // 0xFB
    void sio_read_directory_block();                                  // 0xF6
    void sio_set_baudrate(const FujiSIOPacket &packet);                  // 0xEB
    void sio_new_disk();                                              // 0xE7
    void sio_set_hsio_index(const FujiSIOPacket &packet);                // 0xE3
    void sio_copy_file(const FujiSIOPacket &packet);                     // 0xD8
    void sio_enable_netstream(const FujiSIOPacket &packet);              // 0xF0

    void sio_random_number();                                         // 0xD3

    void sio_status(const FujiSIOPacket &packet) override { fujicmd_status(); }
    void sio_process(const FujiSIOPacket &packet) override;

public:
    sioFuji();
    void setup() override;
    void insert_boot_device(uint8_t image_id, mediatype_t disk_type,
                            DISK_DEVICE *disk_dev) override;

    // Used by sio.cpp
    void debug_tape();
    sioCassette *cassette() { return &_cassetteDev; };

    // If enabled, honor SIO boot-priority: delay our status reply so
    // a real D1: can boot first.
    bool status_wait_enabled = true;

    // ============ Wrapped Fuji commands ============
    success_is_true fujicore_mount_disk_image_success(uint8_t deviceSlot,
                                                      disk_access_flags_t access_mode) override;
    std::optional<std::vector<uint8_t>> fujicore_read_app_key() override;
    void fujicmd_net_scan_networks() override;
};

extern sioFuji platformFuji;

#endif // SIOFUJI_H
