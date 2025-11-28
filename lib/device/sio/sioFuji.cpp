#ifdef BUILD_ATARI

#include "sioFuji.h"
#include "httpService.h"
#include "utils.h"
#include "base64.h"
#include "../../qrcode/qrmanager.h"
#include "compat_string.h"
#include "fuji_endian.h"

#define IMAGE_EXTENSION ".atr"

sioFuji platformFuji;
fujiDevice *theFuji = &platformFuji; // Global fuji object.

#ifdef ESP_PLATFORM
std::unique_ptr<sioNetwork, PSRAMDeleter<sioNetwork>> sioNetDevs[MAX_NETWORK_DEVICES];
#else
std::unique_ptr<sioNetwork> sioNetDevs[MAX_NETWORK_DEVICES];
#endif

bool _validate_host_slot(uint8_t slot, const char *dmsg = nullptr);
bool _validate_device_slot(uint8_t slot, const char *dmsg = nullptr);

bool _validate_host_slot(uint8_t slot, const char *dmsg)
{
    if (slot < MAX_HOSTS)
        return true;

    if (dmsg == NULL)
    {
        Debug_printf("!! Invalid host slot %hu\n", slot);
    }
    else
    {
        Debug_printf("!! Invalid host slot %hu @ %s\n", slot, dmsg);
    }

    return false;
}

bool _validate_device_slot(uint8_t slot, const char *dmsg)
{
    if (slot < MAX_DISK_DEVICES)
        return true;

    if (dmsg == NULL)
    {
        Debug_printf("!! Invalid device slot %hu\n", slot);
    }
    else
    {
        Debug_printf("!! Invalid device slot %hu @ %s\n", slot, dmsg);
    }

    return false;
}

/**
 * Say the numbers 1-8 using phonetic tweaks.
 * @param n The number to say.
 */
void say_number(unsigned char n)
{
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
        Debug_printf("say_number() - Uncaught number %d\n", n);
    }
}

/**
 * Say swap label
 */
void say_swap_label()
{
    // DISK
    util_sam_say("DIHSK7Q ", true);
}

// Constructor
sioFuji::sioFuji()
{
    // Helpful for debugging
    for (int i = 0; i < MAX_HOSTS; i++)
        _fnHosts[i].slotid = i;

#ifdef ESP_PLATFORM
    for (int i = 0; i < MAX_NETWORK_DEVICES; ++i)
    {
        PSRAMAllocator<sioNetwork> allocator;
        sioNetwork* ptr = allocator.allocate(1); // Allocate memory for one sioNetwork object

        if (ptr != nullptr)
        {
            new (ptr) sioNetwork(); // Construct the object using placement new
            sioNetDevs[i] = std::unique_ptr<sioNetwork, PSRAMDeleter<sioNetwork>>(ptr); // Store in smart pointer
        }
    }
#else
    for (int i = 0; i < MAX_NETWORK_DEVICES; i++)
    {
        sioNetwork *ptr = (sioNetwork *) malloc(sizeof(sioNetwork));
        if (ptr != nullptr) {
            new (ptr) sioNetwork();
            sioNetDevs[i] = std::unique_ptr<sioNetwork>(ptr);
        }
    }
#endif

}

// Set SSID
void sioFuji::sio_net_set_ssid()
{
    SSIDConfig cfg;
    if (!transaction_get(&cfg, sizeof(cfg)))
        transaction_error();
    else
        fujicmd_net_set_ssid_success(cfg.ssid, cfg.password, cmdFrame.aux1);
}

// Set SIO baudrate
void sioFuji::sio_set_baudrate()
{

    int br = 0;

    switch(cmdFrame.aux1) {

        case 0:
            br = 19200;
            break;

        case 1:
            br = 38400;
            break;

        case 2:
            br = 57600;
            break;

        case 3:
            br = 115200;
            break;

        case 4:
            br = 230400;
            break;

        case 5:
            br = 460800;
            break;

        case 6:
            br = 921600;
            break;

        default:
            transaction_error();
            return;
    }

    // send complete with current baudrate
    transaction_complete();

    SYSTEM_BUS.flushOutput();
#ifndef ESP_PLATFORM
    fnSystem.delay_microseconds(2000);
#endif
    SYSTEM_BUS.setBaudrate(br);
}

