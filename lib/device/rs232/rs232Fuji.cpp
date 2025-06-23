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

// Check if Wifi is enabled
void rs232Fuji::rs232_net_get_wifi_enabled()
{
    uint8_t e = Config.get_wifi_enabled() ? 1 : 0;
    Debug_printf("Fuji cmd: GET WIFI ENABLED: %d\n", e);
    transaction_put(&e, sizeof(e), false);
}

// Do RS232 copy
void rs232Fuji::rs232_copy_file()
{
    uint8_t csBuf[256];

    if (!transaction_get(csBuf, sizeof(csBuf)))
    {
        transaction_error();
        return;
    }

    fujicmd_copy_file(cmdFrame.aux1, cmdFrame.aux2, (char *) csBuf);
}

char *_generate_appkey_filename(appkey *info)
{
    static char filenamebuf[30];

    snprintf(filenamebuf, sizeof(filenamebuf), "/FujiNet/%04hx%02hhx%02hhx.key", info->creator,
             info->app, info->key);
    return filenamebuf;
}

/*
 Opens an "app key".  This just sets the needed app key parameters (creator, app, key, mode)
 for the subsequent expected read/write command. We could've added this information as part
 of the payload in a WRITE_APPKEY command, but there was no way to do this for READ_APPKEY.
 Requiring a separate OPEN command makes both the read and write commands behave similarly
 and leaves the possibity for a more robust/general file read/write function later.
*/
void rs232Fuji::rs232_open_app_key()
{
    Debug_print("Fuji cmd: OPEN APPKEY\n");

    // The data expected for this command
    if (!transaction_get((uint8_t *)&_current_appkey, sizeof(_current_appkey)))
    {
        transaction_error();
        return;
    }

    // We're only supporting writing to SD, so return an error if there's no SD mounted
    if (fnSDFAT.running() == false)
    {
        Debug_println("No SD mounted - returning error");
        transaction_error();
        return;
    }

    // Basic check for valid data
    if (_current_appkey.creator == 0 || _current_appkey.mode == APPKEYMODE_INVALID)
    {
        Debug_println("Invalid app key data");
        transaction_error();
        return;
    }

    Debug_printf("App key creator = 0x%04hx, app = 0x%02hhx, key = 0x%02hhx, mode = %hhu, "
                 "filename = \"%s\"\n",
                 _current_appkey.creator, _current_appkey.app, _current_appkey.key,
                 _current_appkey.mode, _generate_appkey_filename(&_current_appkey));

    transaction_complete();
}

/*
  The app key close operation is a placeholder in case we want to provide more robust file
  read/write operations. Currently, the file is closed immediately after the read or write
  operation.
*/
void rs232Fuji::rs232_close_app_key()
{
    Debug_print("Fuji cmd: CLOSE APPKEY\n");
    _current_appkey.creator = 0;
    _current_appkey.mode = APPKEYMODE_INVALID;
    transaction_complete();
}

