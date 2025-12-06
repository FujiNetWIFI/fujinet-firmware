#ifdef BUILD_COCO

#include "drivewireFuji.h"
#include "fujiCommandID.h"
#include "network.h"
#include "fnWiFi.h"
#include "base64.h"
#include "utils.h"
#include "compat_string.h"
#include "endianness.h"
#include "fuji_endian.h"

#define IMAGE_EXTENSION ".dsk"
#define LOBBY_URL       "tnfs://tnfs.fujinet.online/COCO/lobby.dsk"

drivewireFuji platformFuji;
fujiDevice *theFuji = &platformFuji; // Global fuji object.

// drivewireDisk drivewireDiskDevs[MAX_HOSTS];
drivewireNetwork drivewireNetDevs[MAX_NETWORK_DEVICES];

/**
 * Say the numbers 1-8 using phonetic tweaks.
 * @param n The number to say.
 */
void say_number(unsigned char n)
{
#ifdef TODO_SPEECH
    switch (n)
    {
    case 1:
        util_sam_say("WAH7NQ", true);
        break;
    case 2:
        util_sam_say("TUW7", true);
        break;
    case 3:
        util_sam_say("THRIYY7Q", true);
        break;
    case 4:
        util_sam_say("FOH7R", true);
        break;
    case 5:
        util_sam_say("F7AYVQ", true);
        break;
    case 6:
        util_sam_say("SIH7IHKSQ", true);
        break;
    case 7:
        util_sam_say("SEHV7EHNQ", true);
        break;
    case 8:
        util_sam_say("AEY74Q", true);
        break;
    default:
        Debug_printf("say_number() - Uncaught number %d", n);
    }
#endif
}

/**
 * Say swap label
 */
void say_swap_label()
{
#ifdef TODO_SPEECH
    // DISK
    util_sam_say("DIHSK7Q ", true);
#endif
}

// Constructor
drivewireFuji::drivewireFuji() : fujiDevice(MAX_DWDISK_DEVICES, IMAGE_EXTENSION, LOBBY_URL)
{
    // Helpful for debugging
    for (int i = 0; i < MAX_HOSTS; i++)
        _fnHosts[i].slotid = i;
}

size_t drivewireFuji::set_additional_direntry_details(fsdir_entry_t *f, uint8_t *dest,
                                                      uint8_t maxlen)
{
    return _set_additional_direntry_details(f, dest, maxlen, 100, SIZE_32_BE,
                                            HAS_DIR_ENTRY_FLAGS_SEPARATE, HAS_DIR_ENTRY_TYPE);
}

// This gets called when we're about to shutdown/reboot
void drivewireFuji::shutdown()
{
    for (int i = 0; i < MAX_DWDISK_DEVICES; i++)
        _fnDisks[i].disk_dev.unmount();
}

// Get network adapter configuration - extended
void drivewireFuji::get_adapter_config_extended()
{
    // also return string versions of the data to save the host some computing
    Debug_printf("Fuji cmd: GET ADAPTER CONFIG EXTENDED\r\n");
    AdapterConfigExtended cfg;
    memset(&cfg, 0, sizeof(cfg));       // ensures all strings are null terminated

    strlcpy(cfg.fn_version, fnSystem.get_fujinet_version(true), sizeof(cfg.fn_version));

    if (!fnWiFi.connected())
    {
        strlcpy(cfg.ssid, "NOT CONNECTED", sizeof(cfg.ssid));
    }
    else
    {
        strlcpy(cfg.hostname, fnSystem.Net.get_hostname().c_str(), sizeof(cfg.hostname));
        strlcpy(cfg.ssid, fnWiFi.get_current_ssid().c_str(), sizeof(cfg.ssid));
        fnWiFi.get_current_bssid(cfg.bssid);
        fnSystem.Net.get_ip4_info(cfg.localIP, cfg.netmask, cfg.gateway);
        fnSystem.Net.get_ip4_dns_info(cfg.dnsIP);
    }

    fnWiFi.get_mac(cfg.macAddress);

    // convert fields to strings
    strlcpy(cfg.sLocalIP, fnSystem.Net.get_ip4_address_str().c_str(), 16);
    strlcpy(cfg.sGateway, fnSystem.Net.get_ip4_gateway_str().c_str(), 16);
    strlcpy(cfg.sDnsIP,   fnSystem.Net.get_ip4_dns_str().c_str(),     16);
    strlcpy(cfg.sNetmask, fnSystem.Net.get_ip4_mask_str().c_str(),    16);

    sprintf(cfg.sMacAddress, "%02X:%02X:%02X:%02X:%02X:%02X", cfg.macAddress[0], cfg.macAddress[1], cfg.macAddress[2], cfg.macAddress[3], cfg.macAddress[4], cfg.macAddress[5]);
    sprintf(cfg.sBssid,      "%02X:%02X:%02X:%02X:%02X:%02X", cfg.bssid[0], cfg.bssid[1], cfg.bssid[2], cfg.bssid[3], cfg.bssid[4], cfg.bssid[5]);

    transaction_put(&cfg, sizeof(cfg));
}

