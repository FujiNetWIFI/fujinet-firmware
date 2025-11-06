#ifdef BUILD_RS232
#include "rs232Fuji.h"
#include "network.h"

#include "fnSystem.h"
#include "fnConfig.h"
#include "fsFlash.h"
#include "fnWiFi.h"
#include "utils.h"
#include "compat_string.h"
#include <libgen.h>

#define IMAGE_EXTENSION ".img"

rs232Fuji platformFuji;
fujiDevice *theFuji = &platformFuji;
rs232Network rs232NetDevs[MAX_NETWORK_DEVICES];

// Initializes base settings and adds our devices to the RS232 bus
void rs232Fuji::setup()
{
    // set up Fuji device

    populate_slots_from_config();

    insert_boot_device(Config.get_general_boot_mode(), IMAGE_EXTENSION, MEDIATYPE_UNKNOWN, &bootdisk);

    // Disable booting from CONFIG if our settings say to turn it off
    boot_config = Config.get_general_config_enabled();

    // Disable status_wait if our settings say to turn it off
    status_wait_enabled = Config.get_general_status_wait_enabled();

    // Add our devices to the RS232 bus
    for (int i = 0; i < MAX_DISK_DEVICES; i++)
        SYSTEM_BUS.addDevice(&_fnDisks[i].disk_dev,
                             static_cast<fujiDeviceID_t>(FUJI_DEVICEID_DISK + i));

    for (int i = 0; i < MAX_NETWORK_DEVICES; i++)
        SYSTEM_BUS.addDevice(&rs232NetDevs[i],
                             static_cast<fujiDeviceID_t>(FUJI_DEVICEID_NETWORK + i));
}

// Status
void rs232Fuji::rs232_status()
{
    Debug_println("Fuji cmd: STATUS");

    if (cmdFrame.aux == STATUS_MOUNT_TIME)
    {
        // Return drive slot mount status: 0 if unmounted, otherwise time when mounted
        time_t mount_status[MAX_DISK_DEVICES];
        int idx;

        for (idx = 0; idx < MAX_DISK_DEVICES; idx++)
            mount_status[idx] = _fnDisks[idx].disk_dev.mount_time;

        transaction_put((uint8_t *)mount_status, sizeof(mount_status), false);
    }
    else
    {
        char ret[4] = {0};

        Debug_printf("Status for what? %08lx\n", cmdFrame.aux);
        transaction_put((uint8_t *)ret, sizeof(ret), false);
    }
    return;
}

// Set SSID
void rs232Fuji::rs232_net_set_ssid(bool save) // was aux1
{
    SSIDConfig cfg;
    if (!transaction_get((uint8_t *)&cfg, sizeof(cfg)))
        transaction_error();
    else
        fujicmd_net_set_ssid_success(cfg.ssid, cfg.password, save);
}

// Do RS232 copy
void rs232Fuji::rs232_copy_file()
{
    char csBuf[256];

    if (!transaction_get(csBuf, sizeof(csBuf)))
    {
        transaction_error();
        return;
    }

    fujicmd_copy_file_success(cmdFrame.aux1, cmdFrame.aux2, csBuf);
}

//  Make new disk and shove into device slot
void rs232Fuji::rs232_new_disk()
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
    if (!transaction_get((uint8_t *)&newDisk, sizeof(newDisk)))
    {
        Debug_print("rs232_new_disk Bad checksum\n");
        transaction_error();
        return;
    }
    if (newDisk.deviceSlot >= MAX_DISK_DEVICES || newDisk.hostSlot >= MAX_HOSTS)
    {
        Debug_print("rs232_new_disk Bad disk or host slot parameter\n");
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
        Debug_printf("rs232_new_disk File exists: \"%s\"\n", disk.filename);
        transaction_error();
        return;
    }

    disk.fileh = host.fnfile_open(disk.filename, disk.filename, sizeof(disk.filename), "w");
    if (disk.fileh == nullptr)
    {
        Debug_printf("rs232_new_disk Couldn't open file for writing: \"%s\"\n", disk.filename);
        transaction_error();
        return;
    }

    bool ok = disk.disk_dev.write_blank(disk.fileh, newDisk.sectorSize, newDisk.numSectors);
    fnio::fclose(disk.fileh);

    if (ok == false)
    {
        Debug_print("rs232_new_disk Data write failed\n");
        transaction_error();
        return;
    }

    Debug_print("rs232_new_disk succeeded\n");
    transaction_complete();
}