// DEBUG TAPE
void sioFuji::debug_tape()
{
    // if not mounted then disable cassette and do nothing
    // if mounted then activate cassette
    // if mounted and active, then deactivate
    // no longer need to handle file open/close
    if (_cassetteDev.is_mounted() == true)
    {
        if (_cassetteDev.is_active() == false)
        {
            Debug_println("::debug_tape ENABLE");
            _cassetteDev.sio_enable_cassette();
        }
        else
        {
            Debug_println("::debug_tape DISABLE");
            _cassetteDev.sio_disable_cassette();
        }
    }
    else
    {
        Debug_println("::debug_tape NO CAS FILE MOUNTED");
        Debug_println("::debug_tape DISABLE");
        _cassetteDev.sio_disable_cassette();
    }
}

#if 0 //ndef ESP_PLATFORM
int sioFuji::_on_ok(bool siomode)
{
    if (siomode) transaction_complete();
    return 0;
}

int sioFuji::_on_error(bool siomode, int rc)
{
    if (siomode) transaction_error();
    return rc;
}
#endif

//  Make new disk and shove into device slot
void sioFuji::sio_new_disk()
{
    Debug_println("Fuji cmd: NEW DISK");

    struct
    {
        unsigned short numSectors;
        unsigned short sectorSize;
        unsigned char hostSlot;
        unsigned char deviceSlot;
        char filename[MAX_FILENAME_LEN]; // WIll set this to MAX_FILENAME_LEN, later.
    } newDisk;

    // Ask for details on the new disk to create
    if (!transaction_get(&newDisk, sizeof(newDisk)))
    {
        Debug_print("sio_new_disk Bad checksum\n");
        transaction_error();
        return;
    }
    if (newDisk.deviceSlot >= MAX_DISK_DEVICES || newDisk.hostSlot >= MAX_HOSTS)
    {
        Debug_print("sio_new_disk Bad disk or host slot parameter\n");
        transaction_error();
        return;
    }
    // A couple of reference variables to make things much easier to read...
    fujiDisk &disk = _fnDisks[newDisk.deviceSlot];
    fujiHost &host = _fnHosts[newDisk.hostSlot];

    disk.host_slot = newDisk.hostSlot;
    disk.access_mode = DISK_ACCESS_MODE_WRITE;
    strlcpy(disk.filename, newDisk.filename, sizeof(disk.filename));

    if (host.file_exists(disk.filename))
    {
        Debug_printf("sio_new_disk File exists: \"%s\"\n", disk.filename);
        transaction_error();
        return;
    }

    disk.fileh = host.fnfile_open(disk.filename, disk.filename, sizeof(disk.filename), FILE_WRITE);
    if (disk.fileh == nullptr)
    {
        Debug_printf("sio_new_disk Couldn't open file for writing: \"%s\"\n", disk.filename);
        transaction_error();
        return;
    }

    bool ok = disk.disk_dev.write_blank(disk.fileh, newDisk.sectorSize, newDisk.numSectors);
    fnio::fclose(disk.fileh);

    if (ok == false)
    {
        Debug_print("sio_new_disk Data write failed\n");
        transaction_error();
        return;
    }

    Debug_print("sio_new_disk succeeded\n");
    transaction_complete();
}

// AUX1 is our index value (from 0 to SIO_HISPEED_LOWEST_INDEX, for FN-PC 0 .. 10, 16)
// AUX2 requests a save of the change if set to 1
void sioFuji::sio_set_hsio_index()
{
    Debug_println("Fuji cmd: SET HSIO INDEX");

    // DAUX1 holds the desired index value
    uint8_t index = cmdFrame.aux1;

    // Make sure it's a valid value
#ifdef ESP_PLATFORM
    if (index > SIO_HISPEED_LOWEST_INDEX)
#else
    if (index > SIO_HISPEED_LOWEST_INDEX && index != SIO_HISPEED_x2_INDEX) // accept 0 .. 10, 16
#endif
    {
        transaction_error();
        return;
    }

    SYSTEM_BUS.setHighSpeedIndex(index);

    // Go ahead and save it if AUX2 = 1
    if (cmdFrame.aux2 & 1)
    {
        Config.store_general_hsioindex(index);
        Config.save();
    }

    transaction_complete();
}

