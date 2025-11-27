#ifndef LYNXFUJI_H
#define LYNXFUJI_H

#include "fujiDevice.h"

#include <cstdint>

#include "network.h"
#include "disk.h"
#include "serial.h"

#include "fujiHost.h"
#include "fujiDisk.h"
#include "fujiDevice.h"

class lynxFuji : public fujiDevice
{
private:
#ifdef OBSOLETE
    bool isReady = false;
    bool alreadyRunning = false; // Replace isReady and scanStarted with THIS.
    bool scanStarted = false;
    bool hostMounted[MAX_HOSTS];
    bool setSSIDStarted = false;

    uint8_t response[1024];
    uint16_t response_len = 0;

    fujiHost _fnHosts[MAX_HOSTS];

    fujiDisk _fnDisks[MAX_DISK_DEVICES];

    int _current_open_directory_slot = -1;

    lynxDisk *_bootDisk = nullptr; // special disk drive just for configuration

    uint8_t bootMode = 0; // Boot mode 0 = CONFIG, 1 = MINI-BOOT

    uint8_t _countScannedSSIDs = 0;

    appkey _current_appkey;
#endif /* OBSOLETE */

protected:
    void transaction_complete() override {
        comlynx_response_ack();
    }
    void transaction_error() override {
        comlynx_response_nack();
    }
    bool transaction_get(void *data, size_t len) override {
        unsigned short rlen = comlynx_recv_buffer((uint8_t *) data, len);
        return rlen == len;
    }
    void transaction_put(const void *data, size_t len, bool err=false) override {
        memcpy(response, data, len);
        response_len = len;
    }

    size_t setDirEntryDetails(fsdir_entry_t *f, uint8_t *dest, uint8_t maxlen) override;

#ifdef OBSOLETE
    void comlynx_reset_fujinet();          // 0xFF
    void comlynx_net_get_ssid();           // 0xFE
    void comlynx_net_scan_networks();      // 0xFD
    void comlynx_net_scan_result();        // 0xFC
    void comlynx_net_set_ssid(uint16_t s);           // 0xFB
    void comlynx_net_get_wifi_status();    // 0xFA
    void comlynx_mount_host();             // 0xF9
    void comlynx_disk_image_mount();       // 0xF8
    void comlynx_open_directory(uint16_t s);         // 0xF7
    void comlynx_read_directory_entry();   // 0xF6
    void comlynx_close_directory();        // 0xF5
    void comlynx_read_host_slots();        // 0xF4
    void comlynx_write_host_slots();       // 0xF3
    void comlynx_read_device_slots();      // 0xF2
    void comlynx_write_device_slots();     // 0xF1
    void comlynx_enable_udpstream(uint16_t s);       // 0xF0
    void comlynx_disk_image_umount();      // 0xE9
    void comlynx_get_adapter_config();     // 0xE8
#endif /* OBSOLETE */
    void comlynx_new_disk();               // 0xE7
#ifdef OBSOLETE
    void comlynx_unmount_host();           // 0xE6
    void comlynx_get_directory_position(); // 0xE5
    void comlynx_set_directory_position(); // 0xE4
    void comlynx_set_hcomlynx_index();         // 0xE3
    void comlynx_set_device_filename(uint16_t s);    // 0xE2
    void comlynx_set_host_prefix();        // 0xE1
    void comlynx_get_host_prefix();        // 0xE0
    void comlynx_set_comlynx_external_clock(); // 0xDF
#endif /* OBSOLETE */
    void comlynx_write_app_key();          // 0xDE
#ifdef OBSOLETE
    void comlynx_read_app_key();           // 0xDD
    void comlynx_open_app_key();           // 0xDC
    void comlynx_close_app_key();          // 0xDB
    void comlynx_get_device_filename();    // 0xDA
#endif /* OBSOLETE */
    void comlynx_set_boot_config();        // 0xD9
#ifdef OBSOLETE
    void comlynx_copy_file();              // 0xD8
    void comlynx_set_boot_mode();          // 0xD6
#endif /* OBSOLETE */
    void comlynx_enable_device();          // 0xD5
    void comlynx_disable_device();         // 0xD4
    void comlynx_random_number();          // 0xD3
    void comlynx_get_time();               // 0xD2
    void comlynx_device_enable_status();   // 0xD1
#ifdef OBSOLETE
    void comlynx_get_copy_status();        // 0xD0
#endif /* OBSOLETE */

    void comlynx_hello(); // test

    void comlynx_test_command();

    void comlynx_control_status() override;
    void comlynx_control_send();
    void comlynx_control_clr();

    void comlynx_process(uint8_t b) override;

    void shutdown() override;

public:
#ifdef OBSOLETE
    bool boot_config = true;

    bool status_wait_enabled = true;

    lynxDisk *bootdisk();

    lynxNetwork *network();

    void debug_tape();

    void insert_boot_device(uint8_t d);
#endif /* OBSOLETE */

    void setup() override;

#ifdef OBSOLETE
    void image_rotate();
    int get_disk_id(int drive_slot);
    std::string get_host_prefix(int host_slot);

    fujiHost *get_host(int i) { return &_fnHosts[i]; }
    fujiDisk *get_disk(int i) { return &_fnDisks[i]; }
    fujiHost *set_slot_hostname(int host_slot, char *hostname);

    void _populate_slots_from_config();
    void _populate_config_from_slots();

    void mount_all();              // 0xD7
#endif /* OBSOLETE */

    lynxFuji();

#ifdef OBSOLETE
    std::string copySpec;
    unsigned char sourceSlot;
    unsigned char destSlot;
    std::string sourcePath;
    std::string destPath;
    FILE *sourceFile;
    FILE *destFile;
    char *dataBuf;
    TaskHandle_t copy_task_handle;
#endif /* OBSOLETE */
};

#ifdef OBSOLETE
extern lynxFuji *theFuji;
extern lynxSerial *theSerial;
#endif /* OBSOLETE */

#endif // LYNXFUJI_H
