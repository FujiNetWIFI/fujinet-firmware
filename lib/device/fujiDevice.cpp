#include "fujiDevice.h"

#include "fnConfig.h"
#include "fnSystem.h"
#include "fnWiFi.h"
#include "fsFlash.h"
#include "fnFsTNFS.h"

#include <endian.h>

// Constructor
fujiDevice::fujiDevice()
{
    // Helpful for debugging
    for (int i = 0; i < MAX_HOSTS; i++)
        _fnHosts[i].slotid = i;
}

// Public method to update host in specific slot
fujiHost *fujiDevice::set_slot_hostname(int host_slot, char *hostname)
{
    _fnHosts[host_slot].set_hostname(hostname);
    populate_config_from_slots();
    return &_fnHosts[host_slot];
}

// Temporary(?) function while we move from old config storage to new
void fujiDevice::populate_slots_from_config()
{
    for (int i = 0; i < MAX_HOSTS; i++)
    {
        if (Config.get_host_type(i) == fnConfig::host_types::HOSTTYPE_INVALID)
            _fnHosts[i].set_hostname("");
        else
            _fnHosts[i].set_hostname(Config.get_host_name(i).c_str());
    }

    for (int i = 0; i < MAX_DISK_DEVICES; i++)
    {
        _fnDisks[i].reset();

        if (Config.get_mount_host_slot(i) != HOST_SLOT_INVALID)
        {
            if (Config.get_mount_host_slot(i) >= 0
                && Config.get_mount_host_slot(i) <= MAX_HOSTS)
            {
                strlcpy(_fnDisks[i].filename,
                        Config.get_mount_path(i).c_str(), sizeof(fujiDisk::filename));
                _fnDisks[i].host_slot = Config.get_mount_host_slot(i);
                if (Config.get_mount_mode(i) == fnConfig::mount_modes::MOUNTMODE_WRITE)
                    _fnDisks[i].access_mode = DISK_ACCESS_MODE_WRITE;
                else
                    _fnDisks[i].access_mode = DISK_ACCESS_MODE_READ;
            }
        }
    }
}

// Temporary(?) function while we move from old config storage to new
void fujiDevice::populate_config_from_slots()
{
    for (int i = 0; i < MAX_HOSTS; i++)
    {
        fujiHostType htype = _fnHosts[i].get_type();
        const char *hname = _fnHosts[i].get_hostname();

        if (hname[0] == '\0')
        {
            Config.clear_host(i);
        }
        else
        {
            Config.store_host(i, hname,
                              htype == HOSTTYPE_TNFS
                              ? fnConfig::host_types::HOSTTYPE_TNFS
                              : fnConfig::host_types::HOSTTYPE_SD);
        }
    }

    for (int i = 0; i < MAX_DISK_DEVICES; i++)
    {
        if (_fnDisks[i].host_slot >= MAX_HOSTS || _fnDisks[i].filename[0] == '\0')
            Config.clear_mount(i);
        else
            Config.store_mount(i, _fnDisks[i].host_slot, _fnDisks[i].filename,
                               _fnDisks[i].access_mode == DISK_ACCESS_MODE_WRITE
                               ? fnConfig::mount_modes::MOUNTMODE_WRITE
                               : fnConfig::mount_modes::MOUNTMODE_READ);
    }
}

