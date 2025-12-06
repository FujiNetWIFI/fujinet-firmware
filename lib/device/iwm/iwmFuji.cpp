#ifdef BUILD_APPLE

#include "iwmFuji.h"
#include "httpService.h"
#include "fnSystem.h"
#include "led.h"
#include "utils.h"
#include "compat_string.h"
#include "fuji_endian.h"
#include "../../qrcode/qrmanager.h"

#define DIR_MAX_LEN 40
#define IMAGE_EXTENSION ".po"
#define LOBBY_URL       "tnfs://tnfs.fujinet.online/APPLE2/_lobby.po"

iwmFuji platformFuji;
fujiDevice *theFuji = &platformFuji; // Global fuji object.

iwmFuji::iwmFuji() : fujiDevice(MAX_A2DISK_DEVICES, IMAGE_EXTENSION, LOBBY_URL)
{
        Debug_printf("Announcing the iwmFuji::iwmFuji()!!!\n");
        for (int i = 0; i < MAX_HOSTS; i++)
                _fnHosts[i].slotid = i;

    command_handlers = {
        { SP_CMD_STATUS, [this](iwm_decoded_cmd_t cmd) { iwm_status(cmd); }},                           // 0x00
        { SP_CMD_CONTROL, [this](iwm_decoded_cmd_t cmd) { iwm_ctrl(cmd); }},                            // 0x04
        { SP_CMD_OPEN, [this](iwm_decoded_cmd_t cmd) { iwm_open(cmd); }},                               // 0x06
        { SP_CMD_CLOSE, [this](iwm_decoded_cmd_t cmd) { iwm_close(cmd); }},                              // 0x07
        { SP_CMD_READ, [this](iwm_decoded_cmd_t cmd) { iwm_read(cmd); }},                               // 0x08

        { SP_CMD_READBLOCK, [this](iwm_decoded_cmd_t cmd) { iwm_return_badcmd(cmd); }},                 // 0x01
        { SP_CMD_WRITEBLOCK, [this](iwm_decoded_cmd_t cmd) { iwm_return_badcmd(cmd); }},                // 0x02
        { SP_CMD_FORMAT, [this](iwm_decoded_cmd_t cmd) { iwm_return_badcmd(cmd); }},                    // 0x03
        { SP_CMD_WRITE, [this](iwm_decoded_cmd_t cmd) { iwm_return_badcmd(cmd); }}                      // 0x09
    };

    control_handlers = {
        { 0xAA, [this]()                               { this->iwm_dummy_command(); }},
        { IWM_CTRL_SET_DCB, [this]()                   { this->iwm_dummy_command(); }},                 // 0x01
        { IWM_CTRL_SET_NEWLINE, [this]()               { this->iwm_dummy_command(); }},                 // 0x02

        { FUJICMD_CLOSE_DIRECTORY, [this]()            { this->fujicmd_close_directory(); }},          // 0xF5
        { FUJICMD_GET_HOST_PREFIX, [this]()            { this->fujicmd_get_host_prefix(data_buffer[0]); }},                  // 0xE0
        { FUJICMD_CONFIG_BOOT, [this]()                { this->fujicmd_set_boot_config(data_buffer[0]); }},          // 0xD9
        { FUJICMD_COPY_FILE, [this]()                  { this->fujicmd_copy_file_success(data_buffer[0], data_buffer[1], (char *)&data_buffer[2]); }},                // 0xD8
        { FUJICMD_DISABLE_DEVICE, [this]()             { this->iwm_ctrl_disable_device(); }},           // 0xD4
        { FUJICMD_ENABLE_DEVICE, [this]()              { this->iwm_ctrl_enable_device(); }},            // 0xD5
        { FUJICMD_GET_SCAN_RESULT, [this]()            { this->fujicmd_net_scan_result(data_buffer[0]); }},          // 0xFC

        { FUJICMD_HASH_INPUT, [this]()                 { this->iwm_ctrl_hash_input(); }},               // 0xC8
        { FUJICMD_HASH_COMPUTE, [this]()               { this->iwm_ctrl_hash_compute(true); }},         // 0xC7
        { FUJICMD_HASH_COMPUTE_NO_CLEAR, [this]()      { this->iwm_ctrl_hash_compute(false); }},        // 0xC7
        { FUJICMD_HASH_LENGTH, [this]()                { this->iwm_stat_hash_length(); }},              // 0xC6
        { FUJICMD_HASH_OUTPUT, [this]()                { this->iwm_stat_hash_output(); }},              // 0xC5
        { FUJICMD_HASH_CLEAR, [this]()                 { this->iwm_ctrl_hash_clear(); }},               // 0xC2

        { FUJICMD_QRCODE_INPUT, [this]()               { this->iwm_ctrl_qrcode_input(); }},             // 0xBC
        { FUJICMD_QRCODE_ENCODE, [this]()              { this->iwm_ctrl_qrcode_encode(); }},            // 0xBD
        { FUJICMD_QRCODE_OUTPUT, [this]()              { this->iwm_ctrl_qrcode_output(); }},            // 0xBF

        { FUJICMD_MOUNT_HOST, [this]()                 { this->fujicmd_mount_host_success(data_buffer[0]); }},               // 0xF9
        { FUJICMD_NEW_DISK, [this]()                   { this->iwm_ctrl_new_disk(); }},                 // 0xE7
        { FUJICMD_OPEN_APPKEY, [this]()                { this->fujicmd_open_app_key(); }},             // 0xDC
        { FUJICMD_READ_DIR_ENTRY, [this]()             { this->fujicmd_read_directory_entry(data_buffer[0], data_buffer[1]); }},     // 0xF6
        { FUJICMD_SET_BOOT_MODE, [this]()              { this->fujicmd_set_boot_mode(data_buffer[0], MEDIATYPE_PO, get_disk_dev(0)); }},            // 0xD6
        { FUJICMD_SET_DEVICE_FULLPATH, [this]()        { this->fujicmd_set_device_filename_success(data_buffer[0], data_buffer[1], (disk_access_flags_t) data_buffer[2]); }},      // 0xE2
        { FUJICMD_SET_DIRECTORY_POSITION, [this]()     { this->fujicmd_set_directory_position(le16toh(*((uint16_t *) &data_buffer))); }},   // 0xE4
        { FUJICMD_SET_HOST_PREFIX, [this]()            { this->fujicmd_set_host_prefix(data_buffer[0], (const char *) &data_buffer[1]); }},          // 0xE1
        { FUJICMD_SET_SSID, [this]()                   { this->fujicmd_net_set_ssid_success((const char *) data_buffer, (const char *) &data_buffer[MAX_SSID_LEN + 1], false); }},             // 0xFB
        { FUJICMD_UNMOUNT_HOST, [this]()               { this->fujicmd_unmount_host_success(data_buffer[0]); }},             // 0xE6
        { FUJICMD_UNMOUNT_IMAGE, [this]()              { this->fujicmd_unmount_disk_image_success(data_buffer[0]); }},        // 0xE9
        { FUJICMD_WRITE_APPKEY, [this]()               { this->fujicmd_write_app_key(data_len); }},            // 0xDE
        { FUJICMD_WRITE_DEVICE_SLOTS, [this]()         { this->fujicmd_write_device_slots(); }},       // 0xF1
        { FUJICMD_WRITE_HOST_SLOTS, [this]()           { this->fujicmd_write_host_slots(); }},         // 0xF3

        { FUJICMD_RESET,  [this]()                     {
             this->send_reply_packet(err_result);
             this->fujicmd_reset();
         }},   // 0xFF
        { IWM_CTRL_RESET, [this]()                     {
             this->send_reply_packet(err_result);
             this->fujicmd_reset();
         }},   // 0x00
#ifndef DEV_RELAY_SLIP
        { IWM_CTRL_CLEAR_ENSEEN, [this]()              { diskii_xface.d2_enable_seen = 0; err_result = SP_ERR_NOERROR; }},
#endif

        { FUJICMD_MOUNT_ALL, [&]()                     {
             err_result = fujicmd_mount_all_success() ? SP_ERR_NOERROR : SP_ERR_IOERROR;
         }},          // 0xD7
        { FUJICMD_MOUNT_IMAGE, [&]()                   { err_result = fujicmd_mount_disk_image_success(data_buffer[0], (disk_access_flags_t) data_buffer[1]) ? SP_ERR_NOERROR : SP_ERR_NODRIVE; }},  // 0xF8
        { FUJICMD_OPEN_DIRECTORY, [&]()                { err_result = fujicore_open_directory_success(data_buffer[0], std::string((char *) &data_buffer[1], sizeof(data_buffer) - 1)) ? SP_ERR_NOERROR : SP_ERR_IOERROR; }}     // 0xF7
    };

    status_handlers = {
        { 0xAA, [this]()                               { this->iwm_hello_world(); }},

        { IWM_STATUS_DIB, [this]()                     { this->send_status_dib_reply_packet(); status_completed = true; }},     // 0x03
        { IWM_STATUS_STATUS, [this]()                  { this->send_status_reply_packet(); status_completed = true; }},         // 0x00
#ifndef DEV_RELAY_SLIP
        { IWM_STATUS_ENSEEN, [this]()                  { data_len = 1; data_buffer[0] = diskii_xface.d2_enable_seen; }},
#endif

        { FUJICMD_DEVICE_ENABLE_STATUS, [this]()       { this->send_stat_get_enable(); }},                      // 0xD1
        { FUJICMD_GET_ADAPTERCONFIG_EXTENDED, [this]() { this->fujicmd_get_adapter_config_extended(); }},      // 0xC4
        { FUJICMD_GET_ADAPTERCONFIG, [this]()          { this->fujicmd_get_adapter_config(); }},               // 0xE8
        { FUJICMD_GET_DEVICE_FULLPATH, [this]()        { this->fujicmd_get_device_filename(data_buffer[0]); }},   // 0xDA
        { FUJICMD_GET_DEVICE1_FULLPATH, [this]()       { this->fujicmd_get_device_filename(status_code - 160); }},   // 0xA0
        { FUJICMD_GET_DEVICE2_FULLPATH, [this]()       { this->fujicmd_get_device_filename(status_code - 160); }},   // 0xA1
        { FUJICMD_GET_DEVICE3_FULLPATH, [this]()       { this->fujicmd_get_device_filename(status_code - 160); }},   // 0xA2
        { FUJICMD_GET_DEVICE4_FULLPATH, [this]()       { this->fujicmd_get_device_filename(status_code - 160); }},   // 0xA3
        { FUJICMD_GET_DEVICE5_FULLPATH, [this]()       { this->fujicmd_get_device_filename(status_code - 160); }},   // 0xA4
        { FUJICMD_GET_DEVICE6_FULLPATH, [this]()       { this->fujicmd_get_device_filename(status_code - 160); }},   // 0xA5
        { FUJICMD_GET_DEVICE7_FULLPATH, [this]()       { this->fujicmd_get_device_filename(status_code - 160); }},   // 0xA6
        { FUJICMD_GET_DEVICE8_FULLPATH, [this]()       { this->fujicmd_get_device_filename(status_code - 160); }},   // 0xA7
        { FUJICMD_GET_DIRECTORY_POSITION, [this]()     { this->fujicmd_get_directory_position(); }},           // 0xE5
        { FUJICMD_GET_HOST_PREFIX, [this]()            { }},                  // 0xE0
        { FUJICMD_GET_SCAN_RESULT, [this]()            { this->iwm_stat_net_scan_result(); }},                  // 0xFC
        { FUJICMD_GET_SSID, [this]()                   { this->fujicmd_net_get_ssid(); }},                     // 0xFE
        { FUJICMD_GET_WIFI_ENABLED, [this]()           { this->iwm_stat_get_wifi_enabled(); }},                 // 0xEA
        { FUJICMD_GET_WIFISTATUS, [this]()             { this->fujicmd_net_get_wifi_status(); }},              // 0xFA
        { FUJICMD_READ_APPKEY, [this]()                { this->fujicmd_read_app_key(); }},                     // 0xDD
        { FUJICMD_READ_DEVICE_SLOTS, [this]()          { this->fujicmd_read_device_slots(); }},                // 0xF2
        { FUJICMD_READ_DIR_ENTRY, [this]()             { }},             // 0xF6
        { FUJICMD_READ_HOST_SLOTS, [this]()            { this->fujicmd_read_host_slots(); }},                  // 0xF4
        { FUJICMD_SCAN_NETWORKS, [this]()              { this->fujicmd_net_scan_networks(); }},                // 0xFD
        { FUJICMD_QRCODE_LENGTH, [this]()              { this->iwm_stat_qrcode_length(); }},                    // 0xBE
        { FUJICMD_QRCODE_OUTPUT, [this]()              { this->iwm_stat_qrcode_output(); }},                    // 0xBE
        { FUJICMD_STATUS, [this]()                     { this->fujicmd_status(); }},                      // 0x53
        { FUJICMD_GET_HEAP, [this]()                   { this->iwm_stat_get_heap(); }},                         // 0xC1
    };

}

