#ifndef FUJI_H
#define FUJI_H

#include "fujiDevice.h"

#include <cstdint>
#include <cstring>

#include "bus.h"
#include "network.h"
#include "cassette.h"

#include "fujiHost.h"
#include "fujiDisk.h"

#define STATUS_MOUNT_TIME       0x01

class rs232Fuji : public virtualDevice
{
private:
    fujiHost _fnHosts[MAX_HOSTS];

    fujiDisk _fnDisks[MAX_DISK_DEVICES];

    int _current_open_directory_slot = -1;

    rs232Disk _bootDisk; // special disk drive just for configuration

    uint8_t bootMode = 0; // Boot mode 0 = CONFIG, 1 = MINI-BOOT

    uint8_t _countScannedSSIDs = 0;

    appkey _current_appkey;

protected:
    void rs232_reset_fujinet();          // 0xFF
    void rs232_net_get_ssid();           // 0xFE
    void rs232_net_scan_networks();      // 0xFD
    void rs232_net_scan_result();        // 0xFC
    void rs232_net_set_ssid();           // 0xFB
    void rs232_net_get_wifi_status();    // 0xFA
    void rs232_mount_host();             // 0xF9
    void rs232_disk_image_mount();       // 0xF8
    void rs232_open_directory();         // 0xF7
    void rs232_read_directory_entry();   // 0xF6
    void rs232_close_directory();        // 0xF5
    void rs232_read_host_slots();        // 0xF4
    void rs232_write_host_slots();       // 0xF3
    void rs232_read_device_slots();      // 0xF2
    void rs232_write_device_slots();     // 0xF1
    void rs232_enable_udpstream();       // 0xF0
    void rs232_net_get_wifi_enabled();   // 0xEA
    void rs232_disk_image_umount();      // 0xE9
    void rs232_get_adapter_config();     // 0xE8
    void rs232_get_adapter_config_extended(); // 0xC4
    void rs232_new_disk();               // 0xE7
    void rs232_unmount_host();           // 0xE6
    void rs232_get_directory_position(); // 0xE5
    void rs232_set_directory_position(); // 0xE4
    void rs232_set_hrs232_index();         // 0xE3
    void rs232_set_device_filename();    // 0xE2
    void rs232_set_host_prefix();        // 0xE1
    void rs232_get_host_prefix();        // 0xE0
    void rs232_set_rs232_external_clock(); // 0xDF
    void rs232_write_app_key();          // 0xDE
    void rs232_read_app_key();           // 0xDD
    void rs232_open_app_key();           // 0xDC
    void rs232_close_app_key();          // 0xDB
    void rs232_get_device_filename();    // 0xDA
    void rs232_set_boot_config();        // 0xD9
    void rs232_copy_file();              // 0xD8
    void rs232_set_boot_mode();          // 0xD6
    void rs232_test();                   // 0x00

    void rs232_status() override;
    void rs232_process(cmdFrame_t *cmd_ptr) override;

    void shutdown() override;

public:
    bool boot_config = true;

    bool status_wait_enabled = true;

    rs232Disk *bootdisk();

    rs232Network *network();

    void debug_tape();

    void insert_boot_device(uint8_t d);

    void setup();

    void image_rotate();
    int get_disk_id(int drive_slot);
    std::string get_host_prefix(int host_slot);

    fujiHost *get_host(int i) { return &_fnHosts[i]; }
    fujiDisk *get_disk(int i) { return &_fnDisks[i]; }
    fujiHost *set_slot_hostname(int host_slot, char *hostname);

    void _populate_slots_from_config();
    void _populate_config_from_slots();

    void mount_all();              // 0xD7

    rs232Fuji();
};

extern rs232Fuji *theFuji;

#endif // FUJI_H