/*
 Write an "app key" to SD (ONLY!) storage.
*/
void rs232Fuji::rs232_write_app_key()
{
    uint16_t keylen = cmdFrame.aux12;

    Debug_printf("Fuji cmd: WRITE APPKEY (keylen = %hu)\n", keylen);

    // Data for  FUJICMD_WRITE_APPKEY
    uint8_t value[MAX_APPKEY_LEN];

    if (!transaction_get((uint8_t *)value, sizeof(value)))
    {
        transaction_error();
        return;
    }

    // Make sure we have valid app key information
    if (_current_appkey.creator == 0 || _current_appkey.mode != APPKEYMODE_WRITE)
    {
        Debug_println("Invalid app key metadata - aborting");
        transaction_error();
        return;
    }

    // Make sure we have an SD card mounted
    if (fnSDFAT.running() == false)
    {
        Debug_println("No SD mounted - can't write app key");
        transaction_error();
        return;
    }

    char *filename = _generate_appkey_filename(&_current_appkey);

    // Reset the app key data so we require calling APPKEY OPEN before another attempt
    _current_appkey.creator = 0;
    _current_appkey.mode = APPKEYMODE_INVALID;

    Debug_printf("Writing appkey to \"%s\"\n", filename);

    // Make sure we have a "/FujiNet" directory, since that's where we're putting these files
    fnSDFAT.create_path("/FujiNet");

    FILE *fOut = fnSDFAT.file_open(filename, "w");
    if (fOut == nullptr)
    {
        Debug_printf("Failed to open/create output file: errno=%d\n", errno);
        transaction_error();
        return;
    }
    size_t count = fwrite(value, 1, keylen, fOut);
    int e = errno;

    fclose(fOut);

    if (count != keylen)
    {
        Debug_printf("Only wrote %u bytes of expected %hu, errno=%d\n", count, keylen, e);
        transaction_error();
    }

    transaction_complete();
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

void rs232Fuji::rs232_open_directory()
{
    Debug_println("Fuji cmd: OPEN DIRECTORY");

    char dirpath[256];
    uint8_t hostSlot = cmdFrame.aux1;
    if (!transaction_get((uint8_t *)&dirpath, sizeof(dirpath)))
    {
        transaction_error();
        return;
    }
    if (!validate_host_slot(hostSlot))
    {
        transaction_error();
        return;
    }

    // If we already have a directory open, close it first
    if (_current_open_directory_slot != -1)
    {
        Debug_print("Directory was already open - closing it first\n");
        _fnHosts[_current_open_directory_slot].dir_close();
        _current_open_directory_slot = -1;
    }

    // See if there's a search pattern after the directory path
    const char *pattern = nullptr;
    int pathlen = strnlen(dirpath, sizeof(dirpath));
    if (pathlen < sizeof(dirpath) - 3) // Allow for two NULLs and a 1-char pattern
    {
        pattern = dirpath + pathlen + 1;
        int patternlen = strnlen(pattern, sizeof(dirpath) - pathlen - 1);
        if (patternlen < 1)
            pattern = nullptr;
    }

    // Remove trailing slash
    if (pathlen > 1 && dirpath[pathlen - 1] == '/')
        dirpath[pathlen - 1] = '\0';

    Debug_printf("Opening directory: \"%s\", pattern: \"%s\"\n", dirpath,
                 pattern ? pattern : "");

    if (_fnHosts[hostSlot].dir_open(dirpath, pattern, 0))
    {
        _current_open_directory_slot = hostSlot;
        transaction_complete();
    }
    else
        transaction_error();
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

// Write a 256 byte filename to the device slot
void rs232Fuji::rs232_set_device_filename()
{
    char tmp[MAX_FILENAME_LEN];

    // AUX1 is the desired device slot
    uint8_t slot = cmdFrame.aux1;
    // AUX2 contains the host slot and the mount mode (READ/WRITE)
    uint8_t host = cmdFrame.aux2 >> 4;
    uint8_t mode = cmdFrame.aux2 & 0x0F;

    if (!transaction_get((uint8_t *)tmp, MAX_FILENAME_LEN))
    {
        transaction_error();
        return;
    }

    Debug_printf("Fuji cmd: SET DEVICE SLOT 0x%02X/%02X/%02X FILENAME: %s\n", slot, host, mode,
                 tmp);

    // Handle DISK slots
    if (slot < MAX_DISK_DEVICES)
    {
        memcpy(_fnDisks[cmdFrame.aux1].filename, tmp, MAX_FILENAME_LEN);

        // If the filename is empty, mark this as an invalid host, so that mounting will ignore
        // it too
        if (strlen(_fnDisks[cmdFrame.aux1].filename) == 0)
        {
            _fnDisks[cmdFrame.aux1].host_slot = INVALID_HOST_SLOT;
        }
        else
        {
            _fnDisks[cmdFrame.aux1].host_slot = host;
        }

        _fnDisks[cmdFrame.aux1].access_mode = mode;
        populate_config_from_slots();
    }
    // Handle TAPE slots
    // else if (slot == BASE_TAPE_SLOT) // TODO? currently do not use this option for CAS image
    // filenames
    // {
    //     // Just save the filename until we need it mount the tape
    //     // TODO: allow read and write options
    //     Config.store_mount(0, host, tmp, fnConfig::mount_mode_t::MOUNTMODE_READ,
    //     fnConfig::MOUNTTYPE_TAPE);
    // }
    // Bad slot
    else
    {
        Debug_println("BAD DEVICE SLOT");
        transaction_error();
        return;
    }

    Config.save();
    transaction_complete();
}

// Set an external clock rate in kHz defined by aux1/aux2, aux2 in steps of 2kHz.
void rs232Fuji::rs232_set_rs232_external_clock()
{
    unsigned short speed = rs232_get_aux16_lo();
    int baudRate = speed * 1000;

    Debug_printf("rs232Fuji::rs232_set_external_clock(%u)\n", baudRate);

    if (speed == 0)
    {
        RS232.setUltraHigh(false, 0);
    }
    else
    {
        RS232.setUltraHigh(true, baudRate);
    }

    transaction_complete();
}

// Set UDP Stream HOST & PORT and start it
void rs232Fuji::rs232_enable_udpstream()
{
    char host[64];

    if (!transaction_get((uint8_t *)&host, sizeof(host)))
        transaction_error();
    else
    {
        int port = (cmdFrame.aux1 << 8) | cmdFrame.aux2;

        Debug_printf("Fuji cmd ENABLE UDPSTREAM: HOST:%s PORT: %d\n", host, port);

        // Save the host and port
        Config.store_udpstream_host(host);
        Config.store_udpstream_port(port);
        Config.save();

        transaction_complete();

        // Start the UDP Stream
        RS232.setUDPHost(host, port);
    }
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
