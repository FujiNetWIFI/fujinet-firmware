#ifdef BUILD_ADAM

#include "adamFuji.h"
#include "fujiCommandID.h"

#include <cstring>

#include "../../include/debug.h"

#include "serial.h"

#include "fnSystem.h"
#include "fnConfig.h"
#include "fnWiFi.h"
#include "fsFlash.h"
#include "led.h"

#include "utils.h"
#include "string_utils.h"
#include "fuji_endian.h"

#define IMAGE_EXTENSION ".ddp"
#define COPY_SIZE 532

adamFuji platformFuji;
fujiDevice *theFuji = &platformFuji;         // global fuji device object
adamNetwork *theNetwork;  // global network device object (temporary)
adamNetwork *theNetwork2; // another network device
adamPrinter *thePrinter;  // global printer
adamSerial *theSerial;    // global serial

using namespace std;

// sioDisk sioDiskDevs[MAX_HOSTS];
// sioNetwork sioNetDevs[MAX_NETWORK_DEVICES];

// Constructor
adamFuji::adamFuji() : fujiDevice(MAX_DISK_DEVICES, IMAGE_EXTENSION, std::nullopt)
{
    // Helpful for debugging
    for (int i = 0; i < MAX_HOSTS; i++)
        _fnHosts[i].slotid = i;
}

// Status
void adamFuji::adamnet_control_status()
{
    uint8_t r[6] = {0x8F, 0x00, 0x04, 0x00, 0x00, 0x04};
    adamnet_send_buffer(r, 6);
}

// Toggle boot config on/off, aux1=0 is disabled, aux1=1 is enabled
void adamFuji::adamnet_set_boot_config()
{
    boot_config = adamnet_recv();
    adamnet_recv();

    SYSTEM_BUS.start_time = esp_timer_get_time();
    adamnet_response_ack();

    Debug_printf("Boot config is now %d\n", boot_config);

    if (_fnDisks[0].disk_dev.is_config_device)
    {
        _fnDisks[0].disk_dev.unmount();
        _fnDisks[0].disk_dev.is_config_device = false;
        _fnDisks[0].reset();
        Debug_printf("Boot config unmounted slot 0\n");
    }
}

/*
 Write an "app key" to SD (ONLY!) storage.
*/
void adamFuji::adamnet_write_app_key()
{
    uint16_t creator = adamnet_recv_length();
    uint8_t app = adamnet_recv();
    uint8_t key = adamnet_recv();
    uint8_t data[64];
    char appkeyfilename[30];
    FILE *fp;

    snprintf(appkeyfilename, sizeof(appkeyfilename), "/FujiNet/%04hx%02hhx%02hhx.key", creator, app, key);

    adamnet_recv_buffer(data, 64);
    adamnet_recv(); // CK

    Debug_printf("Fuji Cmd: WRITE APPKEY %s\n", appkeyfilename);

    SYSTEM_BUS.start_time = esp_timer_get_time();
    adamnet_response_ack();

    fp = fnSDFAT.file_open(appkeyfilename, "w");

    if (fp == nullptr)
    {
        Debug_printf("Could not open.\n");
        return;
    }

    fwrite(data, sizeof(uint8_t), sizeof(data), fp);
    fclose(fp);
}

// DEBUG TAPE
void adamFuji::debug_tape()
{
}

// This gets called when we're about to shutdown/reboot
void adamFuji::shutdown()
{
    for (int i = 0; i < MAX_DISK_DEVICES; i++)
        _fnDisks[i].disk_dev.unmount();
}