//// UNHANDLED CONTROL FUNCTIONS
// case FUJICMD_CLOSE_APPKEY:           // 0xDB
// case FUJICMD_GET_ADAPTERCONFIG:      // 0xE8
// case FUJICMD_GET_DEVICE_FULLPATH:    // 0xDA
// case FUJICMD_GET_DIRECTORY_POSITION: // 0xE5
// case FUJICMD_GET_HOST_PREFIX:        // 0xE0
// case FUJICMD_GET_SSID:               // 0xFE
// case FUJICMD_GET_WIFISTATUS:         // 0xFA
// case FUJICMD_READ_APPKEY:                    // 0xDD
// case FUJICMD_READ_DEVICE_SLOTS:      // 0xF2
// case FUJICMD_READ_HOST_SLOTS:        // 0xF4
// case FUJICMD_SCAN_NETWORKS:          // 0xFD
// case FUJICMD_STATUS:                 // 0x53

//// Unhandled Status Commands
// case FUJICMD_CLOSE_APPKEY:           // 0xDB
// case FUJICMD_CLOSE_DIRECTORY:        // 0xF5
// case FUJICMD_CONFIG_BOOT:            // 0xD9
// case FUJICMD_COPY_FILE:              // 0xD8
// case FUJICMD_DISABLE_DEVICE:         // 0xD4
// case FUJICMD_ENABLE_DEVICE:          // 0xD5
// case FUJICMD_MOUNT_ALL:              // 0xD7
// case FUJICMD_MOUNT_HOST:             // 0xF9
// case FUJICMD_MOUNT_IMAGE:            // 0xF8
// case FUJICMD_NEW_DISK:               // 0xE7
// case FUJICMD_OPEN_APPKEY:            // 0xDC
// case FUJICMD_OPEN_DIRECTORY:         // 0xF7
// case FUJICMD_RESET:                  // 0xFF
// case FUJICMD_SET_BOOT_MODE:          // 0xD6
// case FUJICMD_SET_DEVICE_FULLPATH:    // 0xE2
// case FUJICMD_SET_DIRECTORY_POSITION: // 0xE4
// case FUJICMD_SET_HOST_PREFIX:        // 0xE1
// case FUJICMD_SET_SSID:               // 0xFB
// case FUJICMD_UNMOUNT_HOST:           // 0xE6
// case FUJICMD_UNMOUNT_IMAGE:          // 0xE9
// case FUJICMD_WRITE_APPKEY:           // 0xDE
// case FUJICMD_WRITE_DEVICE_SLOTS:     // 0xF1
// case FUJICMD_WRITE_HOST_SLOTS:       // 0xF3
// case IWM_STATUS_DCB:                 // 0x01
// case IWM_STATUS_NEWLINE:             // 0x02

