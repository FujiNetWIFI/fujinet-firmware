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
        { 0xAA, [this](const iwm_decoded_cmd_t &cmd)                               { this->iwm_dummy_command(cmd); }},
        { SP_CTRL_SET_DCB, [this](const iwm_decoded_cmd_t &cmd)                   { this->iwm_dummy_command(cmd); }},                 // 0x01
        { SP_CTRL_SET_NEWLINE, [this](const iwm_decoded_cmd_t &cmd)               { this->iwm_dummy_command(cmd); }},                 // 0x02

        { FUJICMD_CLOSE_DIRECTORY, [this](const iwm_decoded_cmd_t &cmd)            { this->fujicmd_close_directory(); }},          // 0xF5
        { FUJICMD_GET_HOST_PREFIX, [this](const iwm_decoded_cmd_t &cmd)            { this->fujicmd_get_host_prefix(cmd.param(0)); }},                  // 0xE0
        { FUJICMD_CONFIG_BOOT, [this](const iwm_decoded_cmd_t &cmd)                { this->fujicmd_set_boot_config(cmd.param(0)); }},          // 0xD9
        { FUJICMD_COPY_FILE, [this](const iwm_decoded_cmd_t &cmd)                  {
            uint8_t source = cmd.param(0), dest = cmd.param(1);
            this->fujicmd_copy_file_success(source, dest, cmd.dataAsString()->data());
        }},                // 0xD8
        { FUJICMD_DISABLE_DEVICE, [this](const iwm_decoded_cmd_t &cmd)             { this->iwm_ctrl_disable_device(cmd); }},           // 0xD4
        { FUJICMD_ENABLE_DEVICE, [this](const iwm_decoded_cmd_t &cmd)              { this->iwm_ctrl_enable_device(cmd); }},            // 0xD5
        { FUJICMD_GET_SCAN_RESULT, [this](const iwm_decoded_cmd_t &cmd)            { this->fujicmd_net_scan_result(cmd.param(0)); }},          // 0xFC

        { FUJICMD_QRCODE_INPUT, [this](const iwm_decoded_cmd_t &cmd)               { this->iwm_ctrl_qrcode_input(cmd); }},             // 0xBC
        { FUJICMD_QRCODE_ENCODE, [this](const iwm_decoded_cmd_t &cmd)              { this->iwm_ctrl_qrcode_encode(cmd); }},            // 0xBD
        { FUJICMD_QRCODE_OUTPUT, [this](const iwm_decoded_cmd_t &cmd)              { this->iwm_ctrl_qrcode_output(cmd); }},            // 0xBF

        { FUJICMD_MOUNT_HOST, [this](const iwm_decoded_cmd_t &cmd)                 { this->fujicmd_mount_host_success(cmd.param8(0)); }},               // 0xF9
        { FUJICMD_NEW_DISK, [this](const iwm_decoded_cmd_t &cmd)                   { this->iwm_ctrl_new_disk(cmd); }},                 // 0xE7
        { FUJICMD_OPEN_APPKEY, [this](const iwm_decoded_cmd_t &cmd)                { this->fujicmd_open_app_key(); }},             // 0xDC
        { FUJICMD_READ_DIR_ENTRY, [this](const iwm_decoded_cmd_t &cmd)             { this->fujicmd_read_directory_entry(cmd.param8(0), cmd.param(1)); }},     // 0xF6
        { FUJICMD_SET_BOOT_MODE, [this](const iwm_decoded_cmd_t &cmd)              { this->fujicmd_set_boot_mode(cmd.param(0), MEDIATYPE_PO, get_disk_dev(0)); }},            // 0xD6
        { FUJICMD_SET_DEVICE_FULLPATH, [this](const iwm_decoded_cmd_t &cmd)        { this->fujicmd_set_device_filename_success(cmd.param(0), cmd.param(1), (disk_access_flags_t) cmd.param8(2)); }},      // 0xE2
        { FUJICMD_SET_DIRECTORY_POSITION, [this](const iwm_decoded_cmd_t &cmd)     { this->fujicmd_set_directory_position(cmd.param(0)); }},   // 0xE4
        { FUJICMD_SET_HOST_PREFIX, [this](const iwm_decoded_cmd_t &cmd)            {
            uint8_t slot = cmd.param(0);
            this->fujicmd_set_host_prefix(slot, cmd.dataAsString()->data());
        }},          // 0xE1
        { FUJICMD_SET_SSID, [this](const iwm_decoded_cmd_t &cmd)                   {
            this->fujicmd_net_set_ssid_success(cmd.dataAsString()->c_str(),
                                               cmd.dataAsString()->c_str() + MAX_SSID_LEN + 1,
                                               true);
        }},             // 0xFB
        { FUJICMD_UNMOUNT_HOST, [this](const iwm_decoded_cmd_t &cmd)               { this->fujicmd_unmount_host_success(cmd.param(0)); }},             // 0xE6
        { FUJICMD_UNMOUNT_IMAGE, [this](const iwm_decoded_cmd_t &cmd)              { this->fujicmd_unmount_disk_image_success(cmd.param(0)); }},        // 0xE9
        { FUJICMD_WRITE_APPKEY, [this](const iwm_decoded_cmd_t &cmd)               { this->fujicmd_write_app_key(cmd.data()->size()); }},            // 0xDE
        { FUJICMD_WRITE_DEVICE_SLOTS, [this](const iwm_decoded_cmd_t &cmd)         { this->fujicmd_write_device_slots(); }},       // 0xF1
        { FUJICMD_WRITE_HOST_SLOTS, [this](const iwm_decoded_cmd_t &cmd)           { this->fujicmd_write_host_slots(); }},         // 0xF3

        { FUJICMD_RESET,  [this](const iwm_decoded_cmd_t &cmd)                     {
             this->fujicmd_reset();
         }},   // 0xFF
        { SP_CTRL_RESET, [this](const iwm_decoded_cmd_t &cmd)                     {
             this->fujicmd_reset();
         }},   // 0x00
