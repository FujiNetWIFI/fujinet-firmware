#ifdef BUILD_MAC
#ifndef FUJI_H
#define FUJI_H
#include <cstdint>

#include "../../include/debug.h"
#include "bus.h"

#include "mac/floppy.h"
// #include "mac/network.h"
#include "mac/printer.h"
// #include "iwm/cpm.h"
// #include "iwm/clock.h"
#include "mac/modem.h"

#include "../fuji/fujiHost.h"
#include "../fuji/fujiDisk.h"

#define MAX_HOSTS 8
#define MAX_DISK_DEVICES 5 // 4 DCD devices + 1 floppy devices
#define MAX_FLOPPY_DEVICES 1
#define MAX_NETWORK_DEVICES 4

#define MAX_SSID_LEN 32
#define MAX_WIFI_PASS_LEN 64

#define MAX_APPKEY_LEN 64

#define READ_DEVICE_SLOTS_DISKS1 0x00
#define READ_DEVICE_SLOTS_TAPE 0x10

typedef struct
{
    char ssid[MAX_SSID_LEN + 1];
    char hostname[64];
    unsigned char localIP[4];
    unsigned char gateway[4];
    unsigned char netmask[4];
    unsigned char dnsIP[4];
    unsigned char macAddress[6];
    unsigned char bssid[6];
    char fn_version[15];
} AdapterConfig;

enum appkey_mode : int8_t
{
    APPKEYMODE_INVALID = -1,
    APPKEYMODE_READ = 0,
    APPKEYMODE_WRITE,
    APPKEYMODE_READ_256
};

struct appkey
{
    uint16_t creator = 0;
    uint8_t app = 0;
    uint8_t key = 0;
    appkey_mode mode = APPKEYMODE_INVALID;
    uint8_t reserved = 0;
} __attribute__((packed));

class macFuji : public macDevice
{
private:
    bool isReady = false;
    bool alreadyRunning = false; // Replace isReady and scanStarted with THIS.
    bool scanStarted = false;
    bool hostMounted[MAX_HOSTS];
    bool setSSIDStarted = false;
    // uint8_t err_result = SP_ERR_NOERROR;

    //uint8_t response[1024]; // use packet_buffer instead
    //uint16_t response_len;

    // Response to FUJICMD_GET_SCAN_RESULT
    struct
    {
        char ssid[MAX_SSID_LEN + 1];
        uint8_t rssi;
    } detail;

    fujiHost _fnHosts[MAX_HOSTS];

    fujiDisk _fnDisks[MAX_DISK_DEVICES];

    // iwmNetwork *theNetwork;

    // iwmCPM *theCPM;

    // iwmClock *theClock;

    int _current_open_directory_slot = -1;

    macFloppy *_bootDisk; // special disk drive just for configuration
    // iwmDisk *_bootDisk; // special disk drive just for configuration

    uint8_t bootMode = 0; // Boot mode 0 = CONFIG, 1 = MINI-BOOT

    uint8_t _countScannedSSIDs = 0;

    appkey _current_appkey;

    // uint8_t ctrl_stat_buffer[767]; // what is proper length
    // size_t ctrl_stat_len = 0; // max payload length is 767

