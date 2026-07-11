#ifdef BUILD_APPLE

#include "iwmFuji.h"
#include "httpService.h"
#include "fnSystem.h"
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

    control_handlers = {
        { 0xAA, [this]()                               { this->iwm_dummy_command(); }},
        { SP_CTRL_SET_DCB, [this]()                   { this->iwm_dummy_command(); }},                 // 0x01
        { SP_CTRL_SET_NEWLINE, [this]()               { this->iwm_dummy_command(); }},                 // 0x02

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
        { FUJICMD_SET_SSID, [this]()                   {
            // FIXME - make sure data_len > MAX_SSID_LEN + 1
            std::string buffer(data_len, 0);
            transaction_begin(TRANS_STATE::WILL_GET);
            transaction_get(buffer.data(), buffer.size());
            this->fujicmd_net_set_ssid_success(buffer.c_str(),
                                               buffer.c_str() + MAX_SSID_LEN + 1, true);
        }},             // 0xFB
        { FUJICMD_UNMOUNT_HOST, [this]()               { this->fujicmd_unmount_host_success(data_buffer[0]); }},             // 0xE6
        { FUJICMD_UNMOUNT_IMAGE, [this]()              { this->fujicmd_unmount_disk_image_success(data_buffer[0]); }},        // 0xE9
        { FUJICMD_WRITE_APPKEY, [this]()               { this->fujicmd_write_app_key(data_len); }},            // 0xDE
        { FUJICMD_WRITE_DEVICE_SLOTS, [this]()         { this->fujicmd_write_device_slots(); }},       // 0xF1
        { FUJICMD_WRITE_HOST_SLOTS, [this]()           { this->fujicmd_write_host_slots(); }},         // 0xF3

        { FUJICMD_RESET,  [this]()                     {
             this->send_reply_packet(err_result);
             this->fujicmd_reset();
         }},   // 0xFF
        { SP_CTRL_RESET, [this]()                     {
             this->send_reply_packet(err_result);
             this->fujicmd_reset();
         }},   // 0x00
#ifdef DEV_RELAY_SLIP
        { SP_CTRL_CLEAR_DISKII_SEEN, [this]()              { err_result = SP_ERR::NODRIVE; }},
#else
        { SP_CTRL_CLEAR_DISKII_SEEN, [this]()              { diskii_xface.d2_enable_seen = 0; err_result = SP_ERR::NOERROR; }},
#endif

        { FUJICMD_MOUNT_ALL, [&]()                     {
             err_result = fujicmd_mount_all_success() ? SP_ERR::NOERROR : SP_ERR::IOERROR;
         }},          // 0xD7
        { FUJICMD_MOUNT_IMAGE, [&]()                   { err_result = fujicmd_mount_disk_image_success(data_buffer[0], (disk_access_flags_t) data_buffer[1]) ? SP_ERR::NOERROR : SP_ERR::NODRIVE; }},  // 0xF8
        { FUJICMD_OPEN_DIRECTORY, [&]()                { err_result = fujicore_open_directory_success(data_buffer[0], std::string((char *) &data_buffer[1], data_len - 1)) ? SP_ERR::NOERROR : SP_ERR::IOERROR; }}     // 0xF7
    };

    status_handlers = {
        { 0xAA, [this]()                               { this->iwm_hello_world(); }},

#ifndef DEV_RELAY_SLIP
        { SP_STAT_GET_DISKII_SEEN, [this]()                  { data_len = 1; data_buffer[0] = diskii_xface.d2_enable_seen; }},
#endif

        { FUJICMD_DEVICE_ENABLE_STATUS, [this]()       { this->send_stat_get_enable(); }},                      // 0xD1
        { FUJICMD_GET_ADAPTERCONFIG_EXTENDED, [this]() { this->fujicmd_get_adapter_config_extended(); }},      // 0xC4
        { FUJICMD_GET_ADAPTERCONFIG, [this]()          { this->fujicmd_get_adapter_config(); }},               // 0xE8
        { FUJICMD_GET_DEVICE_FULLPATH, [this]()        { this->fujicmd_get_device_filename(data_buffer[0]); }},   // 0xDA
        { FUJICMD_GET_DEVICE1_FULLPATH, [this]()       { this->fujicmd_get_device_filename(active_fuji_command - 160); }},   // 0xA0
        { FUJICMD_GET_DEVICE2_FULLPATH, [this]()       { this->fujicmd_get_device_filename(active_fuji_command - 160); }},   // 0xA1
        { FUJICMD_GET_DEVICE3_FULLPATH, [this]()       { this->fujicmd_get_device_filename(active_fuji_command - 160); }},   // 0xA2
        { FUJICMD_GET_DEVICE4_FULLPATH, [this]()       { this->fujicmd_get_device_filename(active_fuji_command - 160); }},   // 0xA3
        { FUJICMD_GET_DEVICE5_FULLPATH, [this]()       { this->fujicmd_get_device_filename(active_fuji_command - 160); }},   // 0xA4
        { FUJICMD_GET_DEVICE6_FULLPATH, [this]()       { this->fujicmd_get_device_filename(active_fuji_command - 160); }},   // 0xA5
        { FUJICMD_GET_DEVICE7_FULLPATH, [this]()       { this->fujicmd_get_device_filename(active_fuji_command - 160); }},   // 0xA6
        { FUJICMD_GET_DEVICE8_FULLPATH, [this]()       { this->fujicmd_get_device_filename(active_fuji_command - 160); }},   // 0xA7
        { FUJICMD_GET_DEVICE9_FULLPATH, [this]()       { this->fujicmd_get_device_filename(active_fuji_command - 160); }},   // 0xA8
        { FUJICMD_GET_DEVICE10_FULLPATH, [this]()      { this->fujicmd_get_device_filename(active_fuji_command - 160); }},   // 0xA9
        { FUJICMD_GET_DIRECTORY_POSITION, [this]()     { this->fujicmd_get_directory_position(); }},           // 0xE5
        { FUJICMD_GET_HOST_PREFIX, [this]()            { }},                  // 0xE0
        { FUJICMD_GET_SCAN_RESULT, [this]()            { }},                  // 0xFC
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
        { FUJICMD_GENERATE_GUID, [this]()              { this->fujicmd_generate_guid(); }},                     // 0xBB
    };

}