// Mount all - returns true on success and false on error
bool fujiDevice::fujicmd_mount_all()
{
    bool nodisks = true; // Check at the end if no disks are in a slot and disable config

    for (int i = 0; i < MAX_DISK_DEVICES; i++)
    {
        fujiDisk &disk = _fnDisks[i];
        fujiHost &host = _fnHosts[disk.host_slot];
        DEVICE_TYPE *disk_dev = get_disk_dev(i);
        char flag[4] = {'r', 'b', 0, 0};
        if (disk.access_mode == DISK_ACCESS_MODE_WRITE)
            flag[2] = '+';

        if (disk.host_slot != INVALID_HOST_SLOT && strlen(disk.filename) > 0)
        {
            nodisks = false; // We have a disk in a slot

            if (host.mount() == false)
            {
                transaction_error();
                return false;
            }

            Debug_printf("Selecting '%s' from host #%u as %s on D%u:\n", disk.filename, disk.host_slot, flag, i + 1);

            disk.fileh = host.fnfile_open(disk.filename, disk.filename, sizeof(disk.filename), flag);

            if (disk.fileh == nullptr)
            {
                transaction_error();
                return false;
            }

            // We've gotten this far, so make sure our bootable CONFIG disk is disabled
            boot_config = false;
            status_wait_count = 0;

            // We need the file size for loading XEX files and for CASSETTE, so get that too
            disk.disk_size = host.file_size(disk.fileh);

            // And now mount it
            disk.disk_type = disk_dev->mount(disk.fileh, disk.filename, disk.disk_size);
            if (disk.access_mode == DISK_ACCESS_MODE_WRITE)
            {
                disk_dev->readonly = false;
            }
        }
    }

    if (nodisks)
    {
        // No disks in a slot, disable config
        boot_config = false;
    }

    transaction_complete();
    return true;
}

// This gets called when we're about to shutdown/reboot
void fujiDevice::shutdown()
{
    for (int i = 0; i < MAX_DISK_DEVICES; i++)
        _fnDisks[i].disk_dev.unmount();
}

// DEBUG TAPE
void fujiDevice::debug_tape()
{
}

// Disk Image Rotate
/*
  We rotate disks my changing their disk device ID's. That prevents
  us from having to unmount and re-mount devices.
*/
void fujiDevice::fujicmd_image_rotate()
{
    Debug_println("Fuji cmd: IMAGE ROTATE");

    int count = 0;
    // Find the first empty slot
    while (_fnDisks[count].fileh != nullptr)
        count++;

    if (count > 1)
    {
        count--;

        // Save the device ID of the disk in the last slot
        int last_id = get_disk_dev(count)->id();

        for (int n = count; n > 0; n--)
        {
            int swap = get_disk_dev(n - 1)->id();
            Debug_printf("setting slot %d to ID %hx\n", n, swap);
            _bus->changeDeviceId(get_disk_dev(n), swap); // to do!
        }

        // The first slot gets the device ID of the last slot
        _bus->changeDeviceId(get_disk_dev(0), last_id);

#if ENABLE_SPEECH
        // FIXME - make this work

        // Say whatever disk is in D1:
        if (Config.get_general_rotation_sounds())
        {
            for (int i = 0; i <= count; i++)
            {
                if (_fnDisks[i].disk_dev.id() == 0x31)
                {
                    say_swap_label();
                    say_number(i + 1); // because i starts from 0
                }
            }
        }
#endif /* ENABLE_SPEECH */
    }
}

// ============ Validation of inputs ============

bool fujiDevice::validate_host_slot(uint8_t slot, const char *dmsg)
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

bool fujiDevice::validate_device_slot(uint8_t slot, const char *dmsg)
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

// ============ Standard Fuji commands ============

// Reset FujiNet
void fujiDevice::fujicmd_reset()
{
    Debug_println("Fuji cmd: REBOOT");
    transaction_complete();
    fnSystem.reboot();
}

// Get SSID
void fujiDevice::fujicmd_net_get_ssid()
{
    Debug_println("Fuji cmd: GET SSID");

    // Response to FUJICMD_GET_SSID
    struct
    {
        char ssid[MAX_SSID_LEN + 1];
        char password[MAX_WIFI_PASS_LEN];
    } cfg;

    memset(&cfg, 0, sizeof(cfg));

    /*
      We memcpy instead of strcpy because technically the SSID and
      phasephras aren't std::strings and aren't null terminated,
      they're arrays of bytes officially and can contain any byte
      value - including a zero - at any point in the array.  However,
      we're not consistent about how we treat this in the different
      parts of the code.
    */

    std::string s = Config.get_wifi_ssid();
    memcpy(cfg.ssid, s.c_str(),
           s.length() > sizeof(cfg.ssid) ? sizeof(cfg.ssid) : s.length());

    s = Config.get_wifi_passphrase();
    memcpy(cfg.password, s.c_str(),
           s.length() > sizeof(cfg.password) ? sizeof(cfg.password) : s.length());

    transaction_put(&cfg, sizeof(cfg));
    return;
}

