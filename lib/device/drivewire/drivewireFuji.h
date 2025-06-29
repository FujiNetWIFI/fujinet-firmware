#ifndef DRIVEWIREFUJI_H
#define DRIVEWIREFUJI_H

#include "fujiDevice.h"

#ifdef UNUSED
#include <cstdint>
#include <cstring>
#include <compat_string.h>
#include "bus.h"
#include "disk.h"
#include "network.h"
#endif /* UNUSED */
#include "cassette.h"

#ifdef UNUSED
#include "fujiHost.h"
#include "fujiDisk.h"
#include "fujiCmd.h"

#include "hash.h"
#endif /* UNUSED */

#define IMAGE_EXTENSION ".dsk"

class drivewireFuji : public fujiDevice
{
private:
    systemBus *_drivewire_bus = nullptr;

    bool wifiScanStarted = false;

    char dirpath[256];

    std::string _response;

    uint8_t _errorCode;

    fujiHost _fnHosts[MAX_HOSTS];

    fujiDisk _fnDisks[MAX_DISK_DEVICES];

    Hash::Algorithm algorithm = Hash::Algorithm::UNKNOWN;

#ifdef ESP_PLATFORM
    drivewireCassette _cassetteDev;
#endif

    int _current_open_directory_slot = -1;

    uint8_t bootMode = 0; // Boot mode 0 = CONFIG, 1 = MINI-BOOT

    uint8_t _countScannedSSIDs = 0;

    appkey _current_appkey;

protected:
    void transaction_complete() override {
        _errorCode = 1;
        _response.clear();
        _response.shrink_to_fit();
    }
    void transaction_error() override {
        _errorCode = 144;
    }
    bool transaction_get(void *data, size_t len) override {
        return fnDwCom.readBytes((uint8_t *) data, len) == len;
    }
    void transaction_put(const void *data, size_t len, bool err=false) override {
        transaction_complete();
        _response.append((char *) data, len);
        if (err)
            transaction_error();
    }

#ifdef NOT_SUBCLASS
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
#endif /* NOT_SUBCLASS */
    void new_disk();               // 0xE7
#ifdef NOT_SUBCLASS
    void unmount_host();           // 0xE6
    void get_directory_position(); // 0xE5
    void set_directory_position(); // 0xE4
    void set_hdrivewire_index();   // 0xE3
    void set_device_filename();    // 0xE2
    void set_host_prefix();        // 0xE1
    void get_host_prefix();        // 0xE0
    void set_drivewire_external_clock(); // 0xDF
    void write_app_key();          // 0xDE
    void read_app_key();           // 0xDD
    void open_app_key();           // 0xDC
    void close_app_key();          // 0xDB
    void get_device_filename();    // 0xDA
    void set_boot_config();        // 0xD9
    void copy_file();              // 0xD8
    void set_boot_mode();          // 0xD6
#endif /* NOT_SUBCLASS */
    void random();                 // 0xD3
    void base64_encode_input();    // 0xD0
    void base64_encode_compute();  // 0xCF
    void base64_encode_length();   // 0xCE
    void base64_encode_output();   // 0xCD
    void base64_decode_input();    // 0xCC
    void base64_decode_compute();  // 0xCB
    void base64_decode_length();   // 0xCA
    void base64_decode_output();   // 0xC9
    void hash_input();             // 0xC8
    void hash_compute(bool clear_data); // 0xC7, 0xC3
    void hash_length();            // 0xC6
    void hash_output();            // 0xC5
    void get_adapter_config_extended(); // 0xC4
    void hash_clear();             // 0xC2

    void send_error();             // 0x02
    void send_response();          // 0x01
    void ready();                  // 0x00
    void shutdown() override;

public:
    drivewireDisk bootdisk; // special disk drive just for configuration

    bool boot_config = true;

    bool status_wait_enabled = true;

    drivewireNetwork *network();

#ifdef ESP_PLATFORM
    drivewireCassette *cassette() { return &_cassetteDev; };
#endif
    void debug_tape();

#ifdef NOT_SUBCLASS
    void insert_boot_device(uint8_t d);
#endif /* NOT_SUBCLASS */

    void setup(systemBus *drivewirebus);

#ifdef NOT_SUBCLASS
    void image_rotate();
    int get_disk_id(int drive_slot);
    std::string get_host_prefix(int host_slot);

    fujiHost *get_hosts(int i) { return &_fnHosts[i]; }
    fujiDisk *get_disks(int i) { return &_fnDisks[i]; }
    fujiHost *set_slot_hostname(int host_slot, char *hostname);

    void _populate_slots_from_config();
    void _populate_config_from_slots();
#endif /* NOT_SUBCLASS */

    void process();

#ifdef NOT_SUBCLASS
    void mount_all();              // 0xD7
#endif /* NOT_SUBCLASS */

    drivewireFuji();
};

extern drivewireFuji platformFuji;

#endif // DRIVEWIREFUJI_H