void iwmFuji::iwm_dummy_command() // SP CTRL command
{
        Debug_printf("\r\nData Received: ");
        transaction_begin(TRANS_STATE::WILL_GET);
        std::string buffer(data_len, 0);
        transaction_get(buffer.data(), buffer.size());
        for (uint8_t byte : buffer)
            Debug_printf(" %02x", byte);
}

void iwmFuji::iwm_hello_world()
{
        Debug_printf("\r\nFuji cmd: HELLO WORLD");
        transaction_begin(TRANS_STATE::NO_GET);
        transaction_put("HELLO WORLD", 11);
}

//==============================================================================================================================

void iwmFuji::fujicmd_read_directory_entry(size_t maxlen, uint8_t addtl)
{
    auto result = fujiDevice::fujicore_read_directory_entry(maxlen, addtl);

    // Hack-o-rama to add file type character to beginning of
    // path. - this was for Adam, but must keep for CONFIG
    // compatability; in Apple 2 config will somehow have to work
    // around these extra chars

    // NOTE: Atari *does not* need this hack! Maybe Apple II CONFIG
    // should be fixed instead?

    if (result.has_value() && result->size() >= 2
        && (*result)[0] != 0x7F && (*result)[1] != 0x7F && maxlen == DIR_MAX_LEN)
        result->insert(0, "  ");

    transaction_begin(TRANS_STATE::NO_GET);
    transaction_put(result->data(), result->size());
}

void iwmFuji::iwm_stat_get_heap()
{
    u32le_t avail;
#ifdef ESP_PLATFORM
    avail = esp_get_free_internal_heap_size();
#else
    avail = 0;
#endif

    transaction_begin(TRANS_STATE::NO_GET);
    transaction_put(&avail, sizeof(avail));
    return;
}