// Mount Server
bool fujiDevice::fujicmd_mount_host(unsigned hostSlot)
{
    Debug_println("Fuji cmd: MOUNT HOST");

    // Make sure we weren't given a bad hostSlot
    if (!validate_host_slot(hostSlot, "mount_hosts"))
    {
        transaction_error();
        return false;
    }

    if (!hostMounted[hostSlot] && !_fnHosts[hostSlot].mount())
    {
        transaction_error();
        return false;
    }

    hostMounted[hostSlot] = true;
    transaction_complete();
    return true;
}

void fujiDevice::fujicmd_net_scan_networks()
{
    char ret[4];

    Debug_println("Fuji cmd: SCAN NETWORKS");

    _countScannedSSIDs = fnWiFi.scan_networks();
    ret[0] = _countScannedSSIDs;
    transaction_put(ret, sizeof(ret));
    return;
}

void fujiDevice::fujicmd_net_scan_result(uint8_t index)
{
    bool err = false;
    SSIDInfo detail;

    Debug_println("Fuji cmd: GET SCAN RESULT");

    if (index < _countScannedSSIDs)
        fnWiFi.get_scan_result(index, detail.ssid, &detail.rssi);
    else
    {
        memset(&detail, 0, sizeof(detail));
        err = true;
    }

    transaction_put(&detail, sizeof(detail), err);
}

// Set SSID
bool fujiDevice::fujicmd_net_set_ssid(const char *ssid, const char *password, bool save)
{
    Debug_println("Fuji cmd: SET SSID");

    Config.save();

    Debug_printf("Connecting to net: %s password: %s\n", ssid, password);

    if (fnWiFi.connect(ssid, password) != 0) {
        transaction_error();
        return false;
    }

    if (save)
    {
        Config.store_wifi_ssid(ssid, strlen(ssid) + 1);
        Config.store_wifi_passphrase(password, strlen(password) + 1);
        Config.save();
    }

    transaction_complete();
    return true;
}

// Disk Image Mount
bool fujiDevice::fujicmd_disk_image_mount(uint8_t deviceSlot, uint8_t options)
{
#ifdef BUILD_RS232
    // TAPE or CASSETTE handling: this function can also mount CAS and WAV files
    // to the C: device. Everything stays the same here and the mounting
    // where all the magic happens is done in the rs232Disk::mount() function.
    // This function opens the file, so cassette does not need to open the file.
    // Cassette needs the file pointer and file size.
#endif /* BUILD_RS232 */

    Debug_println("Fuji cmd: MOUNT IMAGE");

    // TODO: Implement FETCH?
    char flag[4] = {'r', 'b', 0, 0};
    if (options == DISK_ACCESS_MODE_WRITE)
        flag[2] = '+';

    // Make sure we weren't given a bad hostSlot
    if (!validate_device_slot(deviceSlot))
    {
        transaction_error();
        return false;
    }

    if (!validate_host_slot(_fnDisks[deviceSlot].host_slot))
    {
        transaction_error();
        return false;
    }

    // A couple of reference variables to make things much easier to read...
    fujiDisk &disk = _fnDisks[deviceSlot];
    fujiHost &host = _fnHosts[disk.host_slot];
    DEVICE_TYPE *disk_dev = get_disk_dev(deviceSlot);

    Debug_printf("Selecting '%s' from host #%u as %s on D%u:\r\n",
                 disk.filename, disk.host_slot, flag, deviceSlot + 1);

    disk.fileh = host.fnfile_open(disk.filename, disk.filename, sizeof(disk.filename), flag);

    if (disk.fileh == nullptr)
    {
        transaction_error();
        return false;
    }

    // We've gotten this far, so make sure our bootable CONFIG disk is disabled
    boot_config = false;
    status_wait_count = 0;

    // We need the file size for loading XEX files and for CASSETTE, so get that too
    disk.disk_size = host.file_size(disk.fileh);

    if (options == DISK_ACCESS_MODE_WRITE)
    {
        disk_dev->readonly = false;
    }

    disk.disk_type = disk_dev->mount(disk.fileh, disk.filename, disk.disk_size);

    transaction_complete();
    return true;
}