//  Make new disk and shove into device slot
void drivewireFuji::new_disk()
{
    Debug_println("Fuji cmd: NEW DISK");

    struct
    {
        unsigned char numDisks;
        unsigned char hostSlot;
        unsigned char deviceSlot;
        char filename[MAX_FILENAME_LEN]; // WIll set this to MAX_FILENAME_LEN, later.
    } newDisk;

    transaction_get(&newDisk, sizeof(newDisk));

    Debug_printf("numDisks: %u\n",newDisk.numDisks);
    Debug_printf("hostSlot: %u\n",newDisk.hostSlot);
    Debug_printf("deviceSl: %u\n",newDisk.deviceSlot);
    Debug_printf("filename: %s\n",newDisk.filename);

    // A couple of reference variables to make things much easier to read...
    fujiDisk &disk = _fnDisks[newDisk.deviceSlot];
    fujiHost &host = _fnHosts[newDisk.hostSlot];

    disk.host_slot = newDisk.hostSlot;
    disk.access_mode = DISK_ACCESS_MODE_WRITE;
    strlcpy(disk.filename, newDisk.filename, sizeof(disk.filename));

    if (host.file_exists(disk.filename))
    {
        Debug_printf("drivewire_new_disk File exists: \"%s\"\n", disk.filename);
        transaction_error();
        return;
    }

    disk.fileh = host.fnfile_open(disk.filename, disk.filename, sizeof(disk.filename), "w");
    if (disk.fileh == nullptr)
    {
        Debug_printf("drivewire_new_disk Couldn't open file for writing: \"%s\"\n", disk.filename);
        return;
    }

    bool ok = disk.disk_dev.write_blank(disk.fileh, newDisk.numDisks);

    if (ok == NETWORK_ERROR_SUCCESS)
        transaction_complete();
    else
        transaction_error();

    fnio::fclose(disk.fileh);
}

void drivewireFuji::base64_encode_input()
{
    uint8_t lenh = SYSTEM_BUS.read();
    uint8_t lenl = SYSTEM_BUS.read();
    uint16_t len = lenh << 8 | lenl;

    if (!len)
    {
        Debug_printf("Zero length. Aborting.\n");
        transaction_error();
        return;
    }

    std::vector<unsigned char> p(len);
    transaction_get(p.data(), len);
    base64.base64_buffer += std::string((const char *)p.data(), len);
    transaction_complete();
}

void drivewireFuji::base64_encode_compute()
{
    size_t out_len;

    std::unique_ptr<char[]> p = Base64::encode(base64.base64_buffer.c_str(), base64.base64_buffer.size(), &out_len);

    if (!p)
    {
        Debug_printf("base64_encode_compute() failed.\n");
        transaction_error();
        return;
    }

    base64.base64_buffer.clear();
    base64.base64_buffer = std::string(p.get(), out_len);

    transaction_complete();
}

void drivewireFuji::base64_encode_length()
{
    size_t l = base64.base64_buffer.length();
    uint8_t o[4] =
    {
        (uint8_t)(l >> 24),
        (uint8_t)(l >> 16),
        (uint8_t)(l >> 8),
        (uint8_t)(l)
    };

    transaction_put(&o, 4);
}

void drivewireFuji::base64_encode_output()
{
    uint8_t lenl = SYSTEM_BUS.read();
    uint8_t lenh = SYSTEM_BUS.read();
    uint16_t len = lenh << 8 | lenl;

    if (!len)
    {
        Debug_printf("Refusing to send zero byte buffer. Exiting.");
        transaction_error();
        return;
    }

    std::vector<unsigned char> p(len);
    std::memcpy(p.data(), base64.base64_buffer.data(), len);
    base64.base64_buffer.erase(0, len);
    base64.base64_buffer.shrink_to_fit();

    transaction_put(p.data(), len);
}