//  Make new disk and shove into device slot
void iwmFuji::iwm_ctrl_new_disk()
{
    uint8_t hs = data_buffer[0];
    uint8_t ds = data_buffer[1];
    uint8_t t = data_buffer[2];
    u32le_t numBlocks;
    const uint8_t *ptr;

    memcpy(&numBlocks, &data_buffer[3], sizeof(numBlocks));
    ptr = &data_buffer[3] + sizeof(numBlocks);

    fujiDisk &disk = _fnDisks[ds];
    fujiHost &host = _fnHosts[hs];

    if (host.file_exists((const char *) ptr))
        return;

    disk.host_slot = hs;
    disk.access_mode = DISK_ACCESS_MODE_WRITE;
    strlcpy(disk.filename, (const char *) ptr, sizeof(disk.filename));

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
        transaction_begin(TRANS_STATE::NO_GET);
        transaction_put(e);
}

void iwmFuji::iwm_ctrl_enable_device()
{
        unsigned char d = data_buffer[0];

        Debug_printf("\nFuji cmd: ENABLE DEVICE");
        SYSTEM_BUS.enableDevice(d);
}

void iwmFuji::iwm_ctrl_disable_device()
{
        unsigned char d = data_buffer[0];

        Debug_printf("\nFuji cmd: DISABLE DEVICE");
        SYSTEM_BUS.disableDevice(d);
}

// Initializes base settings and adds our devices to the SIO bus
void iwmFuji::setup()
{
        populate_slots_from_config();

        // Disable booting from CONFIG if our settings say to turn it off
        boot_config = Config.get_general_config_enabled();

        // Build the device topology once, to avoid duplicating the daisy chain
        // and leaking the devices when setup() re-runs on an in-process restart.
        if (theNetwork == nullptr)
        {
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
        }

        if (boot_config)
        {
            Debug_printf("\nConfig General Boot Mode: %u\n", Config.get_general_boot_mode());
            insert_boot_device(Config.get_general_boot_mode(), MEDIATYPE_PO, get_disk_dev(0));
        }
        else if (!Config.get_config_filename().empty())
        {
            Debug_printf("\nInsert Alternate Config Disk: %s\n", Config.get_config_filename().c_str());
            insert_boot_device(Config.get_config_filename(), MEDIATYPE_PO, get_disk_dev(0));
        }
}

iwm_device_status_block_t iwmFuji::create_status_reply_packet()
{
  iwm_device_status_block_t status;

  status.code = STATCODE_READ_ALLOWED | STATCODE_DEVICE_ONLINE;
  status.block_size = 0;
  return status;
}

iwm_device_info_block_t iwmFuji::create_dib_reply_packet()
{
  iwm_device_info_block_t dib;

  dib.dev_status = create_status_reply_packet();
  strcpy(dib.name, "THE_FUJI");
  dib.name_len = strlen(dib.name);
  dib.type = SP_TYPE_BYTE_FUJINET;
  dib.subtype = SP_SUBTYPE_BYTE_FUJINET;
  dib.version = 0x0100;

  return dib;
}

void iwmFuji::send_stat_get_enable()
{
    transaction_begin(TRANS_STATE::NO_GET);
    transaction_put(1);
}

void iwmFuji::iwm_open(iwm_decoded_cmd_t cmd)
{
        send_status_reply_packet();
}

void iwmFuji::iwm_close(iwm_decoded_cmd_t cmd) {}
void iwmFuji::iwm_read(iwm_decoded_cmd_t cmd) {}

void iwmFuji::iwm_status(iwm_decoded_cmd_t cmd)
{
    active_fuji_command = cmd.control_status.fuji.command;

    Debug_printf("\r\n[Fuji] Device %02x Status Code %02x\r\n", id(), active_fuji_command);

    auto it = status_handlers.find(active_fuji_command);
    if (it != status_handlers.end()) {
        it->second();
    } else {
        Debug_printf("ERROR: Unhandled status code: %02X\n", active_fuji_command);
        data_len = 0;
    }

    Debug_printf("\nStatus code complete, sending response");
    SYSTEM_BUS.iwm_send_packet(id(), iwm_packet_type_t::data, SP_ERR::NOERROR, data_buffer, data_len);
}