// Mounts the desired boot disk number
void fujiDevice::insert_boot_device(uint8_t image_id, std::string extension,
                                    mediatype_t disk_type)
{
    std::string boot_img;
    fnFile *fBoot = nullptr;
    size_t image_size;
    DEVICE_TYPE *disk_dev = get_disk_dev(0);

    switch (image_id)
    {
    case 0:
        boot_img = "/autorun" + extension;
        fBoot = fsFlash.fnfile_open(boot_img.c_str());
        break;
    case 1:
        boot_img = "/mount-and-boot" + extension;
        fBoot = fsFlash.fnfile_open(boot_img.c_str());
        break;
    case 2:
        Debug_printf("Mounting lobby server\n");
        if (fnTNFS.start("tnfs.fujinet.online"))
        {
            Debug_printf("opening lobby.\n");
            boot_img = "/APPLE2/_lobby" + extension;
            fBoot = fnTNFS.fnfile_open(boot_img.c_str());
        }
        break;
    default:
        Debug_printf("Invalid boot mode: %d\n", image_id);
        return;
    }

    if (fBoot == nullptr)
    {
        Debug_printf("Failed to open boot disk image: %s\n", boot_img);
        return;
    }

    image_size = fsFlash.filesize(fBoot);
    disk_dev->mount(fBoot, boot_img.c_str(), image_size, disk_type);
    disk_dev->is_config_device = true;
}

void fujiDevice::fujicmd_close_directory()
{
    Debug_println("Fuji cmd: CLOSE DIRECTORY");

    if (_current_open_directory_slot != -1)
        _fnHosts[_current_open_directory_slot].dir_close();

    _current_open_directory_slot = -1;
    transaction_complete();
}

bool fujiDevice::fujicmd_copy_file(uint8_t sourceSlot, uint8_t destSlot, std::string copySpec)
{
    std::string sourcePath;
    std::string destPath;
    fnFile *sourceFile;
    fnFile *destFile;
    char *dataBuf;

    Debug_printf("copySpec: %s\n", copySpec.c_str());

    // Check for malformed copyspec.
    if (copySpec.empty() || copySpec.find_first_of("|") == std::string::npos)
    {
        transaction_error();
        return false;
    }

    if (sourceSlot < 1 || sourceSlot > MAX_DISK_DEVICES
        || destSlot < 1 || destSlot > MAX_DISK_DEVICES)
    {
        transaction_error();
        return false;
    }

    sourceSlot--;
    destSlot--;

    // Chop up copyspec.
    sourcePath = copySpec.substr(0, copySpec.find_first_of("|"));
    destPath = copySpec.substr(copySpec.find_first_of("|") + 1);

    // At this point, if last part of dest path is / then copy filename from source.
    if (destPath.back() == '/')
    {
        Debug_printf("append source file\n");
        std::string sourceFilename = sourcePath.substr(sourcePath.find_last_of("/") + 1);
        destPath += sourceFilename;
    }

    // Mount hosts, if needed.
    _fnHosts[sourceSlot].mount();
    _fnHosts[destSlot].mount();

    // Open files...
    sourceFile = _fnHosts[sourceSlot].fnfile_open(
        sourcePath.c_str(), (char *)sourcePath.c_str(), sourcePath.size() + 1, "rb");
    if (sourceFile == nullptr)
    {
        transaction_error();
        return false;
    }

    destFile = _fnHosts[destSlot].fnfile_open(destPath.c_str(), (char *)destPath.c_str(),
                                              destPath.size() + 1, "wb");
    if (destFile == nullptr)
    {
        transaction_error();
        fnio::fclose(sourceFile);
        return false;
    }

    dataBuf = (char *)malloc(532);
    if (dataBuf == nullptr)
    {
        transaction_error();
        fnio::fclose(sourceFile);
        return false;
    }

    size_t count = 0;
    do
    {
        count = fnio::fread(dataBuf, 1, 532, sourceFile);
        fnio::fwrite(dataBuf, 1, count, destFile);
    } while (count > 0);

    transaction_complete();

    // copyEnd:
    fnio::fclose(sourceFile);
    fnio::fclose(destFile);
    free(dataBuf);
    return true;
}