void drivewireFuji::base64_decode_input()
{
    uint8_t lenl = SYSTEM_BUS.read();
    uint8_t lenh = SYSTEM_BUS.read();
    uint16_t len = lenh << 8 | lenl;

    if (!len)
    {
        Debug_printf("Refusing to input zero length. Exiting.\n");
        transaction_error();
        return;
    }

    std::vector<unsigned char> p(len);
    transaction_get(p.data(), len);
    base64.base64_buffer += std::string((const char *)p.data(), len);

    transaction_complete();
}

void drivewireFuji::base64_decode_compute()
{
    size_t out_len;

    Debug_printf("FUJI: BASE64 DECODE COMPUTE\n");

    std::unique_ptr<unsigned char[]> p = Base64::decode(base64.base64_buffer.c_str(), base64.base64_buffer.size(), &out_len);
    if (!p)
    {
        Debug_printf("base64_encode compute failed\n");
        transaction_error();
        return;
    }

    base64.base64_buffer.clear();
    base64.base64_buffer = std::string((const char *)p.get(), out_len);

    Debug_printf("Resulting BASE64 encoded data is: %u bytes\n", out_len);
    transaction_complete();
}

void drivewireFuji::base64_decode_length()
{
    Debug_printf("FUJI: BASE64 DECODE LENGTH\n");

    size_t len = base64.base64_buffer.length();
    uint8_t _response[4] = {
        (uint8_t)(len >>  24),
        (uint8_t)(len >>  16),
        (uint8_t)(len >>  8),
        (uint8_t)(len >>  0)
    };

    if (!len)
    {
        Debug_printf("BASE64 buffer is 0 bytes, sending error.\n");
        transaction_error();
        return;
    }

    Debug_printf("base64 buffer length: %u bytes\n", len);

    transaction_put(_response, 4);
}

void drivewireFuji::base64_decode_output()
{
    Debug_printf("FUJI: BASE64 DECODE OUTPUT\n");

    uint8_t lenl = SYSTEM_BUS.read();
    uint8_t lenh = SYSTEM_BUS.read();
    uint16_t len = lenh << 8 | lenl;

    if (!len)
    {
        Debug_printf("Refusing to send a zero byte buffer. Aborting\n");
        transaction_error();
        return;
    }
    else if (len > base64.base64_buffer.length())
    {
        Debug_printf("Requested %u bytes, but buffer is only %u bytes, aborting.\n", len, base64.base64_buffer.length());
        transaction_error();
        return;
    }
    else
    {
        Debug_printf("Requested %u bytes\n", len);
    }

    std::vector<unsigned char> p(len);
    memcpy(p.data(), base64.base64_buffer.data(), len);
    base64.base64_buffer.erase(0, len);
    base64.base64_buffer.shrink_to_fit();
    transaction_put(p.data(), len);
}

void drivewireFuji::hash_input()
{
    Debug_printf("FUJI: HASH INPUT\n");
    uint8_t lenl = SYSTEM_BUS.read();
    uint8_t lenh = SYSTEM_BUS.read();
    uint16_t len = lenh << 8 | lenl;


    if (!len)
    {
        Debug_printf("Invalid length. Aborting");
        transaction_error();
        return;
    }

    std::vector<uint8_t> p(len);
    transaction_get(p.data(), len);
    hasher.add_data(p);
    transaction_complete();
}

void drivewireFuji::hash_compute(bool clear_data)
{
    Debug_printf("FUJI: HASH COMPUTE\n");
    algorithm = Hash::to_algorithm(SYSTEM_BUS.read());
    hasher.compute(algorithm, clear_data);
    transaction_complete();
}

void drivewireFuji::hash_length()
{
    Debug_printf("FUJI: HASH LENGTH\n");
    uint8_t is_hex = SYSTEM_BUS.read() == 1;
    uint8_t r = hasher.hash_length(algorithm, is_hex);
    transaction_put(&r, 1);
}

void drivewireFuji::hash_output()
{
    Debug_printf("FUJI: HASH OUTPUT\n");

    uint8_t is_hex = SYSTEM_BUS.read() == 1;
    if (is_hex) {
        std::string output = hasher.output_hex();
        transaction_put(output.c_str(), output.size());
    } else {
        std::vector<uint8_t> hashed_data = hasher.output_binary();
        transaction_put(hashed_data.data(), hashed_data.size());
    }
}

void drivewireFuji::hash_clear()
{
    Debug_printf("FUJI: HASH INIT\n");
    hasher.clear();
    transaction_complete();
}