// Initializes base settings and adds our devices to the SIO bus
void sioFuji::setup()
{
    // set up Fuji device
    populate_slots_from_config();

    insert_boot_device(Config.get_general_boot_mode(), IMAGE_EXTENSION, MEDIATYPE_UNKNOWN, &bootdisk);

    // Disable booting from CONFIG if our settings say to turn it off
    boot_config = Config.get_general_config_enabled();

    // Disable status_wait if our settings say to turn it off
    status_wait_enabled = Config.get_general_status_wait_enabled();

    // Add our devices to the SIO bus
    for (int i = 0; i < MAX_DISK_DEVICES; i++)
        SYSTEM_BUS.addDevice(&_fnDisks[i].disk_dev, (fujiDeviceID_t) (FUJI_DEVICEID_DISK + i));

    for (int i = 0; i < MAX_NETWORK_DEVICES; i++)
        SYSTEM_BUS.addDevice(sioNetDevs[i].get(),
                             (fujiDeviceID_t) (FUJI_DEVICEID_NETWORK + i));

    SYSTEM_BUS.addDevice(&_cassetteDev, FUJI_DEVICEID_CASSETTE);
    cassette()->set_buttons(Config.get_cassette_buttons());
    cassette()->set_pulldown(Config.get_cassette_pulldown());
}

void sioFuji::sio_qrcode_input()
{
    uint16_t len = sio_get_aux();

    Debug_printf("FUJI: QRCODE INPUT (len: %d)\n", len);

    if (!len)
    {
        Debug_printf("Invalid length. Aborting");
        transaction_error();
        return;
    }

    std::vector<unsigned char> p(len);
    transaction_get(p.data(), len);
    _qrManager.data += std::string((const char *)p.data(), len);
    transaction_complete();
}

void sioFuji::sio_qrcode_encode()
{
    uint16_t aux = sio_get_aux();
    uint8_t version = aux & 0b01111111;
    uint8_t ecc_mode = ((aux >> 8) & 0b00000011);
    bool shorten = (aux >> 12) & 0b00000001;

    Debug_printf("FUJI: QRCODE ENCODE\n");
    Debug_printf("QR Version: %d, ECC: %d, Shorten: %s\n", version, ecc_mode, shorten ? "Y" : "N");

    std::string url = _qrManager.data;

    if (shorten) {
        url = fnHTTPD.shorten_url(url);
    }

    _qrManager.version(version);
    _qrManager.ecc((qr_ecc_t)ecc_mode);
    _qrManager.output_mode = QR_OUTPUT_MODE_ATASCII;
    _qrManager.encode();

    _qrManager.data.clear();

    if (!_qrManager.code.size())
    {
        Debug_printf("QR code encoding failed\n");
        //sio_error();
        transaction_error();
        return;
    }

    Debug_printf("Resulting QR code is: %u modules\n", _qrManager.code.size());
    //sio_complete();
    transaction_complete();
}

void sioFuji::sio_qrcode_length()
{
    Debug_printf("FUJI: QRCODE LENGTH\n");
    uint8_t output_mode = sio_get_aux();
    Debug_printf("Output mode: %i\n", output_mode);

    size_t len = _qrManager.size();

    // A bit gross to have a side effect from length command, but not enough aux bytes
    // to specify version, ecc, *and* output mode for the encode command. Also can't
    // just wait for output command, because output mode determines buffer length,
    if (len && (output_mode != _qrManager.output_mode)) {
        _qrManager.output_mode = (ouput_mode_t)output_mode;
        _qrManager.encode();
        len = _qrManager.code.size();
    }

    uint8_t response[4] = {
        (uint8_t)(len >> 0),
        (uint8_t)(len >> 8),
        (uint8_t)(len >> 16),
        (uint8_t)(len >> 24)
    };

    if (!len)
    {
        Debug_printf("QR code buffer is 0 bytes, sending error.\n");
        bus_to_computer(response, sizeof(response), true);
    }

    Debug_printf("QR code buffer length: %u bytes\n", len);

    bus_to_computer(response, sizeof(response), false);
}

void sioFuji::sio_qrcode_output()
{
    Debug_printf("FUJI: QRCODE OUTPUT\n");

    size_t len = sio_get_aux();

    if (!len)
    {
        Debug_printf("Refusing to send a zero byte buffer. Aborting\n");
        return;
    }
    else if (len > _qrManager.size())
    {
        Debug_printf("Requested %u bytes, but buffer is only %u bytes, aborting.\n", len, _qrManager.code.size());
        return;
    }
    else
    {
        Debug_printf("Requested %u bytes\n", len);
    }

    bus_to_computer(&_qrManager.code[0], len, false);

    _qrManager.code.clear();
    _qrManager.code.shrink_to_fit();
}