size_t adamFuji::set_additional_direntry_details(fsdir_entry_t *f, uint8_t *dest,
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

//  Make new disk and shove into device slot
void adamFuji::adamnet_new_disk()
{
    uint8_t hs = adamnet_recv();
    uint8_t ds = adamnet_recv();
    uint32_t numBlocks;
    uint8_t *c = (uint8_t *)&numBlocks;
    uint8_t p[256];

    adamnet_recv_buffer(c, sizeof(uint32_t));
    adamnet_recv_buffer(p, 256);

    adamnet_recv(); // CK

    fujiDisk &disk = _fnDisks[ds];
    fujiHost &host = _fnHosts[hs];

    if (new_disk_completed)
    {
        new_disk_completed = false;
        SYSTEM_BUS.start_time = esp_timer_get_time();
        adamnet_response_ack();
        return;
    }

    disk.host_slot = hs;
    disk.access_mode = DISK_ACCESS_MODE_WRITE;
    strlcpy(disk.filename, (const char *)p, 256);

    disk.fileh = host.file_open(disk.filename, disk.filename, sizeof(disk.filename), "w");

    Debug_printf("Creating file %s on host slot %u mounting in disk slot %u numblocks: %lu\n", disk.filename, hs, ds, numBlocks);

    disk.disk_dev.write_blank(disk.fileh, numBlocks);

    fclose(disk.fileh);

    new_disk_completed = true;
}

// Mounts the desired boot disk number
void adamFuji::insert_boot_device(uint8_t d)
{
    const char *config_atr = "/autorun.ddp";
    const char *mount_all_atr = "/mount-and-boot.ddp";
    FILE *fBoot;

    switch (d)
    {
    case 0:
        fBoot = fsFlash.file_open(config_atr);
        _fnDisks[0].disk_dev.mount(fBoot, config_atr, 262144, DISK_ACCESS_MODE_READ, MEDIATYPE_DDP);
        break;
    case 1:

        fBoot = fsFlash.file_open(mount_all_atr);
        _fnDisks[0].disk_dev.mount(fBoot, mount_all_atr, 262144, DISK_ACCESS_MODE_READ, MEDIATYPE_DDP);
        break;
    }

    _fnDisks[0].disk_dev.is_config_device = true;
    _fnDisks[0].disk_dev.device_active = true;
}

void adamFuji::adamnet_enable_device()
{
    unsigned char d = adamnet_recv();

    Debug_printf("FUJI ENABLE DEVICE %02x\n", d);

    adamnet_recv();

    SYSTEM_BUS.start_time = esp_timer_get_time();
    adamnet_response_ack();

    switch (d)
    {
    case 0x02:
        Config.store_printer_enabled(true);
        break;
    case 0x04:
        Config.store_device_slot_enable_1(true);
        break;
    case 0x05:
        Config.store_device_slot_enable_2(true);
        break;
    case 0x06:
        Config.store_device_slot_enable_3(true);
        break;
    case 0x07:
        Config.store_device_slot_enable_4(true);
        break;
    }

    Config.save();

    SYSTEM_BUS.enableDevice(d);
}

void adamFuji::adamnet_disable_device()
{
    unsigned char d = adamnet_recv();

    Debug_printf("FUJI DISABLE DEVICE %02x\n", d);

    adamnet_recv();

    SYSTEM_BUS.start_time = esp_timer_get_time();
    adamnet_response_ack();

    switch (d)
    {
    case 0x02:
        Config.store_printer_enabled(false);
        break;
    case 0x04:
        Config.store_device_slot_enable_1(false);
        break;
    case 0x05:
        Config.store_device_slot_enable_2(false);
        break;
    case 0x06:
        Config.store_device_slot_enable_3(false);
        break;
    case 0x07:
        Config.store_device_slot_enable_4(false);
        break;
    }

    Config.save();

    SYSTEM_BUS.disableDevice(d);
}

// Initializes base settings and adds our devices to the SIO bus
void adamFuji::setup()
{
    // set up Fuji device
    populate_slots_from_config();

    // Disable booting from CONFIG if our settings say to turn it off
    boot_config = Config.get_general_config_enabled();

    // Disable status_wait if our settings say to turn it off
    status_wait_enabled = false;

    SYSTEM_BUS.addDevice(&_fnDisks[0].disk_dev, ADAMNET_DEVICEID_DISK);
    SYSTEM_BUS.addDevice(&_fnDisks[1].disk_dev, ADAMNET_DEVICEID_DISK + 1);
    SYSTEM_BUS.addDevice(&_fnDisks[2].disk_dev, ADAMNET_DEVICEID_DISK + 2);
    SYSTEM_BUS.addDevice(&_fnDisks[3].disk_dev, ADAMNET_DEVICEID_DISK + 3);

    // Read and enable devices
    _fnDisks[0].disk_dev.device_active = Config.get_device_slot_enable_1();
    _fnDisks[1].disk_dev.device_active = Config.get_device_slot_enable_2();
    _fnDisks[2].disk_dev.device_active = Config.get_device_slot_enable_3();
    _fnDisks[3].disk_dev.device_active = Config.get_device_slot_enable_4();

    if (boot_config == true)
    {
        Debug_printf("Config General Boot Mode: %u\n", Config.get_general_boot_mode());
        if (Config.get_general_boot_mode() == 0)
        {
            FILE *f = fsFlash.file_open("/autorun.ddp");
            _fnDisks[0].disk_dev.mount(f, "/autorun.ddp", 262144, DISK_ACCESS_MODE_READ, MEDIATYPE_DDP);
            _fnDisks[0].disk_dev.is_config_device = true;
        }
        else
        {
            FILE *f = fsFlash.file_open("/mount-and-boot.ddp");
            _fnDisks[0].disk_dev.mount(f, "/mount-and-boot.ddp", 262144, DISK_ACCESS_MODE_READ, MEDIATYPE_DDP);
        }
    }
    else
    {
        Debug_printf("Not mounting config disk\n");
    }

    theNetwork = new adamNetwork();
    theNetwork2 = new adamNetwork();
    theSerial = new adamSerial();
    SYSTEM_BUS.addDevice(theNetwork, 0x09);  // temporary.
    SYSTEM_BUS.addDevice(theNetwork2, 0x0A); // temporary
    SYSTEM_BUS.addDevice(theFuji, 0x0F);    // Fuji becomes the gateway device.
}

void adamFuji::adamnet_random_number()
{
    int *p = (int *)&response[0];

    adamnet_recv(); // CK

    SYSTEM_BUS.start_time = esp_timer_get_time();
    adamnet_response_ack();

    response_len = sizeof(int);
    *p = rand();
}

void adamFuji::adamnet_get_time()
{
    Debug_println("FUJI GET TIME");
    adamnet_recv(); // CK

    SYSTEM_BUS.start_time = esp_timer_get_time();
    adamnet_response_ack();

    time_t tt = time(nullptr);

    setenv("TZ", Config.get_general_timezone().c_str(), 1);
    tzset();

    struct tm *now = localtime(&tt);

        /*
     NWD order has changed to match apple format
     Previously:
        response[0] = now->tm_mday;
        response[1] = now->tm_mon;
        response[2] = now->tm_year;
        response[3] = now->tm_hour;
        response[4] = now->tm_min;
        response[5] = now->tm_sec;
    */

        response[0] = (now->tm_year) / 100 + 19;
        response[1] = now->tm_year % 100;
        response[2] = now->tm_mon + 1;
        response[3] = now->tm_mday;
        response[4] = now->tm_hour;
        response[5] = now->tm_min;
        response[6] = now->tm_sec;

        response_len = 7;

    Debug_printf("Sending %02X %02X %02X %02X %02X %02X %02X\n", response[0], response[1], response[2], response[3], response[4], response[5], response[6]);
}

void adamFuji::adamnet_device_enable_status()
{
    uint8_t d = adamnet_recv();
    adamnet_recv(); // CK

    SYSTEM_BUS.start_time = esp_timer_get_time();

    if (SYSTEM_BUS.deviceExists(d))
        adamnet_response_ack();
    else
        adamnet_response_nack();

    response_len = 1;
    response[0] = SYSTEM_BUS.deviceEnabled(d);
}

void adamFuji::adamnet_control_send()
{
    uint16_t s = adamnet_recv_length();
    fujiCommandID_t c = (fujiCommandID_t) adamnet_recv();

    switch (c)
    {
    case FUJICMD_RESET:
        fujicmd_reset();
        break;
    case FUJICMD_GET_SSID:
        fujicmd_net_get_ssid();
        break;
    case FUJICMD_SCAN_NETWORKS:
        fujicmd_net_scan_networks();
        break;
    case FUJICMD_GET_SCAN_RESULT:
        fujicmd_net_scan_result(adamnet_recv());
        break;
    case FUJICMD_SET_SSID:
        {
            SSIDConfig cfg;
            adamnet_recv_buffer((uint8_t *)&cfg, s);
            fujicmd_net_set_ssid_success(cfg.ssid, cfg.password, false);
        }
        break;
    case FUJICMD_GET_WIFISTATUS:
        fujicmd_net_get_wifi_status();
        break;
    case FUJICMD_MOUNT_HOST:
        fujicmd_mount_host_success(adamnet_recv());
        break;
    case FUJICMD_UNMOUNT_HOST:
        fujicmd_unmount_host_success(adamnet_recv());
        break;
    case FUJICMD_MOUNT_IMAGE:
        {
            uint8_t slot = adamnet_recv();
            uint8_t mode = adamnet_recv();
            fujicmd_mount_disk_image_success(slot, (disk_access_flags_t) mode);
        }
        break;
    case FUJICMD_OPEN_DIRECTORY:
        fujicmd_open_directory_success(adamnet_recv());
        break;
    case FUJICMD_READ_DIR_ENTRY:
        {
            uint8_t maxlen = adamnet_recv();
            uint8_t addtl = adamnet_recv();
            fujicmd_read_directory_entry(maxlen, addtl);
        }
        break;
    case FUJICMD_CLOSE_DIRECTORY:
        fujicmd_close_directory();
        break;
    case FUJICMD_READ_HOST_SLOTS:
        fujicmd_read_host_slots();
        break;
    case FUJICMD_WRITE_HOST_SLOTS:
        fujicmd_write_host_slots();
        break;
    case FUJICMD_READ_DEVICE_SLOTS:
        fujicmd_read_device_slots();
        break;
    case FUJICMD_WRITE_DEVICE_SLOTS:
        fujicmd_write_device_slots();
        break;
    case FUJICMD_UNMOUNT_IMAGE:
        fujicmd_unmount_disk_image_success(adamnet_recv());
        break;
    case FUJICMD_GET_ADAPTERCONFIG:
        fujicmd_get_adapter_config();
        break;
    case FUJICMD_GET_ADAPTERCONFIG_EXTENDED:
        fujicmd_get_adapter_config_extended();
        break;
    case FUJICMD_NEW_DISK:
        adamnet_new_disk();
        break;
    case FUJICMD_GET_DIRECTORY_POSITION:
        fujicmd_get_directory_position();
        break;
    case FUJICMD_SET_DIRECTORY_POSITION:
        {
            uint16_t pos = 0;
            adamnet_recv_buffer((uint8_t *)&pos, sizeof(uint16_t));
            fujicmd_set_directory_position(pos);
        }
        break;
    case FUJICMD_SET_DEVICE_FULLPATH:
        {
            uint8_t deviceSlot = adamnet_recv();
            char filename[256];
            transaction_get(filename, s - 2);
            fujicore_set_device_filename_success(deviceSlot, _fnDisks[deviceSlot].host_slot,
                                                 _fnDisks[deviceSlot].access_mode,
                                                 std::string(filename, s - 2));
        }
        break;
    case FUJICMD_GET_DEVICE_FULLPATH:
        fujicmd_get_device_filename(adamnet_recv());
        break;
    case FUJICMD_CONFIG_BOOT:
        adamnet_set_boot_config();
        break;
    case FUJICMD_ENABLE_DEVICE:
        adamnet_enable_device();
        break;
    case FUJICMD_DISABLE_DEVICE:
        adamnet_disable_device();
        break;
    case FUJICMD_MOUNT_ALL:
        fujicmd_mount_all_success();
        break;
    case FUJICMD_SET_BOOT_MODE:
        fujicmd_set_boot_mode(adamnet_recv(), MEDIATYPE_UNKNOWN, &bootdisk);
        break;
    case FUJICMD_WRITE_APPKEY:
        adamnet_write_app_key();
        break;
    case FUJICMD_READ_APPKEY:
        fujicmd_read_app_key();
        break;
    case FUJICMD_RANDOM_NUMBER:
        adamnet_random_number();
        break;
    case FUJICMD_GET_TIME:
        adamnet_get_time();
        break;
    case FUJICMD_DEVICE_ENABLE_STATUS:
        adamnet_device_enable_status();
        break;
    case FUJICMD_COPY_FILE:
        {
            uint8_t source = adamnet_recv();
            uint8_t dest = adamnet_recv();
            char dirpath[256];
            transaction_get(dirpath, sizeof(dirpath));
            fujicmd_copy_file_success(source, dest, dirpath);
        }
        break;
    case FUJICMD_GENERATE_GUID:
        fujicmd_generate_guid();
        break;
    default:
        Debug_printv("Unknown command: %02x\n", c);
        break;
    }
}

void adamFuji::adamnet_control_clr()
{
    adamnet_send(0xBF);
    adamnet_send_length(response_len);
    adamnet_send_buffer(response, response_len);
    adamnet_send(adamnet_checksum(response, response_len));
    adamnet_recv(); // get the ack.
    memset(response, 0, sizeof(response));
    response_len = 0;
}

void adamFuji::adamnet_process(uint8_t b)
{
    unsigned char c = b >> 4;

    switch (c)
    {
    case MN_STATUS:
        adamnet_control_status();
        break;
    case MN_CLR:
        adamnet_control_clr();
        break;
    case MN_RECEIVE:
        adamnet_response_ack();
        break;
    case MN_SEND:
        adamnet_control_send();
        break;
    case MN_READY:
        adamnet_control_ready();
        break;
    }
}

void adamFuji::fujicmd_read_directory_entry(size_t maxlen, uint8_t addtl)
{
    if (response[0])
    {
        // Adam is bonkers and if it already got any data we are going
        // to ignore its request and tell it complete instead
        Debug_printv("No soup for you!");
        transaction_complete();
        return;
    }

    transaction_continue(false);
    Debug_printf("Fuji cmd: READ DIRECTORY ENTRY (max=%hu) (addtl=%02x)\n", maxlen, addtl);

    auto current_entry = fujicore_read_directory_entry(maxlen, addtl);
    if (!current_entry)
    {
        transaction_error();
        return;
    }

    std::string &dirpath = *current_entry;

    // Hack-o-rama to add file type character to beginning of path.
    if (maxlen == 31)
    {
        dirpath.resize(dirpath.size() + 2);
        memmove(&dirpath[2], &dirpath[0], dirpath.size() - 2);
        dirpath[0] = dirpath[1] = 0x20;

        // Check if it's a directory first
        if (dirpath.back() == '/')
        {
            dirpath[0] = 0x83;
            dirpath[1] = 0x84;
        }
        else
        {
            size_t dot_pos = dirpath.rfind('.');
            if (dot_pos != std::string::npos)
            {
                std::string ext = dirpath.substr(dot_pos + 1);
                // Convert to uppercase for comparison
                std::transform(ext.begin(), ext.end(), ext.begin(), ::toupper);

                if (ext == "DDP")
                {
                    dirpath[0] = 0x85;
                    dirpath[1] = 0x86;
                }
                else if (ext == "DSK")
                {
                    dirpath[0] = 0x87;
                    dirpath[1] = 0x88;
                }
                else if (ext == "ROM")
                {
                    dirpath[0] = 0x89;
                    dirpath[1] = 0x8a;
                }
            }
        }
    }

    if (current_entry->size() < maxlen)
        current_entry->resize(maxlen, '\0');

    Debug_printf("%s\n", util_hexdump(current_entry->data(), maxlen).c_str());
    transaction_put(current_entry->data(), maxlen);
}

#if 0
bool adamFuji::fujicmd_mount_disk_image_success(uint8_t deviceSlot,
                                                disk_access_flags_t access_mode)
{
    Debug_println("Fuji cmd: MOUNT IMAGE");

    // Adam needs ACK before we even determine if the disk can be mounted
    transaction_complete();
    return fujicore_mount_disk_image_success(deviceSlot, access_mode);
}

void adamFuji::fujicmd_get_adapter_config()
{
    // Adam needs ACK ASAP
    transaction_complete();

    // also return string versions of the data to save the host some computing
    Debug_printf("Fuji cmd: GET ADAPTER CONFIG\r\n");

    // AdapterConfigExtended contains AdapterConfig so just get Extended
    AdapterConfigExtended cfg = fujicore_get_adapter_config_extended();

    // Only write out the AdapterConfig part
    response_len = sizeof(AdapterConfig);
    memcpy(response, &cfg, response_len);
}

void adamFuji::fujicmd_get_adapter_config_extended()
{
    // Adam needs ACK ASAP
    transaction_complete();

    // also return string versions of the data to save the host some computing
    Debug_printf("Fuji cmd: GET ADAPTER CONFIG EXTENDED\r\n");

    AdapterConfigExtended cfg = fujicore_get_adapter_config_extended();
    response_len = sizeof(cfg);
    memcpy(response, &cfg, response_len);
}
#endif

#endif /* BUILD_ADAM */
