#ifndef ADAMFUJI_H
#define ADAMFUJI_H

#include "fujiDevice.h"

#include <cstdint>

#include "network.h"
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

    void adamnet_new_disk();               // 0xE7
    void adamnet_set_boot_config();        // 0xD9
    void adamnet_enable_device();          // 0xD5
    void adamnet_disable_device();         // 0xD4
    void adamnet_random_number();          // 0xD3
    void adamnet_get_time();               // 0xD2
    void adamnet_device_enable_status();   // 0xD1

    void adamnet_test_command();

    void adamnet_control_status() override;
    void adamnet_control_send();
    void adamnet_control_clr();

    void adamnet_process(uint8_t b) override;

    void shutdown() override;

public:
    bool boot_config = true;

    bool status_wait_enabled = true;

    adamDisk *bootDisk = nullptr; // special disk drive just for configuration

    adamNetwork *network();

    void debug_tape();

    void insert_boot_device(uint8_t d);

    void setup() override;

    adamFuji();

    // ============ Wrapped Fuji commands ============
    void fujicmd_read_directory_entry(size_t maxlen, uint8_t addtl) override;
#if 0
    bool fujicmd_mount_disk_image_success(uint8_t deviceSlot, disk_access_flags_t access_mode) override;
    void fujicmd_get_adapter_config() override;
    void fujicmd_get_adapter_config_extended() override;
#endif
};

#endif // ADAMFUJI_H