void fujiDevice::fujicmd_disk_image_umount(uint8_t deviceSlot)
{
    DEVICE_TYPE *disk_dev;

    Debug_printf("Fuji cmd: UNMOUNT IMAGE 0x%02X\n", deviceSlot);

    // FIXME - handle tape?
    if (deviceSlot >= MAX_DISK_DEVICES)
    {
        transaction_error();
        return;
    }

    disk_dev = get_disk_dev(deviceSlot);
#ifndef BUILD_RS232
    if (disk_dev->device_active)
        disk_dev->switched = true;
#else /* BUILD_RS232 */
#warning "Why doesn't RS232 have disk_dev->switched?"
#endif /* BUILD_RS232 */
    disk_dev->unmount();
    _fnDisks[deviceSlot].reset();

    transaction_complete();
}

void fujiDevice::fujicmd_get_adapter_config()
{
    AdapterConfig cfg;

    Debug_println("Fuji cmd: GET ADAPTER CONFIG");

    memset(&cfg, 0, sizeof(cfg));
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

    transaction_put(&cfg, sizeof(cfg));
}

// Get a 256 byte filename from device slot
void fujiDevice::fujicmd_get_device_filename(uint8_t slot)
{
    char *buf = nullptr;
    size_t buflen = 0;
    bool err = false;

    if (slot < MAX_DISK_DEVICES)
    {
        buf = _fnDisks[slot].filename;
        buflen = MAX_FILENAME_LEN;
    }
    else
        err = true;

    transaction_put(buf, buflen, err);
}

void fujiDevice::fujicmd_get_directory_position()
{
    Debug_println("Fuji cmd: GET DIRECTORY POSITION");

    // Make sure we have a current open directory
    if (_current_open_directory_slot == -1)
    {
        Debug_print("No currently open directory\n");
        transaction_error();
        return;
    }

    uint16_t pos = _fnHosts[_current_open_directory_slot].dir_tell();
    if (pos == FNFS_INVALID_DIRPOS)
    {
        transaction_error();
        return;
    }

    pos = htole16(pos);
    transaction_put((uint8_t *)&pos, sizeof(pos));
}

// Retrieve host path prefix
void fujiDevice::fujicmd_get_host_prefix(uint8_t hostSlot)
{
    char prefix[MAX_HOST_PREFIX_LEN];

    Debug_printf("Fuji cmd: GET HOST PREFIX %uh\n", hostSlot);

    if (!validate_host_slot(hostSlot))
    {
        transaction_error();
        return;
    }

    _fnHosts[hostSlot].get_prefix(prefix, sizeof(prefix));
    transaction_put((uint8_t *)prefix, sizeof(prefix));
}

void fujiDevice::fujicmd_net_get_wifi_status()
{
    Debug_println("Fuji cmd: GET WIFI STATUS");
    // WL_CONNECTED = 3, WL_DISCONNECTED = 6
    uint8_t wifiStatus = fnWiFi.connected() ? 3 : 6;
    transaction_put(&wifiStatus, sizeof(wifiStatus));
}

void fujiDevice::fujicmd_read_host_slots()
{
    Debug_println("Fuji cmd: READ HOST SLOTS");

    char hostSlots[MAX_HOSTS][MAX_HOSTNAME_LEN];
    memset(hostSlots, 0, sizeof(hostSlots));

    for (int i = 0; i < MAX_HOSTS; i++)
        strlcpy(hostSlots[i], _fnHosts[i].get_hostname(), MAX_HOSTNAME_LEN);

    transaction_put((uint8_t *)&hostSlots, sizeof(hostSlots));
}

