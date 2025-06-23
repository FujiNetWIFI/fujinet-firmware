#ifdef BUILD_APPLE
#ifndef IWMFUJI_H
#define IWMFUJI_H
#include "fujiDevice.h"

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "../../include/debug.h"
#include "bus.h"
#include "iwm/disk2.h"
#include "iwm/network.h"
#include "iwm/printer.h"
#include "iwm/cpm.h"
#include "iwm/clock.h"
#include "iwm/modem.h"

#include "../fuji/fujiHost.h"
#include "../fuji/fujiDisk.h"
#include "../fuji/fujiCmd.h"

#include "hash.h"
#include "../../qrcode/qrmanager.h"

#define MAX_SP_DEVICES 4
#define MAX_DISK2_DEVICES 2 // for now until we add 3.5" disks
#define MAX_A2DISK_DEVICES (MAX_SP_DEVICES + MAX_DISK2_DEVICES)

#define MAX_SSID_LEN 32
#define MAX_WIFI_PASS_LEN 64

#define MAX_APPKEY_LEN 64

#define READ_DEVICE_SLOTS_DISKS1 0x00
#define READ_DEVICE_SLOTS_TAPE 0x10

using IWMCmdHandlers = std::function<void(iwm_decoded_cmd_t)>;
using IWMControlHandlers = std::function<void()>;
using IWMStatusHandlers = std::function<void()>;

class iwmFuji : public fujiDevice
{
private:
    bool isReady = false;
    bool alreadyRunning = false; // Replace isReady and scanStarted with THIS.
    bool scanStarted = false;
    bool hostMounted[MAX_HOSTS];
    bool setSSIDStarted = false;

    //uint8_t response[1024]; // use packet_buffer instead
    //uint16_t response_len;

    // Response to SIO_FUJICMD_GET_SCAN_RESULT
    struct
    {
        char ssid[MAX_SSID_LEN + 1];
        uint8_t rssi;
    } detail;

    systemBus *_iwm_bus;

    fujiHost _fnHosts[MAX_HOSTS];

    fujiDisk _fnDisks[MAX_DISK_DEVICES];
#ifndef DEV_RELAY_SLIP
    iwmDisk2 _fnDisk2s[MAX_DISK2_DEVICES];
#endif

    iwmNetwork *theNetwork;

    iwmCPM *theCPM;

    iwmClock *theClock;

    int _current_open_directory_slot = -1;

    iwmDisk *_bootDisk; // special disk drive just for configuration

    uint8_t bootMode = 0; // Boot mode 0 = CONFIG, 1 = MINI-BOOT

    uint8_t _countScannedSSIDs = 0;

    char _appkeyfilename[30]; // Temp storage for appkey filename, populated by open and read by read/write

    uint8_t ctrl_stat_buffer[767]; // what is proper length
    size_t ctrl_stat_len = 0; // max payload length is 767

    char dirpath[256];

    std::unordered_map<uint8_t, IWMCmdHandlers> command_handlers;
    std::unordered_map<uint8_t, IWMControlHandlers> control_handlers;
    std::unordered_map<uint8_t, IWMStatusHandlers> status_handlers;

    Hash::Algorithm algorithm = Hash::Algorithm::UNKNOWN;
    bool hash_is_hex_output = false;

protected:
    void transaction_complete() override {}
    void transaction_error() override {}
    bool transaction_get(void *data, size_t len) override {
        if (len > sizeof(data_buffer))
            return false;
        memcpy((uint8_t *) data, data_buffer, len);
        return true;
    }
    void transaction_put(void *data, size_t len, bool err) override {
        // Move into response.
        memcpy(data_buffer, data, len);
        data_len = len;
    }

    void iwm_dummy_command();                     // control 0xAA
    void iwm_hello_world();                       // status 0xAA
    void iwm_ctrl_reset_fujinet();                // control 0xFF
    void iwm_stat_net_get_ssid();                 // status 0xFE
    void iwm_stat_net_scan_networks();            // status 0xFD
    void iwm_ctrl_net_scan_result();              // control 0xFC
    void iwm_stat_net_scan_result();              // status 0xFC
    void iwm_ctrl_net_set_ssid();                 // control 0xFB
    void iwm_stat_net_get_wifi_status();          // status 0xFA
    void iwm_ctrl_mount_host();                   // 0xF9
    uint8_t iwm_ctrl_disk_image_mount();          // 0xF8
    uint8_t iwm_ctrl_open_directory();            // 0xF7
    void iwm_ctrl_read_directory_entry();         // 0xF6
    void iwm_stat_read_directory_entry();         // 0xF6

    void iwm_ctrl_close_directory();              // 0xF5
    void iwm_stat_read_host_slots();              // 0xF4
    void iwm_ctrl_write_host_slots();             // 0xF3
    void iwm_stat_read_device_slots();            // 0xF2
    void iwm_ctrl_write_device_slots();           // 0xF1
    void iwm_stat_get_wifi_enabled();             // 0xEA
    void iwm_ctrl_disk_image_umount();            // 0xE9
    void iwm_stat_get_adapter_config();           // 0xE8
    void iwm_stat_get_adapter_config_extended();  // 0xC4 (additional cmd data)
    void iwm_ctrl_new_disk();                     // 0xE7
    void iwm_ctrl_unmount_host();                 // 0xE6