void rs232Fuji::rs232_open_directory()
{
    char dirpath[256];


    if (!transaction_get(dirpath, sizeof(dirpath))) {
        transaction_error();
        return;
    }

    fujicmd_open_directory_success(cmdFrame.aux1, std::string(dirpath, sizeof(dirpath)));
    return;
}

void rs232Fuji::rs232_test()
{
    uint8_t buf[512];

    Debug_printf("rs232_test()\n");
    memset(buf, 'A', 512);
    transaction_put(buf, 512, false);
}

void rs232Fuji::rs232_process(cmdFrame_t *cmd_ptr)
{
    Debug_println("rs232Fuji::rs232_process() called");

    cmdFrame = *cmd_ptr;
    switch (cmdFrame.comnd)
    {
    case FUJICMD_STATUS:
        rs232_ack();
        rs232_status();
        break;
    case FUJICMD_RESET:
        rs232_ack();
        fujicmd_reset();
        break;
    case FUJICMD_SCAN_NETWORKS:
        rs232_ack();
        fujicmd_net_scan_networks();
        break;
    case FUJICMD_GET_SCAN_RESULT:
        rs232_ack();
        fujicmd_net_scan_result(cmdFrame.aux1);
        break;
    case FUJICMD_SET_SSID:
        rs232_ack();
        rs232_net_set_ssid(cmdFrame.aux1);
        break;
    case FUJICMD_GET_SSID:
        rs232_ack();
        fujicmd_net_get_ssid();
        break;
    case FUJICMD_GET_WIFISTATUS:
        rs232_ack();
        fujicmd_net_get_wifi_status();
        break;
    case FUJICMD_MOUNT_HOST:
        rs232_ack();
        fujicmd_mount_host_success(cmdFrame.aux1);
        break;
    case FUJICMD_MOUNT_IMAGE:
        rs232_ack();
        fujicmd_mount_disk_image_success(cmdFrame.aux1, cmdFrame.aux2);
        break;
    case FUJICMD_OPEN_DIRECTORY:
        rs232_ack();
        rs232_open_directory();
        break;
    case FUJICMD_READ_DIR_ENTRY:
        rs232_ack();
        fujicmd_read_directory_entry(cmdFrame.aux1, cmdFrame.aux2);
        break;
    case FUJICMD_CLOSE_DIRECTORY:
        rs232_ack();
        fujicmd_close_directory();
        break;
    case FUJICMD_GET_DIRECTORY_POSITION:
        rs232_ack();
        fujicmd_get_directory_position();
        break;
    case FUJICMD_SET_DIRECTORY_POSITION:
        rs232_ack();
        fujicmd_set_directory_position(cmdFrame.aux1);
        break;
    case FUJICMD_READ_HOST_SLOTS:
        rs232_ack();
        fujicmd_read_host_slots();
        break;
    case FUJICMD_WRITE_HOST_SLOTS:
        rs232_ack();
        fujicmd_write_host_slots();
        break;
    case FUJICMD_READ_DEVICE_SLOTS:
        rs232_ack();
        fujicmd_read_device_slots(MAX_DISK_DEVICES);
        break;
    case FUJICMD_WRITE_DEVICE_SLOTS:
        rs232_ack();
        fujicmd_write_device_slots(MAX_DISK_DEVICES);
        break;
    case FUJICMD_GET_WIFI_ENABLED:
        rs232_ack();
        fujicmd_net_get_wifi_enabled();
        break;
    case FUJICMD_UNMOUNT_IMAGE:
        rs232_ack();
        fujicmd_unmount_disk_image_success(cmdFrame.aux1);
        break;
    case FUJICMD_GET_ADAPTERCONFIG:
        rs232_ack();
        fujicmd_get_adapter_config();
        break;
    case FUJICMD_GET_ADAPTERCONFIG_EXTENDED:
        rs232_ack();
        fujicmd_get_adapter_config_extended();
        break;
    case FUJICMD_NEW_DISK:
        rs232_ack();
        rs232_new_disk();
        break;
    case FUJICMD_SET_DEVICE_FULLPATH:
        rs232_ack();
        fujicmd_set_device_filename_success(cmdFrame.aux1, cmdFrame.aux2, cmdFrame.aux3);
        break;
    case FUJICMD_SET_HOST_PREFIX:
        rs232_ack();
        fujicmd_set_host_prefix(cmdFrame.aux1);
        break;
    case FUJICMD_GET_HOST_PREFIX:
        rs232_ack();
        fujicmd_get_host_prefix(cmdFrame.aux1);
        break;
    case FUJICMD_WRITE_APPKEY:
        rs232_ack();
        fujicmd_write_app_key(cmdFrame.aux1);
        break;
    case FUJICMD_READ_APPKEY:
        rs232_ack();
        fujicmd_read_app_key();
        break;
    case FUJICMD_OPEN_APPKEY:
        rs232_ack();
        fujicmd_open_app_key();
        break;
    case FUJICMD_CLOSE_APPKEY:
        rs232_ack();
        fujicmd_close_app_key();
        break;
    case FUJICMD_GET_DEVICE_FULLPATH:
        rs232_ack();
        fujicmd_get_device_filename(cmdFrame.aux1);
        break;
    case FUJICMD_CONFIG_BOOT:
        rs232_ack();
        fujicmd_set_boot_config(cmdFrame.aux1);
        break;
    case FUJICMD_COPY_FILE:
        rs232_ack();
        rs232_copy_file();
        break;
    case FUJICMD_MOUNT_ALL:
        rs232_ack();
        fujicmd_mount_all_success();
        break;
    case FUJICMD_SET_BOOT_MODE:
        rs232_ack();
        fujicmd_set_boot_mode(cmdFrame.aux1, IMAGE_EXTENSION, MEDIATYPE_UNKNOWN, &bootdisk);
        break;
#ifdef SYSTEM_BUS_IS_UDP
    case FUJICMD_ENABLE_UDPSTREAM:
        rs232_ack();
        fujicmd_enable_udpstream(cmdFrame.aux1);
        break;
#endif /* SYSTEM_BUS_IS_UDP */
    case FUJICMD_DEVICE_READY:
        Debug_printf("FUJICMD DEVICE TEST\n");
        rs232_ack();
        rs232_test();
        break;
    default:
        rs232_nak();
    }
}