void iwmFuji::iwm_dummy_command() // SP CTRL command
{
        Debug_printf("\r\nData Received: ");
        for (int i = 0; i < data_len; i++)
                Debug_printf(" %02x", data_buffer[i]);
}

void iwmFuji::iwm_hello_world()
{
        Debug_printf("\r\nFuji cmd: HELLO WORLD");
        memcpy(data_buffer, "HELLO WORLD", 11);
        data_len = 11;
}

void iwmFuji::iwm_stat_net_scan_result() // SP STATUS command
{
        Debug_printf("SSID: %s - RSSI: %u\n", detail.ssid, detail.rssi);

        memset(data_buffer, 0, sizeof(data_buffer));
        memcpy(data_buffer, &detail, sizeof(detail));
        data_len = sizeof(detail);
} // 0xFC

//==============================================================================================================================

void iwmFuji::fujicmd_read_directory_entry(size_t maxlen, uint8_t addtl)
{
    fujiDevice::fujicmd_read_directory_entry(maxlen, addtl);

    // Hack-o-rama to add file type character to beginning of
    // path. - this was for Adam, but must keep for CONFIG
    // compatability; in Apple 2 config will somehow have to work
    // around these extra chars

    // NOTE: Atari *does not* need this hack! Maybe Apple II CONFIG
    // should be fixed instead?

    if (data_buffer[0] != 0x7F && data_buffer[1] != 0x7F && maxlen == DIR_MAX_LEN)
    {
        memmove(&data_buffer[2], data_buffer, maxlen - 2);
        data_buffer[0] = data_buffer[1] = 0x20;
    }
}