void sioFuji::sio_base64_encode_input()
{
    uint16_t len = sio_get_aux();

    Debug_printf("FUJI: BASE64 ENCODE INPUT\n");

    if (!len)
    {
        Debug_printf("Invalid length. Aborting");
        transaction_error();
        return;
    }

    std::vector<unsigned char> p(len);
    transaction_get(p.data(), len);
    base64.base64_buffer += std::string((const char *)p.data(), len);
    transaction_complete();
}

void sioFuji::sio_base64_encode_compute()
{
    size_t out_len;

    Debug_printf("FUJI: BASE64 ENCODE COMPUTE\n");

    std::unique_ptr<char[]> p = Base64::encode(base64.base64_buffer.c_str(), base64.base64_buffer.size(), &out_len);
    if (!p)
    {
        Debug_printf("base64_encode compute failed\n");
        transaction_error();
        return;
    }

    base64.base64_buffer.clear();
    base64.base64_buffer = std::string(p.get(), out_len);

    Debug_printf("Resulting BASE64 encoded data is: %u bytes\n", out_len);
    transaction_complete();
}

void sioFuji::sio_base64_encode_length()
{
    Debug_printf("FUJI: BASE64 ENCODE LENGTH\n");

    size_t l = base64.base64_buffer.length();
    uint8_t response[4] = {
        (uint8_t)(l >>  0),
        (uint8_t)(l >>  8),
        (uint8_t)(l >>  16),
        (uint8_t)(l >>  24)
    };

    if (!l)
    {
        Debug_printf("BASE64 buffer is 0 bytes, sending error.\n");
        transaction_put(response, sizeof(response), true);
    }

    Debug_printf("base64 buffer length: %u bytes\n", l);

    transaction_put(response, sizeof(response), false);
}

void sioFuji::sio_base64_encode_output()
{
    Debug_printf("FUJI: BASE64 ENCODE OUTPUT\n");

    size_t len = sio_get_aux();

    if (!len)
    {
        Debug_printf("Refusing to send a zero byte buffer. Aborting\n");
        return;
    }
    else if (len > base64.base64_buffer.length())
    {
        Debug_printf("Requested %u bytes, but buffer is only %u bytes, aborting.\n", len, base64.base64_buffer.length());
        return;
    }
    else
    {
        Debug_printf("Requested %u bytes\n", len);
    }

    std::vector<unsigned char> p(len);
    std::memcpy(p.data(), base64.base64_buffer.data(), len);
    base64.base64_buffer.erase(0, len);
    base64.base64_buffer.shrink_to_fit();

    transaction_put(p.data(), len, false);
}

void sioFuji::sio_random_number()
{
    int r = rand();
    transaction_put(&r,sizeof(int),true);
}

void sioFuji::sio_base64_decode_input()
{
    uint16_t len = sio_get_aux();

    Debug_printf("FUJI: BASE64 DECODE INPUT\n");

    if (!len)
    {
        Debug_printf("Invalid length. Aborting");
        transaction_error();
        return;
    }

    std::vector<unsigned char> p(len);
    transaction_get(p.data(), len);
    base64.base64_buffer += std::string((const char *)p.data(), len);
    transaction_complete();
}

void sioFuji::sio_base64_decode_compute()
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

void sioFuji::sio_base64_decode_length()
{
    Debug_printf("FUJI: BASE64 DECODE LENGTH\n");

    size_t len = base64.base64_buffer.length();
    uint8_t response[4] = {
        (uint8_t)(len >>  0),
        (uint8_t)(len >>  8),
        (uint8_t)(len >>  16),
        (uint8_t)(len >>  24)
    };

    if (!len)
    {
        Debug_printf("BASE64 buffer is 0 bytes, sending error.\n");
        transaction_put(response, sizeof(response), true);
        return;
    }

    Debug_printf("base64 buffer length: %u bytes\n", len);

    transaction_put(response, sizeof(response), false);
}

