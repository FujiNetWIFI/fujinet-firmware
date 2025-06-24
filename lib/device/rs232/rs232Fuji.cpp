#ifdef BUILD_RS232
#include "rs232Fuji.h"
#include "network.h"

#include "fnSystem.h"
#include "fnConfig.h"
#include "fsFlash.h"
#include "fnWiFi.h"
#include "utils.h"

#define IMAGE_EXTENSION ".img"

rs232Fuji platformFuji;
fujiDevice *theFuji = &platformFuji;
rs232Network rs232NetDevs[MAX_NETWORK_DEVICES];

// Initializes base settings and adds our devices to the RS232 bus
void rs232Fuji::setup(systemBus *sysbus)
{
    // set up Fuji device
    _bus = sysbus;

    populate_slots_from_config();

    insert_boot_device(Config.get_general_boot_mode(), IMAGE_EXTENSION, MEDIATYPE_UNKNOWN);

    // Disable booting from CONFIG if our settings say to turn it off
    boot_config = Config.get_general_config_enabled();

    // Disable status_wait if our settings say to turn it off
    status_wait_enabled = Config.get_general_status_wait_enabled();

    // Add our devices to the RS232 bus
    for (int i = 0; i < MAX_DISK_DEVICES; i++)
        _bus->addDevice(&_fnDisks[i].disk_dev, RS232_DEVICEID_DISK + i);

    for (int i = 0; i < MAX_NETWORK_DEVICES; i++)
        _bus->addDevice(&rs232NetDevs[i], RS232_DEVICEID_FN_NETWORK + i);
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
void rs232Fuji::rs232_net_set_ssid()
{
    SSIDConfig cfg;
    if (!transaction_get((uint8_t *)&cfg, sizeof(cfg)))
        transaction_error();
    else
        fujicmd_net_set_ssid(cfg.ssid, cfg.password, cmdFrame.aux1);
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

    fujicmd_copy_file(cmdFrame.aux1, cmdFrame.aux2, csBuf);
}

/*
 Read an "app key" from SD (ONLY!) storage
*/
void rs232Fuji::rs232_read_app_key()
{

    Debug_println("Fuji cmd: READ APPKEY");

    // Make sure we have an SD card mounted
    if (fnSDFAT.running() == false)
    {
        Debug_println("No SD mounted - can't read app key");
        transaction_error();
        return;
    }

    // Make sure we have valid app key information
    if (_current_appkey.creator == 0 || _current_appkey.mode != APPKEYMODE_READ)
    {
        Debug_println("Invalid app key metadata - aborting");
        transaction_error();
        return;
    }

    char *filename = _generate_appkey_filename(&_current_appkey);

    Debug_printf("Reading appkey from \"%s\"\n", filename);

    FILE *fIn = fnSDFAT.file_open(filename, "r");
    if (fIn == nullptr)
    {
        Debug_printf("Failed to open input file: errno=%d\n", errno);
        transaction_error();
        return;
    }

    struct
    {
        uint16_t size;
        uint8_t value[MAX_APPKEY_LEN];
    } __attribute__((packed)) response;
    memset(&response, 0, sizeof(response));

    size_t count = fread(response.value, 1, sizeof(response.value), fIn);

    fclose(fIn);
    Debug_printf("Read %d bytes from input file\n", count);

    response.size = count;

    transaction_put((uint8_t *)&response, sizeof(response), false);
}

void _set_additional_direntry_details(fsdir_entry_t *f, uint8_t *dest, uint8_t maxlen)
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

    // File size
    uint16_t fsize = f->size;
    dest[6] = LOBYTE_FROM_UINT16(fsize);
    dest[7] = HIBYTE_FROM_UINT16(fsize);

    // File flags
#define FF_DIR   0x01
#define FF_TRUNC 0x02

    dest[8] = f->isDir ? FF_DIR : 0;

    maxlen -=
        10; // Adjust the max return value with the number of additional bytes we're copying
    if (f->isDir) // Also subtract a byte for a terminating slash on directories
        maxlen--;
    if (strlen(f->filename) >= maxlen)
        dest[8] |= FF_TRUNC;

    // File type
    dest[9] = MediaType::discover_disktype(f->filename);
}