void iwmFuji::iwm_stat_get_heap()
{
#ifdef ESP_PLATFORM
        uint32_t avail = esp_get_free_internal_heap_size();
#else
        uint32_t avail = 0;
#endif

    memcpy(data_buffer, &avail, sizeof(avail));
    data_len = sizeof(avail);
    return;
}

//  Make new disk and shove into device slot
void iwmFuji::iwm_ctrl_new_disk()
{
        int idx = 0;
        uint8_t hs = data_buffer[idx++]; // adamnet_recv();
        uint8_t ds = data_buffer[idx++]; // adamnet_recv();
        uint8_t t = data_buffer[idx++];  // added for apple2;
        uint32_t numBlocks;
        uint8_t *c = (uint8_t *)&numBlocks;
        uint8_t p[256];

        // adamnet_recv_buffer(c, sizeof(uint32_t));
        memcpy((uint8_t *)c, (uint8_t *)&data_buffer[idx], sizeof(uint32_t));
        idx += sizeof(uint32_t);

        memcpy(p, (uint8_t *)&data_buffer[idx], sizeof(p));
        // adamnet_recv_buffer(p, 256);

        fujiDisk &disk = _fnDisks[ds];
        fujiHost &host = _fnHosts[hs];

        if (host.file_exists((const char *)p))
        {
                return;
        }

        disk.host_slot = hs;
        disk.access_mode = DISK_ACCESS_MODE_WRITE;
        strlcpy(disk.filename, (const char *)p, 256);

        disk.fileh = host.fnfile_open(disk.filename, disk.filename, sizeof(disk.filename), "wb");

        Debug_printf("Creating file %s on host slot %u mounting in disk slot %u numblocks: %lu\n", disk.filename, hs, ds, numBlocks);

        DISK_DEVICE *disk_dev = get_disk_dev(ds);
        disk_dev->write_blank(disk.fileh, numBlocks, t);

        fnio::fclose(disk.fileh);

        // Persist slots
        populate_config_from_slots();
        Config.mark_dirty();
        Config.save();
}