// Read and save host slot data from computer
void fujiDevice::fujicmd_write_host_slots()
{
    Debug_println("Fuji cmd: WRITE HOST SLOTS");

    char hostSlots[MAX_HOSTS][MAX_HOSTNAME_LEN];
    if (!transaction_get((uint8_t *)&hostSlots, sizeof(hostSlots)))
        transaction_error();

    for (int i = 0; i < MAX_HOSTS; i++)
    {
        hostMounted[i] = false;
        _fnHosts[i].set_hostname(hostSlots[i]);
    }
    populate_config_from_slots();
    Config.save();
    transaction_complete();
}

// Toggle boot config on/off
void fujiDevice::fujicmd_set_boot_config(bool enable)
{
    if (!enable)
    {
        fujiDisk &disk = _fnDisks[0];
        if (disk.host_slot == INVALID_HOST_SLOT)
        {
            get_disk_dev(0)->unmount();
            _fnDisks[0].reset();
        }
    }
    transaction_complete();
}

// Set boot mode
void fujiDevice::fujicmd_set_boot_mode(uint8_t bootMode, std::string extension,
                                       mediatype_t disk_type)
{
    insert_boot_device(bootMode, extension, disk_type);
    boot_config = true;
    transaction_complete();
}

void fujiDevice::fujicmd_set_directory_position(uint16_t pos)
{
    Debug_println("Fuji cmd: SET DIRECTORY POSITION");

    // Make sure we have a current open directory
    if (_current_open_directory_slot == -1)
    {
        Debug_print("No currently open directory\n");
        transaction_error();
        return;
    }

    bool success = _fnHosts[_current_open_directory_slot].dir_seek(pos);
    if (success == false)
    {
        transaction_error();
        return;
    }
    transaction_complete();
}

// Store host path prefix
void fujiDevice::fujicmd_set_host_prefix(uint8_t hostSlot, const char *prefix)
{
    Debug_printf("Fuji cmd: SET HOST PREFIX %uh \"%s\"\n", hostSlot, prefix);

    if (!validate_host_slot(hostSlot))
    {
        transaction_error();
        return;
    }

    _fnHosts[hostSlot].set_prefix(prefix);
    transaction_complete();
}

// UnMount Server
void fujiDevice::fujicmd_unmount_host(uint8_t hostSlot)
{
    Debug_printf("\r\nFuji cmd: MOUNT HOST no. %d", hostSlot);

    if ((hostSlot < MAX_HOST_SLOTS) && (hostMounted[hostSlot] == false))
    {
        _fnHosts[hostSlot].umount();
        hostMounted[hostSlot] = true;
    }
}

// Send device slot data to computer
void fujiDevice::fujicmd_read_device_slots(uint8_t numDevices)
{
    Debug_println("Fuji cmd: READ DEVICE SLOTS");

    disk_slot diskSlots[MAX_DISK_DEVICES];
    memset(&diskSlots, 0, sizeof(diskSlots));

    // Load the data from our current device array
    for (int i = 0; i < numDevices; i++)
    {
        diskSlots[i].mode = _fnDisks[i].access_mode;
        diskSlots[i].hostSlot = _fnDisks[i].host_slot;
        strlcpy(diskSlots[i].filename, _fnDisks[i].filename, MAX_DISPLAY_FILENAME_LEN);

        DEVICE_TYPE *disk_dev = get_disk_dev(i);
        if (disk_dev->device_active && !disk_dev->is_config_device)
            diskSlots[i].mode |= DISK_ACCESS_MODE_MOUNTED;
    }

    transaction_put(&diskSlots, sizeof(disk_slot) * numDevices);
}

// Read and save disk slot data from computer
void fujiDevice::fujicmd_write_device_slots(uint8_t numDevices)
{
    Debug_println("Fuji cmd: WRITE DEVICE SLOTS");

    disk_slot diskSlots[MAX_DISK_DEVICES];

    if (!transaction_get(&diskSlots, sizeof(disk_slot) * numDevices))
    {
        transaction_error();
        return;
    }

    // Load the data into our current device array
    for (int i = 0; i < numDevices; i++)
        _fnDisks[i].reset(diskSlots[i].filename, diskSlots[i].hostSlot, diskSlots[i].mode);

    // Save the data to disk
    populate_config_from_slots();
    Config.save();
    transaction_complete();
}