    void iwm_stat_get_directory_position();       // 0xE5
    void iwm_ctrl_set_directory_position();       // 0xE4
/*
    void adamnet_set_hadamnet_index();            // 0xE3
*/
    uint8_t iwm_ctrl_set_device_filename();       // 0xE2

    void iwm_ctrl_set_host_prefix();              // 0xE1
    void iwm_stat_get_host_prefix();              // 0xE0
/*
    void adamnet_set_adamnet_external_clock();    // 0xDF
*/
    void iwm_ctrl_write_app_key();                // 0xDE
    void iwm_ctrl_read_app_key();                 // 0xDD - control
    void iwm_stat_read_app_key();                 // 0xDD - status
    void iwm_ctrl_open_app_key();                 // 0xDC

/*  void adamnet_close_app_key();                 // 0xDB
*/
    void iwm_stat_get_device_filename(uint8_t s); // 0xDA, 0xA0 thru 0xA7

    void iwm_ctrl_set_boot_config();              // 0xD9
    void iwm_ctrl_copy_file();                    // 0xD8
    void iwm_ctrl_set_boot_mode();                // 0xD6
    void iwm_ctrl_enable_device();                // 0xD5
    void iwm_ctrl_disable_device();               // 0xD4
    void send_stat_get_enable();                  // 0xD1

    void iwm_ctrl_hash_input();                   // 0xC8
    void iwm_ctrl_hash_compute(bool clear_data);  // 0xC7, 0xC3
    void iwm_stat_hash_length();                  // 0xC6
    void iwm_ctrl_hash_output();                  // 0xC5 set hash_is_hex_output
    void iwm_stat_hash_output();                  // 0xC5 write response
    void iwm_ctrl_hash_clear();                   // 0xC2
    void iwm_stat_get_heap();                     // 0xC1

    void iwm_ctrl_qrcode_input();                 // 0xBC
    void iwm_ctrl_qrcode_encode();                // 0xBD
    void iwm_stat_qrcode_length();                // OxBE
    void iwm_ctrl_qrcode_output();                // 0xBF
    void iwm_stat_qrcode_output();                // 0xBF

    void iwm_stat_fuji_status();                  // 0x53

    void shutdown() override;
    void process(iwm_decoded_cmd_t cmd) override;

    void iwm_ctrl(iwm_decoded_cmd_t cmd) override;
    void iwm_open(iwm_decoded_cmd_t cmd) override;
    void iwm_close(iwm_decoded_cmd_t cmd) override;
    void iwm_read(iwm_decoded_cmd_t cmd) override;
    void iwm_status(iwm_decoded_cmd_t cmd) override;

    void send_status_reply_packet() override;
    void send_status_dib_reply_packet() override;

    void send_extended_status_reply_packet() override{};
    void send_extended_status_dib_reply_packet() override{};

    // map appkey open modes to key sizes. The open will set the appkey_size to correct value for subsequent reads to ensure the returned block is the correct size
    int appkey_size = 64;
    std::map<int, int> mode_to_keysize = {
        {0, 64},
        {2, 256}
    };

public:
    bool boot_config = true;

    bool status_wait_enabled = true;

    uint8_t err_result = SP_ERR_NOERROR;
    bool status_completed = false;
    uint8_t status_code;

    iwmDisk *bootdisk();

    void debug_tape();

    void insert_boot_device(uint8_t d);

    void setup(systemBus *iwmbus);

    void image_rotate();
    int get_disk_id(int drive_slot);
    void handle_ctl_eject(uint8_t spid);
    std::string get_host_prefix(int host_slot);

    fujiHost *get_hosts(int i) { return &_fnHosts[i]; }
    fujiDisk *get_disks(int i) { return &_fnDisks[i]; }
    fujiHost *set_slot_hostname(int host_slot, char *hostname);
    DEVICE_TYPE *get_disk_dev(int i) {
#ifndef DEV_RELAY_SLIP
      return i < MAX_SP_DEVICES
        ? (DEVICE_TYPE *) &_fnDisks[i].disk_dev
        : (DEVICE_TYPE *) &_fnDisk2s[i - MAX_SP_DEVICES];
#else
      return &_fnDisks[i].disk_dev;
#endif
    }

    bool mount_all();              // 0xD7

    void FujiStatus(iwm_decoded_cmd_t cmd) { iwm_status(cmd); }
    void FujiControl(iwm_decoded_cmd_t cmd) { iwm_ctrl(cmd); }

    iwmFuji();

    // virtual void startup_hack() override { Debug_printf("\n Fuji startup hack"); }
};

extern iwmFuji platformFuji;

#endif // IWMFUJI_H
#endif /* BUILD_APPLE */