// Get the wifi enabled value
void iwmFuji::iwm_stat_get_wifi_enabled()
{
        uint8_t e = Config.get_wifi_enabled() ? 1 : 0;
        Debug_printf("\nFuji cmd: GET WIFI ENABLED: %d", e);
        data_buffer[0] = e;
        data_len = 1;
}

void iwmFuji::iwm_ctrl_enable_device()
{
        unsigned char d = data_buffer[0]; // adamnet_recv();

        Debug_printf("\nFuji cmd: ENABLE DEVICE");
        SYSTEM_BUS.enableDevice(d);
}

void iwmFuji::iwm_ctrl_disable_device()
{
        unsigned char d = data_buffer[0]; // adamnet_recv();

        Debug_printf("\nFuji cmd: DISABLE DEVICE");
        SYSTEM_BUS.disableDevice(d);
}

// Initializes base settings and adds our devices to the SIO bus
void iwmFuji::setup()
{
        populate_slots_from_config();

        // Disable booting from CONFIG if our settings say to turn it off
        boot_config = false; // to do - understand?

        // add ourselves as a device
        SYSTEM_BUS.addDevice(this, iwm_fujinet_type_t::FujiNet);

        theNetwork = new iwmNetwork();
        SYSTEM_BUS.addDevice(theNetwork, iwm_fujinet_type_t::Network);

        theClock = new iwmClock();
        SYSTEM_BUS.addDevice(theClock, iwm_fujinet_type_t::Clock);

        theCPM = new iwmCPM();
        SYSTEM_BUS.addDevice(theCPM, iwm_fujinet_type_t::CPM);

        for (int i = MAX_SPDISK_DEVICES - 1; i >= 0; i--)
        {
                DISK_DEVICE *disk_dev = get_disk_dev(i);
                disk_dev->set_disk_number('0' + i);
                SYSTEM_BUS.addDevice(disk_dev, iwm_fujinet_type_t::BlockDisk);
        }

        Debug_printf("\nConfig General Boot Mode: %u\n", Config.get_general_boot_mode());
        insert_boot_device(Config.get_general_boot_mode(), MEDIATYPE_PO, get_disk_dev(0));
}