// For some reason the directory entry structure that is sent back
// isn't standardized across platforms.
typedef struct {
    uint8_t year, month, day, hour, minute, second;
    uint32_t file_size;
#define DIR_ENTRY_FLAGS
#ifdef DIR_ENTRY_FLAGS
    uint8_t flags, truncated;
#ifdef DIR_ENTRY_TYPE
    uint8_t file_type;
#endif /* DIR_ENTRY_TYPE */
#endif /* DIR_ENTRY_FLAGS */
}  __attribute__((packed)) DirEntAttrib;

size_t rs232Fuji::setDirEntryDetails(fsdir_entry_t *f, uint8_t *dest, uint8_t maxlen)
{
    DirEntAttrib *attrib = (DirEntAttrib *) dest;


    // File modified date-time
    struct tm *modtime = localtime(&f->modified_time);
    attrib->year = modtime->tm_year - 100;
    attrib->month = modtime->tm_mon + 1;
    attrib->day = modtime->tm_mday;
    attrib->hour = modtime->tm_hour;
    attrib->minute = modtime->tm_min;
    attrib->second = modtime->tm_sec;

    attrib->file_size = htole32(f->size);

#ifdef DIR_ENTRY_FLAGS
    // File flags
#define FF_DIR 0x01
#define FF_TRUNC 0x02

    attrib->flags = f->isDir ? FF_DIR : 0;

    maxlen -= sizeof(*attrib); // Adjust the max return value with the number of additional
                              // bytes we're copying
    if (f->isDir)             // Also subtract a byte for a terminating slash on directories
        maxlen--;
    attrib->truncated = strlen(f->filename) >= maxlen ? FF_TRUNC : 0;

#ifdef DIR_ENTRY_TYPE
    // File type
    attrib->file_type = MediaType::discover_mediatype(f->filename);
#endif /* DIR_ENTRY_TYPE */
#endif /* DIR_ENTRY_FLAGS */

    Debug_printf("Addtl: ");
    for (int i = 0; i < sizeof(*attrib); i++)
        Debug_printf("%02x ", dest[i]);
    Debug_printf("\n");
    return sizeof(*attrib);
}

#endif /* BUILD_RS232 */