// Initializes base settings and adds our devices to the DRIVEWIRE bus
void drivewireFuji::setup()
{
    Debug_printf("theFuji->setup()\n");
    // set up Fuji device

    populate_slots_from_config();

    insert_boot_device(Config.get_general_boot_mode(), MEDIATYPE_UNKNOWN, &bootdisk);

    // Disable booting from CONFIG if our settings say to turn it off
    boot_config = Config.get_general_config_enabled();

#ifdef OBSOLETE
    // Disable status_wait if our settings say to turn it off
    status_wait_enabled = Config.get_general_status_wait_enabled();
#endif /* OBSOLETE */
}

void drivewireFuji::send_error()
{
    Debug_printf("drivewireFuji::send_error(%u)\n",_errorCode);
    SYSTEM_BUS.write(_errorCode);
}

void drivewireFuji::random()
{
    int r = rand();
    Debug_printf("drivewireFuji::random(%u)\n",r);
    transaction_put(&r, sizeof(r));
}

void drivewireFuji::send_response()
{
    // Send body
    SYSTEM_BUS.write((uint8_t *)_response.c_str(),_response.length());

    // Clear the response
    _response.clear();
    _response.shrink_to_fit();
}

void drivewireFuji::ready()
{
    SYSTEM_BUS.write(0x01); // Yes, ready.
}