void iwmFuji::send_status_reply_packet()
{

        uint8_t data[4];

        // Build the contents of the packet
        data[0] = STATCODE_READ_ALLOWED | STATCODE_DEVICE_ONLINE;
        data[1] = 0; // block size 1
        data[2] = 0; // block size 2
        data[3] = 0; // block size 3
        SYSTEM_BUS.iwm_send_packet(id(), iwm_packet_type_t::status, SP_ERR_NOERROR, data, 4);
}

void iwmFuji::send_status_dib_reply_packet()
{
        Debug_printf("\r\nTHE_FUJI: Sending DIB reply\r\n");
        std::vector<uint8_t> data = create_dib_reply_packet(
                "THE_FUJI",                                             // name
                STATCODE_READ_ALLOWED | STATCODE_DEVICE_ONLINE,         // status
                { 0, 0, 0 },                                            // block size
                { SP_TYPE_BYTE_FUJINET, SP_SUBTYPE_BYTE_FUJINET },      // type, subtype
                { 0x00, 0x01 }                                          // version.
        );
        SYSTEM_BUS.iwm_send_packet(id(), iwm_packet_type_t::status, SP_ERR_NOERROR, data.data(), data.size());
}

void iwmFuji::send_stat_get_enable()
{
        data_len = 1;
        data_buffer[0] = 1;
}

void iwmFuji::iwm_open(iwm_decoded_cmd_t cmd)
{
        send_status_reply_packet();
}