void rs232Fuji::rs232_read_directory_entry()
{
    uint8_t maxlen = cmdFrame.aux1;
    Debug_printf("Fuji cmd: READ DIRECTORY ENTRY (max=%hu)\n", maxlen);

    // Make sure we have a current open directory
    if (_current_open_directory_slot == -1)
    {
        Debug_print("No currently open directory\n");
        transaction_error();
        return;
    }

    char current_entry[256];

    fsdir_entry_t *f = _fnHosts[_current_open_directory_slot].dir_nextfile();

    if (f == nullptr)
    {
        Debug_println("Reached end of of directory");
        current_entry[0] = 0x7F;
        current_entry[1] = 0x7F;
    }
    else
    {
        Debug_printf("::read_direntry \"%s\"\n", f->filename);

        int bufsize = sizeof(current_entry);
        char *filenamedest = current_entry;

#define ADDITIONAL_DETAILS_BYTES 10
        // If 0x80 is set on AUX2, send back additional information
        if (cmdFrame.aux2 & 0x80)
        {
            _set_additional_direntry_details(f, (uint8_t *)current_entry, maxlen);
            // Adjust remaining size of buffer and file path destination
            bufsize = sizeof(current_entry) - ADDITIONAL_DETAILS_BYTES;
            filenamedest = current_entry + ADDITIONAL_DETAILS_BYTES;
        }
        else
        {
            bufsize = maxlen;
        }

        // int filelen = strlcpy(filenamedest, f->filename, bufsize);
        int filelen = util_ellipsize(f->filename, filenamedest, bufsize);

        // Add a slash at the end of directory entries
        if (f->isDir && filelen < (bufsize - 2))
        {
            current_entry[filelen] = '/';
            current_entry[filelen + 1] = '\0';
        }
    }

    transaction_put((uint8_t *)current_entry, maxlen, false);
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

// Store host path prefix
void rs232Fuji::rs232_set_host_prefix()
{
    char prefix[MAX_HOST_PREFIX_LEN];
    uint8_t hostSlot = cmdFrame.aux1;

    if (!transaction_get(prefix, sizeof(prefix)))
    {
        transaction_error();
        return;
    }

    fujicmd_set_host_prefix(hostSlot, prefix);
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
        rs232_net_set_ssid();
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
        fujicmd_mount_host(cmdFrame.aux1);
        break;
    case FUJICMD_MOUNT_IMAGE:
        rs232_ack();
        fujicmd_disk_image_mount(cmdFrame.aux1, cmdFrame.aux2);
        break;
    case FUJICMD_OPEN_DIRECTORY:
        rs232_ack();
        rs232_open_directory();
        break;
    case FUJICMD_READ_DIR_ENTRY:
        rs232_ack();
        rs232_read_directory_entry();
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
        fujicmd_set_directory_position(rs232_get_aux16_lo());
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
        rs232_net_get_wifi_enabled();
        break;
    case FUJICMD_UNMOUNT_IMAGE:
        rs232_ack();
        fujicmd_disk_image_umount(cmdFrame.aux1);
        break;
    case FUJICMD_GET_ADAPTERCONFIG:
        rs232_ack();
        fujicmd_get_adapter_config();
        break;
    case FUJICMD_NEW_DISK:
        rs232_ack();
        rs232_new_disk();
        break;
    case FUJICMD_SET_DEVICE_FULLPATH:
        rs232_ack();
        rs232_set_device_filename();
        break;
    case FUJICMD_SET_HOST_PREFIX:
        rs232_ack();
        rs232_set_host_prefix();
        break;
    case FUJICMD_GET_HOST_PREFIX:
        rs232_ack();
        fujicmd_get_host_prefix(cmdFrame.aux1);
        break;
    case FUJICMD_WRITE_APPKEY:
        rs232_ack();
        rs232_write_app_key();
        break;
    case FUJICMD_READ_APPKEY:
        rs232_ack();
        rs232_read_app_key();
        break;
    case FUJICMD_OPEN_APPKEY:
        rs232_ack();
        rs232_open_app_key();
        break;
    case FUJICMD_CLOSE_APPKEY:
        rs232_ack();
        rs232_close_app_key();
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
        fujicmd_mount_all();
        break;
    case FUJICMD_SET_BOOT_MODE:
        rs232_ack();
        fujicmd_set_boot_mode(cmdFrame.aux1, IMAGE_EXTENSION, MEDIATYPE_UNKNOWN);
        break;
    case FUJICMD_ENABLE_UDPSTREAM:
        rs232_ack();
        rs232_enable_udpstream();
        break;
    case FUJICMD_DEVICE_READY:
        Debug_printf("FUJICMD DEVICE TEST\n");
        rs232_ack();
        rs232_test();
        break;
    default:
        rs232_nak();
    }
}

#endif /* BUILD_RS232 */