void sioFuji::sio_base64_decode_output()
{
    Debug_printf("FUJI: BASE64 DECODE OUTPUT\n");

    size_t len = sio_get_aux();

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
    transaction_put(p.data(), len, false);
}

void sioFuji::sio_hash_input()
{
    Debug_printf("FUJI: HASH INPUT\n");
    uint16_t len = sio_get_aux();
    if (!len)
    {
        Debug_printf("Invalid length. Aborting");
        transaction_error();
        return;
    }

    std::vector<unsigned char> p(len);
    transaction_get(p.data(), len);
    hasher.add_data(p);
    transaction_complete();
}

void sioFuji::sio_hash_compute(bool clear_data)
{
    Debug_printf("FUJI: HASH COMPUTE\n");
    algorithm = Hash::to_algorithm(sio_get_aux());
    hasher.compute(algorithm, clear_data);
    transaction_complete();
}

void sioFuji::sio_hash_length()
{
    Debug_printf("FUJI: HASH LENGTH\n");
    uint16_t is_hex = sio_get_aux() == 1;
    uint8_t r = hasher.hash_length(algorithm, is_hex);
    transaction_put(&r, 1, false);
}

void sioFuji::sio_hash_output()
{
    Debug_printf("FUJI: HASH OUTPUT\n");
    uint16_t is_hex = sio_get_aux() == 1;

    std::vector<uint8_t> hashed_data;
    if (is_hex) {
        std::string hex = hasher.output_hex();
        hashed_data.insert(hashed_data.end(), hex.begin(), hex.end());
    } else {
        hashed_data = hasher.output_binary();
    }
    transaction_put(hashed_data.data(), hashed_data.size(), false);
}

void sioFuji::sio_hash_clear()
{
    Debug_printf("FUJI: HASH CLEAR\n");
    hasher.clear();
    transaction_complete();
}

void sioFuji::sio_copy_file()
{
    char csBuf[256];

    if (!transaction_get(csBuf, sizeof(csBuf)))
    {
        transaction_error();
        return;
    }

    fujicmd_copy_file_success(cmdFrame.aux1, cmdFrame.aux2, csBuf);
}