void iwmFuji::iwm_close(iwm_decoded_cmd_t cmd) {}
void iwmFuji::iwm_read(iwm_decoded_cmd_t cmd) {}

void iwmFuji::iwm_status(iwm_decoded_cmd_t cmd)
{
        status_code = get_status_code(cmd);
        status_completed = false;

        Debug_printf("\r\n[Fuji] Device %02x Status Code %02x\r\n", id(), status_code);

        auto it = status_handlers.find(status_code);
    if (it != status_handlers.end()) {
        it->second();
    } else {
                Debug_printf("ERROR: Unhandled status code: %02X\n", status_code);
    }

        if (status_completed) return;

        Debug_printf("\nStatus code complete, sending response");
        SYSTEM_BUS.iwm_send_packet(id(), iwm_packet_type_t::data, 0, data_buffer, data_len);
}

void iwmFuji::iwm_ctrl(iwm_decoded_cmd_t cmd)
{
        uint8_t control_code = get_status_code(cmd);
        Debug_printf("\ntheFuji Device %02x Control Code %02x", id(), control_code);

        err_result = SP_ERR_NOERROR;
        data_len = 512;

        Debug_printf("\nDecoding Control Data Packet for code: 0x%02x\r\n", control_code);
        SYSTEM_BUS.iwm_decode_data_packet((uint8_t *)data_buffer, data_len);
        print_packet((uint8_t *)data_buffer, data_len);

        auto it = control_handlers.find(control_code);
    if (it != control_handlers.end()) {
        it->second();
    } else {
                Debug_printf("ERROR: Unhandled control code: %02X\n", control_code);
        err_result = SP_ERR_BADCTL;
    }

        send_reply_packet(err_result);
}


void iwmFuji::process(iwm_decoded_cmd_t cmd)
{
        fnLedManager.set(LED_BUS, true);

    auto it = command_handlers.find(cmd.command);
        // Debug_printf("\r\n----- iwmFuji::process handling command: %02X\r\n", cmd.command);
    if (it != command_handlers.end()) {
        it->second(cmd);
    } else {
        Debug_printv("\r\nUnknown command: %02x\r\n", cmd.command);
                iwm_return_badcmd(cmd);
    }

        fnLedManager.set(LED_BUS, false);
}

void iwmFuji::handle_ctl_eject(uint8_t spid)
{
        int ds = 255;
        for (int i = 0; i < _totalDiskDevices; i++)
        {
                if (theFuji->get_disk_dev(i)->id() == spid)
                {
                        ds = i;
                }
        }
        if (ds != 255)
        {
                theFuji->get_disk(ds)->reset();
                Config.clear_mount(ds);
                Config.save();
                theFuji->populate_slots_from_config();
        }
}

void iwmFuji::iwm_ctrl_hash_input()
{
    std::vector<uint8_t> data(data_len, 0);
    std::copy(&data_buffer[0], &data_buffer[0] + data_len, data.begin());
    hasher.add_data(data);
}

void iwmFuji::iwm_ctrl_hash_compute(bool clear_data)
{
    Debug_printf("FUJI: HASH COMPUTE\n");
    algorithm = Hash::to_algorithm(data_buffer[0]);
    hasher.compute(algorithm, clear_data);
}

void iwmFuji::iwm_stat_hash_length()
{
    uint8_t is_hex = data_buffer[0] == 1;
    uint8_t r = hasher.hash_length(algorithm, is_hex);

        memset(data_buffer, 0, sizeof(data_buffer));
        data_buffer[0] = r;
        data_len = 1;
}

void iwmFuji::iwm_ctrl_hash_output()
{
    Debug_printf("FUJI: HASH OUTPUT CONTROL\n");
    hash_is_hex_output = data_buffer[0] == 1;
}