void drivewireFuji::process()
{
    uint8_t c = SYSTEM_BUS.read();

    _errorCode = 1;
    switch (c)
    {
    case FUJICMD_SEND_ERROR:
        send_error();
        break;
    case FUJICMD_RESET:
        fnSystem.reboot();
        break;
    case FUJICMD_GET_ADAPTERCONFIG:
        fujicmd_get_adapter_config();
        break;
    case FUJICMD_GET_ADAPTERCONFIG_EXTENDED:
        get_adapter_config_extended();
        break;
    case FUJICMD_GET_SCAN_RESULT:
        fujicmd_net_scan_result(SYSTEM_BUS.read());
        break;
    case FUJICMD_SCAN_NETWORKS:
        fujicmd_net_scan_networks();
        break;
    case FUJICMD_SET_SSID:
        {
            SSIDConfig cfg;
            if (!transaction_get(&cfg, sizeof(cfg)))
                transaction_error();
            else
                fujicmd_net_set_ssid_success(cfg.ssid, cfg.password, false);
        }
        break;
    case FUJICMD_GET_SSID:
        fujicmd_net_get_ssid();
        break;
    case FUJICMD_READ_HOST_SLOTS:
        fujicmd_read_host_slots();
        break;
    case FUJICMD_READ_DEVICE_SLOTS:
        fujicmd_read_device_slots();
        break;
    case FUJICMD_WRITE_DEVICE_SLOTS:
        fujicmd_write_device_slots();
        break;
    case FUJICMD_WRITE_HOST_SLOTS:
        fujicmd_write_host_slots();
        break;
    case FUJICMD_GET_WIFI_ENABLED:
        fujicmd_net_get_wifi_enabled();
        break;
    case FUJICMD_GET_WIFISTATUS:
        fujicmd_net_get_wifi_status();
        break;
    case FUJICMD_MOUNT_HOST:
        fujicmd_mount_host_success(SYSTEM_BUS.read());
        break;
    case FUJICMD_OPEN_DIRECTORY:
        fujicmd_open_directory_success(SYSTEM_BUS.read());
        break;
    case FUJICMD_CLOSE_DIRECTORY:
        fujicmd_close_directory();
        break;
    case FUJICMD_READ_DIR_ENTRY:
        {
            uint8_t maxlen = SYSTEM_BUS.read();
            uint8_t addtl = SYSTEM_BUS.read();
            fujicmd_read_directory_entry(maxlen, addtl);
        }
        break;
    case FUJICMD_SET_DIRECTORY_POSITION:
        {
            uint8_t h, l;
            h = SYSTEM_BUS.read();
            l = SYSTEM_BUS.read();
            uint16_t pos = UINT16_FROM_HILOBYTES(h, l);

            fujicmd_set_directory_position(pos);
        }
        break;
    case FUJICMD_SET_DEVICE_FULLPATH:
        {
            uint8_t slot = SYSTEM_BUS.read();
            uint8_t host = SYSTEM_BUS.read();
            uint8_t mode = SYSTEM_BUS.read();
            fujicmd_set_device_filename_success(slot, host, (disk_access_flags_t) mode);
        }
        break;
    case FUJICMD_GET_DEVICE_FULLPATH:
        fujicmd_get_device_filename(SYSTEM_BUS.read());
        break;
    case FUJICMD_MOUNT_IMAGE:
        {
            uint8_t slot = SYSTEM_BUS.read();
            uint8_t mode = SYSTEM_BUS.read();
            fujicmd_mount_disk_image_success(slot, (disk_access_flags_t) mode);
        }
        break;
    case FUJICMD_UNMOUNT_HOST:
        fujicmd_unmount_host_success(SYSTEM_BUS.read());
        break;
    case FUJICMD_UNMOUNT_IMAGE:
        fujicmd_unmount_disk_image_success(SYSTEM_BUS.read());
        break;
    case FUJICMD_NEW_DISK:
        new_disk();
        break;
    case FUJICMD_SEND_RESPONSE:
        send_response();
        break;
    case FUJICMD_DEVICE_READY:
        ready();
        break;
    case FUJICMD_OPEN_APPKEY:
        fujicmd_open_app_key();
        break;
    case FUJICMD_CLOSE_APPKEY:
        fujicmd_close_app_key();
        break;
    case FUJICMD_READ_APPKEY:
        fujicmd_read_app_key();
        break;
    case FUJICMD_WRITE_APPKEY:
        {
            uint8_t lenh = SYSTEM_BUS.read();
            uint8_t lenl = SYSTEM_BUS.read();
            uint16_t len = lenh << 8 | lenl;
            fujicmd_write_app_key(len);
        }
        break;
    case FUJICMD_RANDOM_NUMBER:
        random();
        break;
    case FUJICMD_BASE64_ENCODE_INPUT:
        base64_encode_input();
        break;
    case FUJICMD_BASE64_ENCODE_COMPUTE:
        base64_encode_compute();
        break;
    case FUJICMD_BASE64_ENCODE_LENGTH:
        base64_encode_length();
        break;
    case FUJICMD_BASE64_ENCODE_OUTPUT:
        base64_encode_output();
        break;
    case FUJICMD_BASE64_DECODE_INPUT:
        base64_decode_input();
        break;
    case FUJICMD_BASE64_DECODE_COMPUTE:
        base64_decode_compute();
        break;
    case FUJICMD_BASE64_DECODE_LENGTH:
        base64_decode_length();
        break;
    case FUJICMD_BASE64_DECODE_OUTPUT:
        base64_decode_output();
        break;
    case FUJICMD_HASH_INPUT:
        hash_input();
        break;
    case FUJICMD_HASH_COMPUTE:
        hash_compute(true);
        break;
    case FUJICMD_HASH_COMPUTE_NO_CLEAR:
        hash_compute(false);
        break;
    case FUJICMD_HASH_LENGTH:
        hash_length();
        break;
    case FUJICMD_HASH_OUTPUT:
        hash_output();
        break;
    case FUJICMD_HASH_CLEAR:
        hash_clear();
        break;
    case FUJICMD_SET_BOOT_MODE:
        fujicmd_set_boot_mode(SYSTEM_BUS.read(), MEDIATYPE_UNKNOWN, &bootdisk);
        break;
    case FUJICMD_MOUNT_ALL:
        fujicmd_mount_all_success();
        break;
    case FUJICMD_GET_HOST_PREFIX:
        fujicmd_get_host_prefix(SYSTEM_BUS.read());
        break;
    case FUJICMD_SET_HOST_PREFIX:
        fujicmd_set_host_prefix(SYSTEM_BUS.read());
        break;
    case FUJICMD_COPY_FILE:
        {
            uint8_t source = SYSTEM_BUS.read();
            uint8_t dest = SYSTEM_BUS.read();
            char dirpath[256];
            transaction_get(dirpath, sizeof(dirpath));
            fujicmd_copy_file_success(source, dest, dirpath);
        }
        break;
    default:
        break;
    }
}

std::optional<std::vector<uint8_t>> drivewireFuji::fujicore_read_app_key()
{
    auto result = fujiDevice::fujicore_read_app_key();

    if (result)
    {
        uint16_t len = htobe16(result->size());
        result->resize(MAX_APPKEY_LEN, 0);
        const uint8_t *len_bytes = reinterpret_cast<const uint8_t*>(&len);
        result->insert(result->begin(), len_bytes, len_bytes + sizeof(len));
    }

    return result;
}

#endif /* BUILD_COCO */