void sioFuji::sio_process(uint32_t commanddata, uint8_t checksum)
{
    cmdFrame.commanddata = commanddata;
    cmdFrame.checksum = checksum;

    Debug_printf("sioFuji::fujicmd_process() called, baud: %d\n", SYSTEM_BUS.getBaudrate());

    switch (cmdFrame.comnd)
    {
    case FUJICMD_HSIO_INDEX:
        //sio_ack();
        sio_high_speed();
        break;
    case FUJICMD_SET_HSIO_INDEX:
        //sio_ack();
        sio_set_hsio_index();
        break;
    case FUJICMD_STATUS:
        //sio_ack();
        sio_status();
        break;
    case FUJICMD_RESET:
        //sio_ack();
        fujicmd_reset();
        break;
    case FUJICMD_SCAN_NETWORKS:
        //sio_ack();
        fujicmd_net_scan_networks();
        break;
    case FUJICMD_GET_SCAN_RESULT:
        //sio_ack();
        fujicmd_net_scan_result(cmdFrame.aux1);
        break;
    case FUJICMD_SET_SSID:
        //sio_late_ack();
        sio_net_set_ssid();
        break;
    case FUJICMD_GET_SSID:
        //sio_ack();
        fujicmd_net_get_ssid();
        break;
    case FUJICMD_GET_WIFISTATUS:
        //sio_ack();
        fujicmd_net_get_wifi_status();
        break;
    case FUJICMD_MOUNT_HOST:
        //sio_ack();
        fujicmd_mount_host_success(cmdFrame.aux1);
        break;
    case FUJICMD_MOUNT_IMAGE:
        //sio_ack();
        fujicmd_mount_disk_image_success(cmdFrame.aux1, cmdFrame.aux2);
        break;
    case FUJICMD_OPEN_DIRECTORY:
        //sio_late_ack();
        {
            char dirpath[256];

            if (!transaction_get(dirpath, sizeof(dirpath))) {
                transaction_error();
                return;
            }

            fujicmd_open_directory_success(cmdFrame.aux1,
                                           std::string(dirpath, sizeof(dirpath)));
        }
        break;
    case FUJICMD_READ_DIR_ENTRY:
        //sio_ack();
        fujicmd_read_directory_entry(cmdFrame.aux1, cmdFrame.aux2);
        break;
    case FUJICMD_CLOSE_DIRECTORY:
        //sio_ack();
        fujicmd_close_directory();
        break;
    case FUJICMD_GET_DIRECTORY_POSITION:
        //sio_ack();
        fujicmd_get_directory_position();
        break;
    case FUJICMD_SET_DIRECTORY_POSITION:
        //sio_ack();
        fujicmd_set_directory_position(le16toh(cmdFrame.aux12));
        break;
    case FUJICMD_READ_HOST_SLOTS:
        //sio_ack();
        fujicmd_read_host_slots();
        break;
    case FUJICMD_WRITE_HOST_SLOTS:
        //sio_late_ack();
        fujicmd_write_host_slots();
        break;
    case FUJICMD_READ_DEVICE_SLOTS:
        //sio_ack();
        fujicmd_read_device_slots(MAX_DISK_DEVICES);
        break;
    case FUJICMD_WRITE_DEVICE_SLOTS:
        //sio_late_ack();
        fujicmd_write_device_slots(MAX_DISK_DEVICES);
        break;
    case FUJICMD_GET_WIFI_ENABLED:
        //sio_ack();
        fujicmd_net_get_wifi_enabled();
        break;
    case FUJICMD_SET_BAUDRATE:
        //sio_ack();
        sio_set_baudrate();
        break;
    case FUJICMD_UNMOUNT_IMAGE:
        //sio_ack();
        fujicmd_unmount_disk_image_success(cmdFrame.aux1);
        break;
    case FUJICMD_GET_ADAPTERCONFIG:
        //sio_ack();
        fujicmd_get_adapter_config();
        break;
    case FUJICMD_GET_ADAPTERCONFIG_EXTENDED:
        //sio_ack();
        fujicmd_get_adapter_config_extended();
        break;
    case FUJICMD_NEW_DISK:
        //sio_late_ack();
        sio_new_disk();
        break;
    case FUJICMD_UNMOUNT_HOST:
        //sio_ack();
        fujicmd_unmount_host_success(cmdFrame.aux1);
        break;
    case FUJICMD_SET_DEVICE_FULLPATH:
        //sio_late_ack();
        fujicmd_set_device_filename_success(cmdFrame.aux1, cmdFrame.aux2 >> 4, cmdFrame.aux2 & 0x0F);
        break;
    case FUJICMD_SET_HOST_PREFIX:
        //sio_late_ack();
        fujicmd_set_host_prefix(cmdFrame.aux1);
        break;
    case FUJICMD_GET_HOST_PREFIX:
        //sio_ack();
        fujicmd_get_host_prefix(cmdFrame.aux1);
        break;
    case FUJICMD_SET_SIO_EXTERNAL_CLOCK:
        //sio_ack();
        fujicmd_set_sio_external_clock(le16toh(cmdFrame.aux12));
        break;
    case FUJICMD_WRITE_APPKEY:
        //sio_late_ack();
        fujicmd_write_app_key(le16toh(cmdFrame.aux12));
        break;
    case FUJICMD_READ_APPKEY:
        //sio_ack();
        fujicmd_read_app_key();
        break;
    case FUJICMD_OPEN_APPKEY:
        //sio_late_ack();
        fujicmd_open_app_key();
        break;
    case FUJICMD_CLOSE_APPKEY:
        //sio_ack();
        fujicmd_close_app_key();
        break;
    case FUJICMD_GET_DEVICE_FULLPATH:
        //sio_ack();
        fujicmd_get_device_filename(cmdFrame.aux1);
        break;
    case FUJICMD_CONFIG_BOOT:
        //sio_ack();
        fujicmd_set_boot_config(cmdFrame.aux1);
        break;
    case FUJICMD_COPY_FILE:
        //sio_late_ack();
        sio_copy_file();
        break;
    case FUJICMD_MOUNT_ALL:
        //sio_ack();
        fujicmd_mount_all_success();
        break;
    case FUJICMD_SET_BOOT_MODE:
        //sio_ack();
        fujicmd_set_boot_mode(cmdFrame.aux1, IMAGE_EXTENSION, MEDIATYPE_UNKNOWN, &bootdisk);
        break;
    case FUJICMD_ENABLE_UDPSTREAM:
        //sio_late_ack();
        fujicmd_enable_udpstream(le16toh(cmdFrame.aux12));
        break;
    case FUJICMD_QRCODE_INPUT:
        //sio_ack();
        sio_qrcode_input();
        break;
    case FUJICMD_QRCODE_ENCODE:
        //sio_ack();
        sio_qrcode_encode();
        break;
    case FUJICMD_QRCODE_LENGTH:
        //sio_ack();
        sio_qrcode_length();
        break;
    case FUJICMD_QRCODE_OUTPUT:
        //sio_ack();
        sio_qrcode_output();
        break;
    case FUJICMD_BASE64_ENCODE_INPUT:
        //sio_late_ack();
        sio_base64_encode_input();
        break;
    case FUJICMD_BASE64_ENCODE_COMPUTE:
        //sio_ack();
        sio_base64_encode_compute();
        break;
    case FUJICMD_BASE64_ENCODE_LENGTH:
        //sio_ack();
        sio_base64_encode_length();
        break;
    case FUJICMD_BASE64_ENCODE_OUTPUT:
        //sio_ack();
        sio_base64_encode_output();
        break;
    case FUJICMD_BASE64_DECODE_INPUT:
        //sio_late_ack();
        sio_base64_decode_input();
        break;
    case FUJICMD_BASE64_DECODE_COMPUTE:
        //sio_ack();
        sio_base64_decode_compute();
        break;
    case FUJICMD_BASE64_DECODE_LENGTH:
        //sio_ack();
        sio_base64_decode_length();
        break;
    case FUJICMD_BASE64_DECODE_OUTPUT:
        //sio_ack();
        sio_base64_decode_output();
        break;
    case FUJICMD_HASH_INPUT:
        //sio_late_ack();
        sio_hash_input();
        break;
    case FUJICMD_HASH_COMPUTE:
        //sio_ack();
        sio_hash_compute(true);
        break;
    case FUJICMD_HASH_COMPUTE_NO_CLEAR:
        //sio_ack();
        sio_hash_compute(false);
        break;
    case FUJICMD_HASH_LENGTH:
        //sio_ack();
        sio_hash_length();
        break;
    case FUJICMD_HASH_OUTPUT:
        //sio_ack();
        sio_hash_output();
        break;
    case FUJICMD_HASH_CLEAR:
        //sio_ack();
        sio_hash_clear();
        break;
    case FUJICMD_RANDOM_NUMBER:
        //sio_ack();
        sio_random_number();
        break;
    default:
        //sio_nak();
        transaction_error();
    }
}

