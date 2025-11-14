#ifndef FUJI_H
#define FUJI_H

#include <cstdint>
#include <cstring>
#include <string>

#include "bus.h"
#include "network.h"
#include "cassette.h"

#include "fujiHost.h"
#include "fujiDisk.h"

class cx16Fuji : public virtualDevice
{
private:
    fujiHost _fnHosts[MAX_HOSTS];

    fujiDisk _fnDisks[MAX_DISK_DEVICES];

    int _current_open_directory_slot = -1;

    uint8_t _countScannedSSIDs = 0;

    appkey _current_appkey;

protected:
    void reset_fujinet();          // 0xFF
    void net_get_ssid();           // 0xFE
    void net_scan_networks();      // 0xFD
    void net_scan_result();        // 0xFC
    void net_set_ssid();           // 0xFB
    void net_get_wifi_status();    // 0xFA
    void mount_host();             // 0xF9
    void disk_image_mount();       // 0xF8
    void open_directory();         // 0xF7
    void read_directory_entry();   // 0xF6
    void close_directory();        // 0xF5
    void read_host_slots();        // 0xF4
    void write_host_slots();       // 0xF3
    void read_device_slots();      // 0xF2
    void write_device_slots();     // 0xF1
    void enable_udpstream();       // 0xF0
    void net_get_wifi_enabled();   // 0xEA
    void disk_image_umount();      // 0xE9
    void get_adapter_config();     // 0xE8
    void new_disk();               // 0xE7
    void sio_unmount_host();           // 0xE6
    void get_directory_position(); // 0xE5
    void set_directory_position(); // 0xE4
    void set_hsio_index();         // 0xE3
    void set_device_filename();    // 0xE2
    void set_host_prefix();        // 0xE1
    void get_host_prefix();        // 0xE0
    void set_sio_external_clock(); // 0xDF
    void sio_write_app_key();          // 0xDE
    void read_app_key();           // 0xDD
    void open_app_key();           // 0xDC
    void close_app_key();          // 0xDB
    void get_device_filename();    // 0xDA
    void set_boot_config();        // 0xD9
    void copy_file();              // 0xD8
    void set_boot_mode();          // 0xD6

    void status() override;
    void process(uint32_t commanddata, uint8_t checksum) override;

    void shutdown() override;

public:
    bool boot_config = true;

    bool status_wait_enabled = true;

    //sioNetwork *network();

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

    cx16Fuji();
};

extern cx16Fuji *theFuji;

#endif /* FUJI_H */