#ifdef DEV_RELAY_SLIP
        { SP_CTRL_CLEAR_DISKII_SEEN, [this](const iwm_decoded_cmd_t &cmd)              { SYSTEM_BUS.transaction_error(SP_ERR::NODRIVE); }},
#else
        { SP_CTRL_CLEAR_DISKII_SEEN, [this](const iwm_decoded_cmd_t &cmd)              { diskii_xface.d2_enable_seen = 0; SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET); SYSTEM_BUS.transaction_success(); }},
#endif

        { FUJICMD_MOUNT_ALL, [this](const iwm_decoded_cmd_t &cmd)                     { fujicmd_mount_all_success(); }},          // 0xD7
        { FUJICMD_MOUNT_IMAGE, [this](const iwm_decoded_cmd_t &cmd)                   { fujicmd_mount_disk_image_success(cmd.param(0), (disk_access_flags_t) cmd.param8(1)); }},  // 0xF8
        { FUJICMD_OPEN_DIRECTORY, [this](const iwm_decoded_cmd_t &cmd)                {
            SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
            uint8_t slot = cmd.param(0);
            if (fujicore_open_directory_success(slot, cmd.dataAsString().value()).is_error())
                SYSTEM_BUS.transaction_error(SP_ERR::IOERROR);
            else
                SYSTEM_BUS.transaction_success();
        }}     // 0xF7
    };

    status_handlers = {
        { 0xAA, [this](const iwm_decoded_cmd_t &cmd)                               { this->iwm_hello_world(); }},

#ifndef DEV_RELAY_SLIP
        { SP_STAT_GET_DISKII_SEEN, [this](const iwm_decoded_cmd_t &cmd)                  {
            transaction_begin(TRANS_STATE::NO_GET);
            transaction_put(diskii_xface.d2_enable_seen);
        }},
#endif

        { FUJICMD_DEVICE_ENABLE_STATUS, [this](const iwm_decoded_cmd_t &cmd)       { this->send_stat_get_enable(); }},                      // 0xD1
        { FUJICMD_GET_ADAPTERCONFIG_EXTENDED, [this](const iwm_decoded_cmd_t &cmd) { this->fujicmd_get_adapter_config_extended(); }},      // 0xC4
        { FUJICMD_GET_ADAPTERCONFIG, [this](const iwm_decoded_cmd_t &cmd)          { this->fujicmd_get_adapter_config(); }},               // 0xE8
        { FUJICMD_GET_DEVICE_FULLPATH, [this](const iwm_decoded_cmd_t &cmd)        { this->fujicmd_get_device_filename(cmd.param(0)); }},   // 0xDA
        { FUJICMD_GET_DEVICE1_FULLPATH, [this](const iwm_decoded_cmd_t &cmd)       { this->fujicmd_get_device_filename(cmd.command() - 160); }},   // 0xA0
        { FUJICMD_GET_DEVICE2_FULLPATH, [this](const iwm_decoded_cmd_t &cmd)       { this->fujicmd_get_device_filename(cmd.command() - 160); }},   // 0xA1
        { FUJICMD_GET_DEVICE3_FULLPATH, [this](const iwm_decoded_cmd_t &cmd)       { this->fujicmd_get_device_filename(cmd.command() - 160); }},   // 0xA2
        { FUJICMD_GET_DEVICE4_FULLPATH, [this](const iwm_decoded_cmd_t &cmd)       { this->fujicmd_get_device_filename(cmd.command() - 160); }},   // 0xA3
        { FUJICMD_GET_DEVICE5_FULLPATH, [this](const iwm_decoded_cmd_t &cmd)       { this->fujicmd_get_device_filename(cmd.command() - 160); }},   // 0xA4
        { FUJICMD_GET_DEVICE6_FULLPATH, [this](const iwm_decoded_cmd_t &cmd)       { this->fujicmd_get_device_filename(cmd.command() - 160); }},   // 0xA5
        { FUJICMD_GET_DEVICE7_FULLPATH, [this](const iwm_decoded_cmd_t &cmd)       { this->fujicmd_get_device_filename(cmd.command() - 160); }},   // 0xA6
        { FUJICMD_GET_DEVICE8_FULLPATH, [this](const iwm_decoded_cmd_t &cmd)       { this->fujicmd_get_device_filename(cmd.command() - 160); }},   // 0xA7
        { FUJICMD_GET_DEVICE9_FULLPATH, [this](const iwm_decoded_cmd_t &cmd)       { this->fujicmd_get_device_filename(cmd.command() - 160); }},   // 0xA8
        { FUJICMD_GET_DEVICE10_FULLPATH, [this](const iwm_decoded_cmd_t &cmd)      { this->fujicmd_get_device_filename(cmd.command() - 160); }},   // 0xA9
        { FUJICMD_GET_DIRECTORY_POSITION, [this](const iwm_decoded_cmd_t &cmd)     { this->fujicmd_get_directory_position(); }},           // 0xE5
        { FUJICMD_GET_HOST_PREFIX, [this](const iwm_decoded_cmd_t &cmd)            { }},                  // 0xE0
        { FUJICMD_GET_SCAN_RESULT, [this](const iwm_decoded_cmd_t &cmd)            { }},                  // 0xFC
        { FUJICMD_GET_SSID, [this](const iwm_decoded_cmd_t &cmd)                   { this->fujicmd_net_get_ssid(); }},                     // 0xFE
        { FUJICMD_GET_WIFI_ENABLED, [this](const iwm_decoded_cmd_t &cmd)           { this->iwm_stat_get_wifi_enabled(); }},                 // 0xEA
        { FUJICMD_GET_WIFISTATUS, [this](const iwm_decoded_cmd_t &cmd)             { this->fujicmd_net_get_wifi_status(); }},              // 0xFA
        { FUJICMD_READ_APPKEY, [this](const iwm_decoded_cmd_t &cmd)                { this->fujicmd_read_app_key(); }},                     // 0xDD
        { FUJICMD_READ_DEVICE_SLOTS, [this](const iwm_decoded_cmd_t &cmd)          { this->fujicmd_read_device_slots(); }},                // 0xF2
        { FUJICMD_READ_DIR_ENTRY, [this](const iwm_decoded_cmd_t &cmd)             { }},             // 0xF6
        { FUJICMD_READ_HOST_SLOTS, [this](const iwm_decoded_cmd_t &cmd)            { this->fujicmd_read_host_slots(); }},                  // 0xF4
        { FUJICMD_SCAN_NETWORKS, [this](const iwm_decoded_cmd_t &cmd)              { this->fujicmd_net_scan_networks(); }},                // 0xFD
        { FUJICMD_QRCODE_LENGTH, [this](const iwm_decoded_cmd_t &cmd)              { this->iwm_stat_qrcode_length(); }},                    // 0xBE
        { FUJICMD_QRCODE_OUTPUT, [this](const iwm_decoded_cmd_t &cmd)              { this->iwm_stat_qrcode_output(); }},                    // 0xBE
        { FUJICMD_STATUS, [this](const iwm_decoded_cmd_t &cmd)                     { this->fujicmd_status(); }},                      // 0x53
        { FUJICMD_GET_HEAP, [this](const iwm_decoded_cmd_t &cmd)                   { this->iwm_stat_get_heap(); }},                         // 0xC1
        { FUJICMD_GENERATE_GUID, [this](const iwm_decoded_cmd_t &cmd)              { this->fujicmd_generate_guid(); }},                     // 0xBB
    };

}