#define ADDITIONAL_DETAILS_BYTES 13
size_t sioFuji::setDirEntryDetails(fsdir_entry_t *f, uint8_t *dest, uint8_t maxlen)
{
    // File modified date-time
    struct tm *modtime = localtime(&f->modified_time);
    modtime->tm_mon++;
    modtime->tm_year -= 70;

    dest[0] = modtime->tm_year;
    dest[1] = modtime->tm_mon;
    dest[2] = modtime->tm_mday;
    dest[3] = modtime->tm_hour;
    dest[4] = modtime->tm_min;
    dest[5] = modtime->tm_sec;

    // File size LITTLE ENDIAN for Atari
    uint32_t fsize = f->size;
    dest[6] = fsize & 0xFF;          // Least significant byte
    dest[7] = (fsize >> 8) & 0xFF;
    dest[8] = (fsize >> 16) & 0xFF;
    dest[9] = (fsize >> 24) & 0xFF;  // Most significant byte

    // File flags
#define FF_DIR 0x01
#define FF_TRUNC 0x02

    dest[10] = f->isDir ? FF_DIR : 0;

    maxlen -= ADDITIONAL_DETAILS_BYTES; // Adjust the max return value with the number of additional bytes we're copying
    if (f->isDir)                       // Also subtract a byte for a terminating slash on directories
        maxlen--;
    if (strlen(f->filename) >= maxlen)
        dest[11] |= FF_TRUNC;

    // File type
    dest[12] = MediaType::discover_mediatype(f->filename);

    Debug_printf("Addtl: ");
    for (int i = 0; i < ADDITIONAL_DETAILS_BYTES; i++)
        Debug_printf("%02x ", dest[i]);
    Debug_printf("\n");
    return sizeof(dest);
}

#endif /* BUILD_ATARI */