    char dirpath[256];

protected:
//     void iwm_dummy_command();                    // control 0xAA
//     void iwm_hello_world();                      // status 0xAA
//     void iwm_ctrl_reset_fujinet();               // control 0xFF
//     void iwm_stat_net_get_ssid();                // status 0xFE
//     void iwm_stat_net_scan_networks();           // status 0xFD
//     void iwm_ctrl_net_scan_result();             // control 0xFC
//     void iwm_stat_net_scan_result();             // status 0xFC
//     void iwm_ctrl_net_set_ssid();                // control 0xFB
//     void iwm_stat_net_get_wifi_status();         // status 0xFA
//     void iwm_ctrl_mount_host();                  // 0xF9
//     void iwm_ctrl_disk_image_mount();            // 0xF8
//     void iwm_ctrl_open_directory();              // 0xF7
//     void iwm_ctrl_read_directory_entry();        // 0xF6
//     void iwm_stat_read_directory_entry();        // 0xF6

//     void iwm_ctrl_close_directory();        // 0xF5
//     void iwm_stat_read_host_slots();        // 0xF4
//     void iwm_ctrl_write_host_slots();       // 0xF3
//     void iwm_stat_read_device_slots();      // 0xF2
//     void iwm_ctrl_write_device_slots();     // 0xF1
//     void iwm_ctrl_disk_image_umount();      // 0xE9
//     void iwm_stat_get_adapter_config();     // 0xE8
//     void iwm_ctrl_new_disk();               // 0xE7
//     void iwm_ctrl_unmount_host();           // 0xE6

//    void iwm_stat_get_directory_position(); // 0xE5
//    void iwm_ctrl_set_directory_position(); // 0xE4
    /*
    void adamnet_set_hadamnet_index();         // 0xE3
     */
//    void iwm_ctrl_set_device_filename();    // 0xE2

//     void iwm_ctrl_set_host_prefix();        // 0xE1
//     void iwm_stat_get_host_prefix();        // 0xE0
    /*
    void adamnet_set_adamnet_external_clock(); // 0xDF
    */
    // void iwm_ctrl_write_app_key();          // 0xDE
    // void iwm_ctrl_read_app_key();           // 0xDD - control
    // void iwm_stat_read_app_key();             // 0xDD - status
    /*
    void adamnet_open_app_key();           // 0xDC
    void adamnet_close_app_key();          // 0xDB
    */
    // void iwm_ctrl_get_device_filename();    // 0xDA
    // void iwm_stat_get_device_filename();    // 0xDA

    // void iwm_ctrl_set_boot_config();            // 0xD9
    // void iwm_ctrl_copy_file();                  // 0xD8
    // void iwm_ctrl_set_boot_mode();              // 0xD6
    // void iwm_ctrl_enable_device();          // 0xD5
    // void iwm_ctrl_disable_device();         // 0xD4
    // void send_stat_get_enable();        // 0xD1



    // void iwm_ctrl(iwm_decoded_cmd_t cmd) override;
    // void iwm_open(iwm_decoded_cmd_t cmd) override;
    // void iwm_close(iwm_decoded_cmd_t cmd) override;
    // void iwm_read(iwm_decoded_cmd_t cmd) override;
    // void iwm_status(iwm_decoded_cmd_t cmd) override;

    // void send_status_reply_packet() override;
    // void send_status_dib_reply_packet() override;

    // void send_extended_status_reply_packet() override{};
    // void send_extended_status_dib_reply_packet() override{};

public:
    bool boot_config = true;

    bool status_wait_enabled = true;

    // iwmDisk *bootdisk();
    macFloppy *bootdisk();

    // 27-Aug-23 get it online: void debug_tape() {};

    // 27-Aug-23 get it online: void insert_boot_device(uint8_t d) {};

    void setup();

    // 27-Aug-23 get it online: void image_rotate() {};
    int get_disk_id(int drive_slot);
    // 27-Aug-23 get it online: void handle_ctl_eject(uint8_t spid) {};
    std::string get_host_prefix(int host_slot);

    fujiHost *get_hosts(int i) { return &_fnHosts[i]; }
    fujiDisk *get_disks(int i) { return &_fnDisks[i]; }
    fujiHost *set_slot_hostname(int host_slot, char *hostname);

    void _populate_slots_from_config();
    void _populate_config_from_slots();

    bool mount_all();              // 0xD7

    // void FujiStatus(iwm_decoded_cmd_t cmd) { iwm_status(cmd); }
    // void FujiControl(iwm_decoded_cmd_t cmd) { iwm_ctrl(cmd); }

    macFuji();
    ~macFuji(){};

    void startup_hack();
    void shutdown() override;
    void process(mac_cmd_t cmd) override {};

};



extern macFuji theFuji;

#endif // guard
#endif // BUILD_MAC

#if 0
#ifndef FUJI_H
#define FUJI_H
#include <cstdint>

#include "../../include/debug.h"
#include "bus.h"
#include "iwm/disk2.h"
#include "iwm/network.h"
#include "iwm/printer.h"
#include "iwm/cpm.h"
#include "iwm/clock.h"
#include "iwm/modem.h"




#endif // FUJI_H
#endif /* BUILD_APPLE */