void iwmFuji::iwm_dummy_command(const iwm_decoded_cmd_t &cmd) // SP CTRL command
{
        Debug_printf("\r\nData Received: ");
        for (uint8_t byte : cmd.data().value())
            Debug_printf(" %02x", byte);
        transaction_complete();
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
void iwmFuji::iwm_ctrl_new_disk(const iwm_decoded_cmd_t &cmd)
{
    uint8_t hs = cmd.param(0);
    uint8_t ds = cmd.param(1);
    uint8_t t = cmd.param(2);
    u32le_t numBlocks;
    const uint8_t *ptr;

    ptr = cmd.data()->data();
    memcpy(&numBlocks, ptr, sizeof(numBlocks));
    ptr += sizeof(numBlocks);

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

void iwmFuji::iwm_ctrl_enable_device(const iwm_decoded_cmd_t &cmd)
{
        unsigned char d = cmd.param(0);

        Debug_printf("\nFuji cmd: ENABLE DEVICE");
        SYSTEM_BUS.enableDevice(d);
}

void iwmFuji::iwm_ctrl_disable_device(const iwm_decoded_cmd_t &cmd)
{
        unsigned char d = cmd.param(0);

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

void iwmFuji::iwm_open(const iwm_decoded_cmd_t &cmd) {}
void iwmFuji::iwm_close(const iwm_decoded_cmd_t &cmd) {}
void iwmFuji::iwm_read(const iwm_decoded_cmd_t &cmd) {}

void iwmFuji::iwm_status(const iwm_decoded_cmd_t &cmd)
{
    Debug_printf("\r\n[Fuji] Device %02x Status Code %02x\r\n", id(), cmd.command());

    // Let the base class handle standard commands
    if (fujiDevice::processCommand(cmd))
        return;

    auto it = status_handlers.find(cmd.command());
    if (it != status_handlers.end()) {
        it->second(cmd);
    } else {
        Debug_printf("ERROR: Unhandled status code: %02X\n", cmd.command());
        transaction_error();
    }
}

void iwmFuji::iwm_ctrl(const iwm_decoded_cmd_t &cmd)
{
    Debug_printf("\ntheFuji Device %02x Control Code %02x", id(), cmd.command());

    // Let the base class handle standard commands
    if (fujiDevice::processCommand(cmd))
        return;

    auto it = control_handlers.find(cmd.command());
    if (it != control_handlers.end()) {
        it->second(cmd);
    } else {
        Debug_printf("ERROR: Unhandled control code: %02X\n", cmd.command());
        SYSTEM_BUS.transaction_error(SP_ERR::BADCTL);
    }
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

void iwmFuji::iwm_ctrl_qrcode_input(const iwm_decoded_cmd_t &cmd)
{
    Debug_printf("FUJI: QRCODE INPUT (len: %d)\n", cmd.data()->size());
    transaction_begin(TRANS_STATE::NO_GET);
    _qrManager.data += cmd.dataAsString().value();
    transaction_complete();
}

void iwmFuji::iwm_ctrl_qrcode_encode(const iwm_decoded_cmd_t &cmd)
{
    uint8_t version = cmd.param8(0) & 0b01111111;
    uint8_t ecc_mode = cmd.param(1);
    bool shorten = cmd.param(2);

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

void iwmFuji::iwm_ctrl_qrcode_output(const iwm_decoded_cmd_t &cmd)
{
    Debug_printf("FUJI: QRCODE OUTPUT CONTROL\n");

    uint8_t output_mode = cmd.param(0);
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

#endif /* BUILD_APPLE */