void iwmFuji::iwm_ctrl(iwm_decoded_cmd_t cmd)
{
    Debug_printf("\ntheFuji Device %02x Control Code %02x", id(), cmd.control_status.fuji.command);

    err_result = SP_ERR::NOERROR;

    Debug_printf("\nDecoding Control Data Packet for code: 0x%02x\r\n", cmd.control_status.fuji.command);

    auto it = control_handlers.find(cmd.control_status.fuji.command);
    if (it != control_handlers.end()) {
        it->second();
    } else {
        Debug_printf("ERROR: Unhandled control code: %02X\n", cmd.control_status.fuji.command);
        err_result = SP_ERR::BADCTL;
    }

    send_reply_packet(err_result);
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
    ByteBuffer data(data_len, 0);
    transaction_begin(TRANS_STATE::WILL_GET);
    transaction_get(data.data(), data.size());
    hasher.add_data(data);
    transaction_complete();
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

    transaction_begin(TRANS_STATE::NO_GET);
    transaction_put(r);
}

void iwmFuji::iwm_ctrl_hash_output()
{
    Debug_printf("FUJI: HASH OUTPUT CONTROL\n");
    hash_is_hex_output = data_buffer[0] == 1;
}

void iwmFuji::iwm_stat_hash_output()
{
    Debug_printf("FUJI: HASH OUTPUT STAT\n");

    transaction_begin(TRANS_STATE::NO_GET);
    if (hash_is_hex_output)
        transaction_put(hasher.output_hex());
    else
        transaction_put(hasher.output_binary());
}

void iwmFuji::iwm_ctrl_hash_clear()
{
    hasher.clear();
}

void iwmFuji::iwm_ctrl_qrcode_input()
{
    Debug_printf("FUJI: QRCODE INPUT (len: %d)\n", data_len);
    std::string data(data_len, 0);
    transaction_begin(TRANS_STATE::WILL_GET);
    transaction_get(data.data(), data.size());
    _qrManager.data += data;
    transaction_complete();
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
    u16le_t len;
    Debug_printf("FUJI: QRCODE LENGTH\n");
    len = _qrManager.code.size();
    transaction_begin(TRANS_STATE::NO_GET);
    transaction_put(&len, sizeof(len));
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
    transaction_begin(TRANS_STATE::NO_GET);
    transaction_put(_qrManager.code);
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
    struct {
        dirEntryTimestamp modified;
        uint32_t size;
        uint8_t is_dir;
        uint8_t is_trunc;
        uint8_t mediatype;
    } __attribute__((packed)) custom_details;
    dirEntryDetails details;

    details = _additional_direntry_details(f);
    custom_details.modified = details.modified;
    custom_details.modified.year -= 100;
    custom_details.size = htole32(details.size);
    custom_details.is_dir = details.flags & DET_FF_DIR;
    custom_details.mediatype = details.mediatype;

    maxlen -= sizeof(custom_details);
    // Subtract a byte for a terminating slash on directories
    if (custom_details.is_dir)
        maxlen--;

    custom_details.is_trunc = strlen(f->filename) >= maxlen ? DET_FF_TRUNC : 0;
    memcpy(dest, &custom_details, sizeof(custom_details));
    return sizeof(custom_details);
}

success_is_true iwmFuji::fujicmd_set_device_filename_success(uint8_t deviceSlot, uint8_t host,
                                                              disk_access_flags_t mode)
{
    char tmp[MAX_FILENAME_LEN];

    transaction_begin(TRANS_STATE::WILL_GET);
    if (!transaction_get(tmp, sizeof(tmp)))
    {
        transaction_error();
        RETURN_ERROR_AS_FALSE();
    }

    // For some reason Apple2 CONFIG sends 3 bytes of garbage before the filename
    memmove(&tmp, &tmp[3], sizeof(tmp) - 3);
    Debug_printf("Fuji cmd: SET DEVICE SLOT 0x%02X/%02X/%02X FILENAME: %s\n",
                 deviceSlot, host, mode, tmp);

    if (!fujicore_set_device_filename_success(deviceSlot, host, mode,
                                              std::string(tmp, strnlen(tmp, sizeof(tmp)))))
    {
        transaction_error();
        RETURN_ERROR_AS_FALSE();
    }

    transaction_complete();
    RETURN_SUCCESS_AS_TRUE();
}

#endif /* BUILD_APPLE */