void iwmFuji::iwm_stat_hash_output()
{
    Debug_printf("FUJI: HASH OUTPUT STAT\n");
        memset(data_buffer, 0, sizeof(data_buffer));

        if (hash_is_hex_output) {
                std::string hex_output = hasher.output_hex();
                std::memcpy(data_buffer, hex_output.c_str(), hex_output.size());
                data_len = static_cast<int>(hex_output.size());
        } else {
                std::vector<uint8_t> binary_output = hasher.output_binary();
                std::memcpy(data_buffer, binary_output.data(), binary_output.size());
                data_len = static_cast<int>(binary_output.size());
        }
}

void iwmFuji::iwm_ctrl_hash_clear()
{
    hasher.clear();
}

void iwmFuji::iwm_ctrl_qrcode_input()
{
    Debug_printf("FUJI: QRCODE INPUT (len: %d)\n", data_len);
    std::vector<uint8_t> data(data_len, 0);
    std::copy(&data_buffer[0], &data_buffer[0] + data_len, data.begin());
    _qrManager.data += std::string((const char *)data.data(), data_len);
}

void iwmFuji::iwm_ctrl_qrcode_encode()
{
    uint8_t version = data_buffer[0] & 0b01111111;
    uint8_t ecc_mode = data_buffer[1];
    bool shorten = data_buffer[2];

    Debug_printf("FUJI: QRCODE ENCODE\n");
    Debug_printf("QR Version: %d, ECC: %d, Shorten: %s\n", version, ecc_mode, shorten ? "Y" : "N");

    std::string url = _qrManager.data;

    if (shorten) {
        url = fnHTTPD.shorten_url(url);
    }

        _qrManager.version(version);
        _qrManager.ecc((qr_ecc_t)ecc_mode);
        _qrManager.output_mode = QR_OUTPUT_MODE_BINARY;
        _qrManager.encode();

    _qrManager.data.clear();

    if (!_qrManager.code.size())
    {
        Debug_printf("QR code encoding failed\n");
        return;
    }

    Debug_printf("Resulting QR code is: %u modules\n", _qrManager.code.size());
}

void iwmFuji::iwm_stat_qrcode_length()
{
    Debug_printf("FUJI: QRCODE LENGTH\n");
    size_t len = _qrManager.code.size();
        data_buffer[0] = (uint8_t)(len >> 0);
    data_buffer[1] = (uint8_t)(len >> 8);
        data_len = 2;
}

void iwmFuji::iwm_ctrl_qrcode_output()
{
    Debug_printf("FUJI: QRCODE OUTPUT CONTROL\n");

    uint8_t output_mode = data_buffer[0];
    Debug_printf("Output mode: %i\n", output_mode);

    size_t len = _qrManager.code.size();

    if (len && (output_mode != _qrManager.output_mode)) {
                _qrManager.output_mode = (ouput_mode_t)output_mode;
                _qrManager.encode();
    }
}

void iwmFuji::iwm_stat_qrcode_output()
{
    Debug_printf("FUJI: QRCODE OUTPUT STAT\n");
        memset(data_buffer, 0, sizeof(data_buffer));

        data_len = _qrManager.code.size();
        memcpy(data_buffer, &_qrManager.code[0], data_len);

        _qrManager.code.clear();
    _qrManager.code.shrink_to_fit();
}

void iwmFuji::fujicmd_reset()
{
    send_status_reply_packet();
    fujiDevice::fujicmd_reset();
}

void iwmFuji::fujicmd_close_directory()
{
    fujiDevice::fujicmd_close_directory();
    fnSystem.delay(100); // add delay because bad traces
}

size_t iwmFuji::set_additional_direntry_details(fsdir_entry_t *f, uint8_t *dest,
                                                uint8_t maxlen)
{
    return _set_additional_direntry_details(f, dest, maxlen, 100, SIZE_32_LE,
                                           HAS_DIR_ENTRY_FLAGS_SEPARATE, HAS_DIR_ENTRY_TYPE);
}

#endif /* BUILD_APPLE */
