/* =============================================================================
 * IMPORTANT: PLATFORM ABSTRACTION BOUNDARY
 * =============================================================================
 * This file implements logic common to all FujiNet platforms.
 * If you find yourself adding `#if` conditionals to this file
 * then **YOU ARE DOING IT WRONG**.
 *
 * Platform-specific differences belong in platform subclasses
 * (e.g. `rs232Fuji`, `iwmFuji`), not here.
 *
 * HOW TO DO PLATFORM-SPECIFIC VARIANTS CORRECTLY:
 *   - Subclasses should override fujicmd_ methods from the base class.
 *   - Prefer calling the base method first, then adding subclass behavior.
 *     This keeps shared logic centralized and minimizes duplication.
 *
 * The goal of this structure is to keep the base class free of
 * platform-specific hacks and `#ifdef` clutter.
 *
 * =============================================================================
 */

#include "fujiDevice.h"

#include "fnConfig.h"
#include "fnSystem.h"
#include "fnWiFi.h"
#include "fsFlash.h"
#include "fnFsTNFS.h"

#include "utils.h"
#include "directoryPageGroup.h"
#include "compat_string.h"

#include "fuji_endian.h"
#ifndef ESP_PLATFORM // why ESP does not like it? it throws a linker error undefined reference to 'basename'
#include <libgen.h>
#endif /* ESP_PLATFORM */

// File flags
enum DET_file_flags_t {
    DET_FF_DIR   = 0x01,
    DET_FF_TRUNC = 0x02,
};

#define DIR_BLOCK_SIZE 256

// Constructor
fujiDevice::fujiDevice(unsigned int numDisk) : totalDiskDevices(numDisk)
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

    for (int i = 0; i < totalDiskDevices; i++)
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

    for (int i = 0; i < totalDiskDevices; i++)
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
bool fujiDevice::fujicore_mount_all_success()
{
    bool nodisks = true; // Check at the end if no disks are in a slot and disable config

    for (int i = 0; i < totalDiskDevices; i++)
    {
        fujiDisk &disk = _fnDisks[i];
        fujiHost &host = _fnHosts[disk.host_slot];
        char flag[4] = {'r', 'b', 0, 0};
        if (disk.access_mode == DISK_ACCESS_MODE_WRITE)
            flag[2] = '+';

        if (disk.host_slot != INVALID_HOST_SLOT && strlen(disk.filename) > 0)
        {
            nodisks = false; // We have a disk in a slot

            if (host.mount() == false)
            {
                return false;
            }

            Debug_printf("Selecting '%s' from host #%u as %s on D%u:\n", disk.filename, disk.host_slot, flag, i + 1);
            if (!fujicore_mount_disk_image_success(i, disk.access_mode))
            {
                return false;
            }
        }
    }

    if (nodisks)
    {
        // No disks in a slot, disable config
        boot_config = false;
    }

    return true;
}

// Mount all - returns true on success and false on error
bool fujiDevice::fujicmd_mount_all_success()
{
    transaction_continue(false);
    if (!fujicore_mount_all_success()) {
        transaction_error();
        return false;
    }

    transaction_complete();
    return true;
}

// This gets called when we're about to shutdown/reboot
void fujiDevice::shutdown()
{
    for (int i = 0; i < totalDiskDevices; i++)
        _fnDisks[i].disk_dev.unmount();
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
    while (_fnDisks[count].fileh != nullptr && count < totalDiskDevices)
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
            SYSTEM_BUS.changeDeviceId(get_disk_dev(n), swap);
        }

        // The first slot gets the device ID of the last slot
        SYSTEM_BUS.changeDeviceId(get_disk_dev(0), last_id);

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
    if (slot < totalDiskDevices)
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
    transaction_continue(false);
    Debug_println("Fuji cmd: REBOOT");
    transaction_complete();
    fnSystem.reboot();
}

// Get SSID
SSIDConfig fujiDevice::fujicore_net_get_ssid()
{
    SSIDConfig cfg {};

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

    return cfg;
}

void fujiDevice::fujicmd_net_get_ssid()
{
    transaction_continue(false);
    Debug_println("Fuji cmd: GET SSID");

    SSIDConfig cfg = fujicore_net_get_ssid();
    transaction_put(&cfg, sizeof(cfg));
    return;
}

// Mount Server
bool fujiDevice::fujicore_mount_host_success(unsigned hostSlot)
{
    Debug_println("Fuji cmd: MOUNT HOST");

    // Make sure we weren't given a bad hostSlot
    if (!validate_host_slot(hostSlot, "mount_hosts"))
        return false;

    if (!hostMounted[hostSlot] && !_fnHosts[hostSlot].mount())
        return false;

    hostMounted[hostSlot] = true;
    return true;
}

bool fujiDevice::fujicmd_mount_host_success(unsigned hostSlot)
{
    transaction_continue(false);
    if (!fujicore_mount_host_success(hostSlot))
    {
        transaction_error();
        return false;
    }

    transaction_complete();
    return true;
}

void fujiDevice::fujicmd_net_scan_networks()
{
    uint8_t ret;

    transaction_continue(false);
    Debug_println("Fuji cmd: SCAN NETWORKS");

    _countScannedSSIDs = fnWiFi.scan_networks();
    ret = _countScannedSSIDs;
    transaction_put(&ret, sizeof(ret));
    return;
}

SSIDInfo fujiDevice::fujicore_net_scan_result(uint8_t index, bool *err)
{
    SSIDInfo detail {};

    bool is_err = index >= _countScannedSSIDs;
    if (!is_err)
        fnWiFi.get_scan_result(index, detail.ssid, &detail.rssi);
    if (err)
        *err = is_err;

    return detail;
}

void fujiDevice::fujicmd_net_scan_result(uint8_t index)
{
    transaction_continue(false);
    Debug_println("Fuji cmd: GET SCAN RESULT");

    bool err;
    SSIDInfo result = {};

    result = fujicore_net_scan_result(index, &err);
    transaction_put(&result, sizeof(result), err);
}

// Set SSID
bool fujiDevice::fujicmd_net_set_ssid_success(const char *ssid, const char *password,
                                              bool save)
{
    transaction_continue(false);
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

// Check if Wifi is enabled
uint8_t fujiDevice::fujicore_net_get_wifi_enabled()
{
    return Config.get_wifi_enabled() ? 1 : 0;
}

void fujiDevice::fujicmd_net_get_wifi_enabled()
{
    transaction_continue(false);
    uint8_t e = fujicore_net_get_wifi_enabled();
    Debug_printf("Fuji cmd: GET WIFI ENABLED: %d\n", e);
    transaction_put(&e, sizeof(e), false);
}

// Disk Image Mount
bool fujiDevice::fujicore_mount_disk_image_success(uint8_t deviceSlot,
                                                   disk_access_flags_t access_mode)
{
    // TODO: Implement FETCH?
    char mode[4] = {'r', 'b', 0, 0};
    if (access_mode == DISK_ACCESS_MODE_WRITE)
        mode[2] = '+';

    // Make sure we weren't given a bad hostSlot
    if (!validate_device_slot(deviceSlot))
        return false;

    if (!validate_host_slot(_fnDisks[deviceSlot].host_slot))
        return false;

    // A couple of reference variables to make things much easier to read...
    fujiDisk &disk = _fnDisks[deviceSlot];
    fujiHost &host = _fnHosts[disk.host_slot];
    DEVICE_TYPE *disk_dev = get_disk_dev(deviceSlot);

    Debug_printf("Selecting '%s' from host #%u as %s on D%u:\r\n",
                 disk.filename, disk.host_slot, mode, deviceSlot + 1);

    disk.fileh = host.fnfile_open(disk.filename, disk.filename, sizeof(disk.filename), mode);

    if (disk.fileh == nullptr)
        return false;

    // We've gotten this far, so make sure our bootable CONFIG disk is disabled
    boot_config = false;

    // We need the file size for loading XEX files and for CASSETTE, so get that too
    disk.disk_size = host.file_size(disk.fileh);
    disk.disk_type = disk_dev->mount(disk.fileh, disk.filename, disk.disk_size, access_mode);

    return true;
}

bool fujiDevice::fujicmd_mount_disk_image_success(uint8_t deviceSlot,
                                                  disk_access_flags_t access_mode)
{
    transaction_continue(false);
    Debug_println("Fuji cmd: MOUNT IMAGE");

    if (!fujicore_mount_disk_image_success(deviceSlot, access_mode))
    {
        transaction_error();
        return false;
    }

    transaction_complete();
    return true;
}

// Mounts the desired boot disk number
void fujiDevice::insert_boot_device(uint8_t image_id, std::string extension,
                                    mediatype_t disk_type, DEVICE_TYPE *disk_dev)
{
    std::string boot_img;
    fnFile *fBoot = nullptr;
    size_t image_size;

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
        Debug_printf("Failed to open boot disk image: %s\n", boot_img.c_str());
        return;
    }

    image_size = fsFlash.filesize(fBoot);
    disk_dev->mount(fBoot, boot_img.c_str(), image_size, DISK_ACCESS_MODE_READ, disk_type);
    disk_dev->is_config_device = true;
}

bool fujiDevice::fujicore_open_directory_success(uint8_t hostSlot, const std::string &dirpath)
{
    // See if there's a search pattern after the directory path
    const std::string *finalpath = &dirpath;
    std::string noslash;
    std::optional<std::string> pattern;
    int pathlen = finalpath->find('\0');
    if (pathlen < finalpath->size() - 3) // Allow for two NULLs and a 1-char pattern
        pattern = finalpath->substr(pathlen + 1);

    // Remove trailing slash
    if (pathlen > 1 && (*finalpath)[pathlen - 1] == '/') {
        noslash = finalpath->substr(0, pathlen - 1);
        finalpath = &noslash;
    }

    return fujicore_open_directory_success(hostSlot, *finalpath, pattern);
}

bool fujiDevice::fujicore_open_directory_success(uint8_t hostSlot, const std::string &dirpath,
                                                 const std::optional<std::string> &pattern)
{
    if (!validate_host_slot(hostSlot))
        return false;

    // If we already have a directory open, close it first
    if (_current_open_directory_slot != -1)
    {
        Debug_print("Directory was already open - closing it first\r\n");
        _fnHosts[_current_open_directory_slot].dir_close();
        _current_open_directory_slot = -1;
    }

    Debug_printf("Opening directory: \"%s\", pattern: \"%s\"\r\n",
                 dirpath.c_str(), pattern.value_or("").c_str());

    if (!_fnHosts[hostSlot].dir_open(dirpath.c_str(), pattern ? pattern->c_str() : nullptr, 0))
        return false;

    _current_open_directory_slot = hostSlot;
    return true;
}

bool fujiDevice::fujicmd_open_directory_success(uint8_t hostSlot)
{
    transaction_continue(true);
    Debug_println("Fuji cmd: OPEN DIRECTORY");

    if (!validate_host_slot(hostSlot))
    {
        transaction_error();
        return false;
    }

    std::string dirpath(256, 0);
    if (!transaction_get(dirpath.data(), dirpath.size())) {
        transaction_error();
        return false;
    }

    if (_current_open_directory_slot != -1)
    {
        Debug_print("Directory was already open - closing it first\n");
        _fnHosts[_current_open_directory_slot].dir_close();
        _current_open_directory_slot = -1;
    }

    if (!fujicore_open_directory_success(hostSlot, dirpath))
    {
        transaction_error();
        return false;
    }

    transaction_complete();
    return true;
}

void fujiDevice::fujicmd_close_directory()
{
    transaction_continue(false);
    Debug_println("Fuji cmd: CLOSE DIRECTORY");

    if (_current_open_directory_slot != -1)
        _fnHosts[_current_open_directory_slot].dir_close();

    _current_open_directory_slot = -1;
    transaction_complete();
}

/*
 * Read directory entries in block mode
 *
 * Input parameters:
 * aux1: Number of 256-byte pages to return (determines maximum response size)
 * aux2: Lower 6 bits define the number of entries per page group
 *
 * Response format:
 * Overall response header:
 * Byte  0    : 'M' (Magic number byte 1)
 * Byte  1    : 'F' (Magic number byte 2)
 * Byte  2    : Header size (4)
 * Byte  3    : Number of page groups that follow
 *
 * Followed by one or more complete PageGroups, padded to aux1 * 256 bytes.
 * Each PageGroup must fit entirely within the response - partial groups are not allowed.
 * If a PageGroup would exceed the remaining space, the directory position is rewound
 * and that group is not included.
 *
 * PageGroup structure:
 * Byte  0    : Flags
 *              - Bit 7: Last group (1=yes, 0=no)
 *              - Bits 6-0: Reserved
 * Byte  1    : Number of directory entries in this group
 * Bytes 2-3  : Group data size (16-bit little-endian, excluding header)
 * Byte  4    : Group index (0-based, calculated as dir_pos/group_size)
 * Bytes 5+   : File/Directory entries for this group
 *              Each entry:
 *              - Bytes 0-3: Packed timestamp and flags
 *                          - Byte 0: Years since 1970 (0-255)
 *                          - Byte 1: FFFF MMMM (4 bits flags, 4 bits month 1-12)
 *                                   Flags: bit 7 = directory, bits 6-4 reserved
 *                          - Byte 2: DDDDD HHH (5 bits day 1-31, 3 high bits of hour)
 *                          - Byte 3: HH mmmmmm (2 low bits hour 0-23, 6 bits minute 0-59)
 *              - Bytes 4-6: File size (24-bit little-endian, 0 for directories)
 *              - Byte  7  : Media type (0-255, with 0=unknown)
 *              - Bytes 8+ : Null-terminated filename
 *
 * The last PageGroup in the response will have its last_group flag set if:
 * a) There are no more directory entries to process, or
 * b) The next PageGroup would exceed the maximum response size
 */
void fujiDevice::fujicmd_read_directory_block(uint8_t num_pages, uint8_t group_size)
{
    transaction_continue(false);
    Debug_println("Fuji cmd: READ DIRECTORY BLOCK");

    size_t max_block_size = num_pages * DIR_BLOCK_SIZE;

    // Debug_printf("Parameters: aux1=$%02X (pages=%d), aux2=$%02X (group_size=%d), max_block_size=%d\n",
    //              cmdFrame.aux1, num_pages, cmdFrame.aux2, group_size, max_block_size);

#ifdef WE_NEED_TO_REWIND
    // Save current directory position in case we need to rewind
    uint16_t starting_pos = _fnHosts[_current_open_directory_slot].dir_tell();
#endif /* WE_NEED_TO_REWIND */
    // Debug_printf("Starting directory position: %d\n", starting_pos);

    std::vector<DirectoryPageGroup> page_groups;
    size_t total_size = 0;
    bool is_last_entry = false;

    while (!is_last_entry) {
        // Create a new page group
        DirectoryPageGroup group;
        uint16_t group_start_pos = _fnHosts[_current_open_directory_slot].dir_tell();

        // Calculate group index (0-based)
        group.index = group_start_pos / group_size;

        // Debug_printf("Starting new group at directory position: %d (index=%d)\n",
        //              group_start_pos, group.index);

        // Fill the group with entries
        for (int i = 0; i < group_size && !is_last_entry; i++) {
            fsdir_entry_t *f = _fnHosts[_current_open_directory_slot].dir_nextfile();

            if (f == nullptr) {
                // Debug_println("Reached end of directory");
                is_last_entry = true;
                group.is_last_group = true;
                break;
            }

            // Debug_printf("Adding entry %d: \"%s\" (size=%lu)\n",
            //             i, f->filename, f->size);

            if (!group.add_entry(f)) {
                // Debug_println("Failed to add entry to group");
                break;
            }
        }

        // If this is the last group, mark its last entry as the last one
        if (is_last_entry) {
            Debug_println("This is the last group in the directory");
            group.is_last_group = true;
        }

        // this sets all the data for the group up correctly for us to insert into the block
        group.finalize();

        // Check if adding this group would exceed max_block_size
        size_t new_total = total_size + group.data.size();
        // Debug_printf("Group stats: entries=%d, size=%d, new_total=%d/%d\n",
        //             group.entry_count, group.data.size(), new_total, max_block_size);

        if (new_total > max_block_size) {
            // Debug_printf("Group would exceed max_block_size (%d > %d), rewinding to pos %d\n",
            //            new_total, max_block_size, group_start_pos);
            // Rewind to start of this group and break
            _fnHosts[_current_open_directory_slot].dir_seek(group_start_pos);
            break;
        }

        // Add group to our collection
        total_size = new_total;
        page_groups.push_back(std::move(group));
        // Debug_printf("Added group %d, total_size now %d\n",
        //             page_groups.size(), total_size);
    }

    // If we couldn't fit any groups, return error
    if (page_groups.empty()) {
        Debug_println("No page groups fit in requested size");
        Debug_printf("Final stats: total_size=%d, max_block_size=%d\n",
                    total_size, max_block_size);
        transaction_error();
        return;
    }

    // Create final response buffer
    std::vector<uint8_t> response(max_block_size, 0);  // Initialize with zeros at full size

    // Add response header
    response[0] = 'M';  // Magic byte 1
    response[1] = 'F';  // Magic byte 2
    response[2] = 4;    // Header size (magic + size + count)
    response[3] = page_groups.size(); // Number of page groups that follow

    // Copy all page groups to response
    size_t current_pos = 4;  // Start after header
    for (const auto& group : page_groups) {
        if (current_pos + group.data.size() <= max_block_size) {
            std::copy(group.data.begin(), group.data.end(), response.begin() + current_pos);
            current_pos += group.data.size();
        }
    }

    // Debug_printf("Directory block stats:\n");
    // Debug_printf("  Number of groups: %d\n", page_groups.size());
    // Debug_printf("  Total data size: %d bytes\n", total_size);
    // Debug_printf("  Last group: %s\n", (page_groups.back().is_last_group ? "Yes" : "No"));
    // Debug_printf("Full response block:\n%s\n", util_hexdump(response.data(), response.size()).c_str());

    transaction_put(response.data(), response.size(), false);
}

std::optional<std::string> fujiDevice::fujicore_read_directory_entry(size_t maxlen,
                                                                     uint8_t addtl)
{
    // Make sure we have a current open directory
    if (_current_open_directory_slot == -1)
    {
        Debug_print("READ DIRECTORY ENTRY: No currently open directory\n");
        return std::nullopt;
    }

    // detect block mode in request
    if ((addtl & 0xC0) == 0xC0)
    {
        fujicmd_read_directory_block(maxlen, addtl & 0x3F);
        return std::nullopt;
    }

    size_t attrib_len = 0;
    char buffer[DIR_BLOCK_SIZE] {};

    fsdir_entry_t *entry = _fnHosts[_current_open_directory_slot].dir_nextfile();

    if (entry == nullptr)
        return std::string(2, char(0x7F));

    Debug_printf("::read_direntry \"%s\"\n", entry->filename);

    char *filenamedest = buffer;
    maxlen = std::min(maxlen, sizeof(buffer));

    // If 0x80 is set on ADDTL, send back additional information
    if (addtl & 0x80)
    {
        attrib_len = set_additional_direntry_details(entry, (uint8_t *) buffer, maxlen);
        // Adjust remaining size of buffer and file path destination
        filenamedest = buffer + attrib_len;
    }

    int filelen;
    int buflen = maxlen - attrib_len;

    if (strlen(entry->filename) >= buflen)
        filelen = util_ellipsize(entry->filename, filenamedest, buflen - 1);
    else
        filelen = strlcpy(filenamedest, entry->filename, buflen);

    // Add a slash at the end of directory entries
    if (entry->isDir && filelen < (buflen - 2))
    {
        buffer[filelen] = '/';
        buffer[filelen + 1] = '\0';
    }

    return std::string(buffer, strlen(filenamedest) + attrib_len + 1);
}

void fujiDevice::fujicmd_read_directory_entry(size_t maxlen, uint8_t addtl)
{
    transaction_continue(false);
    Debug_printf("Fuji cmd: READ DIRECTORY ENTRY (max=%hu) (addtl=%02x)\n", maxlen, addtl);

    char buffer[DIR_BLOCK_SIZE];
    size_t entrylen;
    auto current_entry = fujicore_read_directory_entry(maxlen, addtl);
    if (!current_entry)
    {
        transaction_error();
        return;
    }

    entrylen = std::min(maxlen, current_entry->size() + 1);
    Debug_printv("entry: \"%s\" len:%d maxlen:%d", current_entry->c_str(), entrylen, maxlen);
    memcpy(buffer, current_entry->data(), entrylen);
    transaction_put(buffer, maxlen, false);
}

size_t fujiDevice::_set_additional_direntry_details(fsdir_entry_t *f, uint8_t *dest,
                                                    uint8_t maxlen, int year_offset,
                                                    DET_size_endian_t size_endian,
                                                    DET_dir_flags_t dir_flags,
                                                    DET_has_type_t has_type)
{
    unsigned int idx = 0;

    // File modified date-time
    struct tm *modtime = localtime(&f->modified_time);

    dest[idx++] = modtime->tm_year - year_offset;
    dest[idx++] = modtime->tm_mon + 1;
    dest[idx++] = modtime->tm_mday;
    dest[idx++] = modtime->tm_hour;
    dest[idx++] = modtime->tm_min;
    dest[idx++] = modtime->tm_sec;

    switch (size_endian)
    {
    case SIZE_16_LE:
        {
            uint16_t fsize = htole16(f->size);
            memcpy(&dest[idx], &fsize, sizeof(fsize));
            idx += sizeof(fsize);
        }
        break;
    case SIZE_16_BE:
        {
            uint16_t fsize = htobe16(f->size);
            memcpy(&dest[idx], &fsize, sizeof(fsize));
            idx += sizeof(fsize);
        }
        break;
    case SIZE_32_LE:
        {
            uint32_t fsize = htole32(f->size);
            memcpy(&dest[idx], &fsize, sizeof(fsize));
            idx += sizeof(fsize);
        }
        break;
    case SIZE_32_BE:
        {
            uint32_t fsize = htobe32(f->size);
            memcpy(&dest[idx], &fsize, sizeof(fsize));
            idx += sizeof(fsize);
        }
        break;
    default:
        break;
    }

    dest[idx++] = f->isDir ? DET_FF_DIR : 0;

    // Remember where truncate field is, will fill in after we know how many bytes we need
    unsigned int trunc_field_idx = idx;
    if (dir_flags == HAS_DIR_ENTRY_FLAGS_COMBINED)
        trunc_field_idx--;
    else
        dest[idx++] = 0;

    // File type
    if (has_type == HAS_DIR_ENTRY_TYPE)
        dest[idx++] = MediaType::discover_mediatype(f->filename);

    // Adjust the truncated flag using total bytes of dir entry
    maxlen -= idx;

    // Also subtract a byte for a terminating slash on directories
    if (f->isDir)
        maxlen--;

    // Now that we know actual maxlen we can set the truncated flag
    if (strlen(f->filename) >= maxlen)
        dest[trunc_field_idx] |= DET_FF_TRUNC;

    Debug_printf("Addtl: ");
    for (int i = 0; i < idx; i++)
        Debug_printf("%02x ", dest[i]);
    Debug_printf("\n");
    return idx;
}

bool fujiDevice::fujicmd_copy_file_success(uint8_t sourceSlot, uint8_t destSlot,
                                           std::string copySpec)
{
    std::string sourcePath;
    std::string destPath;
    fnFile *sourceFile;
    fnFile *destFile;
    char *dataBuf;

    transaction_continue(false);
    Debug_printf("copySpec: %s\n", copySpec.c_str());

    // Check for malformed copyspec.
    if (copySpec.empty() || copySpec.find_first_of("|") == std::string::npos)
    {
        transaction_error();
        return false;
    }

    if (sourceSlot < 1 || sourceSlot > totalDiskDevices
        || destSlot < 1 || destSlot > totalDiskDevices)
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

bool fujiDevice::fujicore_unmount_disk_image_success(uint8_t deviceSlot)
{
    DEVICE_TYPE *disk_dev;

    Debug_printf("Fuji cmd: UNMOUNT IMAGE 0x%02X\n", deviceSlot);

    // FIXME - handle tape?
    if (deviceSlot >= totalDiskDevices)
        return false;

    disk_dev = get_disk_dev(deviceSlot);
#ifdef FIXME
    if (disk_dev->device_active)
        disk_dev->switched = true;
#else
#warning "FIXME - why is switched not part of all disk classes?"
#endif /* FIXME */
    disk_dev->unmount();
    _fnDisks[deviceSlot].reset();

    return true;
}

bool fujiDevice::fujicmd_unmount_disk_image_success(uint8_t deviceSlot)
{
    transaction_continue(false);
    if (!fujicore_unmount_disk_image_success(deviceSlot))
    {
        transaction_error();
        return false;
    }

    transaction_complete();
    return true;
}

void fujiDevice::fujicmd_get_adapter_config()
{
    AdapterConfig cfg {};

    transaction_continue(false);
    Debug_println("Fuji cmd: GET ADAPTER CONFIG");

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

AdapterConfigExtended fujiDevice::fujicore_get_adapter_config_extended()
{
    // also return string versions of the data to save the host some computing
    AdapterConfigExtended cfg {};

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
    strlcpy(cfg.sDnsIP, fnSystem.Net.get_ip4_dns_str().c_str(), 16);
    strlcpy(cfg.sNetmask, fnSystem.Net.get_ip4_mask_str().c_str(), 16);

    snprintf(cfg.sMacAddress, sizeof(cfg.sMacAddress), "%02X:%02X:%02X:%02X:%02X:%02X",
             cfg.macAddress[0], cfg.macAddress[1], cfg.macAddress[2], cfg.macAddress[3],
             cfg.macAddress[4], cfg.macAddress[5]);
    snprintf(cfg.sBssid, sizeof(cfg.sBssid), "%02X:%02X:%02X:%02X:%02X:%02X", cfg.bssid[0],
             cfg.bssid[1], cfg.bssid[2], cfg.bssid[3], cfg.bssid[4], cfg.bssid[5]);

    return cfg;
}

void fujiDevice::fujicmd_get_adapter_config_extended()
{
    transaction_continue(false);
    // also return string versions of the data to save the host some computing
    Debug_printf("Fuji cmd: GET ADAPTER CONFIG EXTENDED\r\n");

    AdapterConfigExtended cfg = fujicore_get_adapter_config_extended();
    transaction_put(&cfg, sizeof(cfg), false);
}

// Get a 256 byte filename from device slot
std::optional<std::string> fujiDevice::fujicore_get_device_filename(uint8_t slot)
{
    if (slot < totalDiskDevices)
        return std::string(_fnDisks[slot].filename);

    return std::nullopt;
}

void fujiDevice::fujicmd_get_device_filename(uint8_t slot)
{
    char buf[MAX_FILENAME_LEN] {};
    bool err = false;

    transaction_continue(false);
    auto filename = fujicore_get_device_filename(slot);
    if (filename)
        memcpy(buf, filename->data(), std::min(sizeof(buf), filename->size()));
    else
        err = true;

    transaction_put(buf, sizeof(buf), err);
}

// Write a 256 byte filename to the device slot
bool fujiDevice::fujicore_set_device_filename_success(uint8_t deviceSlot, uint8_t host,
                                                      disk_access_flags_t mode,
                                                      std::string filename)
{
    // Handle DISK slots
    if (deviceSlot >= totalDiskDevices)
    {
        Debug_println("BAD DEVICE SLOT");
        return false;
    }

    if (!filename.size())
        _fnDisks[deviceSlot].host_slot = INVALID_HOST_SLOT;
    else
    {
        std::strncpy(_fnDisks[deviceSlot].filename, filename.c_str(), MAX_FILENAME_LEN);
        _fnDisks[deviceSlot].filename[MAX_FILENAME_LEN - 1] = 0;
        _fnDisks[deviceSlot].host_slot = host;
    }

    _fnDisks[deviceSlot].access_mode = mode;
    populate_config_from_slots();

    Config.save();
    return true;
}

bool fujiDevice::fujicmd_set_device_filename_success(uint8_t deviceSlot, uint8_t host,
                                                     disk_access_flags_t mode)
{
    char tmp[MAX_FILENAME_LEN];

    transaction_continue(true);
    Debug_printf("Fuji cmd: SET DEVICE SLOT 0x%02X/%02X/%02X FILENAME: %s\n",
                 deviceSlot, host, mode, tmp);

    if (!transaction_get(tmp, sizeof(tmp)))
    {
        transaction_error();
        return false;
    }

    if (!fujicore_set_device_filename_success(deviceSlot, host, mode,
                                              std::string(tmp, strnlen(tmp, sizeof(tmp)))))
    {
        transaction_error();
        return false;
    }

    transaction_complete();
    return true;
}

uint16_t fujiDevice::fujicore_get_directory_position()
{
    // Make sure we have a current open directory
    if (_current_open_directory_slot == -1)
    {
        Debug_print("No currently open directory\n");
        return FNFS_INVALID_DIRPOS;
    }

    uint16_t pos = _fnHosts[_current_open_directory_slot].dir_tell();
    if (pos == FNFS_INVALID_DIRPOS)
    {
        return FNFS_INVALID_DIRPOS;
    }

    return pos;
}

void fujiDevice::fujicmd_get_directory_position()
{
    transaction_continue(false);
    Debug_println("Fuji cmd: GET DIRECTORY POSITION");

    uint16_t pos = fujicore_get_directory_position();
    if (pos == FNFS_INVALID_DIRPOS)
    {
        transaction_error();
        return;
    }
    // Return the value we read
    transaction_put(&pos, sizeof(pos), false);
}

// Retrieve host path prefix
void fujiDevice::fujicmd_get_host_prefix(uint8_t hostSlot)
{
    char prefix[MAX_HOST_PREFIX_LEN];

    transaction_continue(false);
    Debug_printf("Fuji cmd: GET HOST PREFIX %uh\n", hostSlot);

    if (!validate_host_slot(hostSlot))
    {
        transaction_error();
        return;
    }

    _fnHosts[hostSlot].get_prefix(prefix, sizeof(prefix));
    transaction_put(prefix, sizeof(prefix));
}

uint8_t fujiDevice::fujicore_net_get_wifi_status()
{
    // WL_CONNECTED = 3, WL_DISCONNECTED = 6
    return fnWiFi.connected() ? 3 : 6;
}

void fujiDevice::fujicmd_net_get_wifi_status()
{
    transaction_continue(false);
    Debug_println("Fuji cmd: GET WIFI STATUS");
    uint8_t wifiStatus = fujicore_net_get_wifi_status();
    transaction_put(&wifiStatus, sizeof(wifiStatus));
}

void fujiDevice::fujicmd_read_host_slots()
{
    transaction_continue(false);
    Debug_println("Fuji cmd: READ HOST SLOTS");

    char hostSlots[MAX_HOSTS][MAX_HOSTNAME_LEN] = {0};

    for (int i = 0; i < MAX_HOSTS; i++)
        strlcpy(hostSlots[i], _fnHosts[i].get_hostname(), MAX_HOSTNAME_LEN);

    transaction_put(&hostSlots, sizeof(hostSlots));
}

// Read and save host slot data from computer
void fujiDevice::fujicmd_write_host_slots()
{
    transaction_continue(true);
    Debug_println("Fuji cmd: WRITE HOST SLOTS");

    char hostSlots[MAX_HOSTS][MAX_HOSTNAME_LEN];
    if (!transaction_get(&hostSlots, sizeof(hostSlots)))
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
    transaction_continue(false);
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
                                       mediatype_t disk_type, DEVICE_TYPE *disk_dev)
{
    transaction_continue(false);
    insert_boot_device(bootMode, extension, disk_type, disk_dev);
    boot_config = true;
    transaction_complete();
}

void fujiDevice::fujicmd_set_directory_position(uint16_t pos)
{
    transaction_continue(false);
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
    char buffer[MAX_HOST_PREFIX_LEN];

    transaction_continue(true);
    if (!prefix)
    {
        if (!transaction_get(buffer, MAX_FILENAME_LEN))
        {
            transaction_error();
            return;
        }
        prefix = buffer;
    }

    Debug_printf("Fuji cmd: SET HOST PREFIX %uh \"%s\"\n", hostSlot, prefix);

    if (!validate_host_slot(hostSlot))
    {
        transaction_error();
        return;
    }

    _fnHosts[hostSlot].set_prefix(prefix);
    transaction_complete();
}

// Unmount specified host
bool fujiDevice::fujicmd_unmount_host_success(uint8_t hostSlot)
{
    transaction_continue(false);
    Debug_printf("\r\nFuji cmd: MOUNT HOST no. %d", hostSlot);

    if (!validate_host_slot(hostSlot, "sio_tnfs_mount_hosts")
        || (hostMounted[hostSlot] == false))
    {
        transaction_error();
        return false;
    }

    // Unmount any disks associated with host slot
    for (int i = 0; i < totalDiskDevices; i++)
    {
        if (_fnDisks[i].host_slot == hostSlot)
        {
            _fnDisks[i].disk_dev.unmount();
            _fnDisks[i].disk_dev.device_active = false;
            _fnDisks[i].reset();
        }
    }

    // Unmount the host
    if (!_fnHosts[hostSlot].unmount_success())
    {
        transaction_error();
        return false;
    }

    transaction_complete();
    return true;
}

// Send device slot data to computer
void fujiDevice::fujicmd_read_device_slots()
{
    transaction_continue(false);
    Debug_println("Fuji cmd: READ DEVICE SLOTS");

    char *filename;
    disk_slot diskSlots[MAX_DISK_DEVICES] {};

    // Load the data from our current device array
    for (int i = 0; i < totalDiskDevices; i++)
    {
        diskSlots[i].mode = _fnDisks[i].access_mode;
        diskSlots[i].hostSlot = _fnDisks[i].host_slot;
        strlcpy(diskSlots[i].filename, _fnDisks[i].filename, MAX_DISPLAY_FILENAME_LEN);

        if (_fnDisks[i].filename[0] == '\0')
        {
            strlcpy(diskSlots[i].filename, "", MAX_DISPLAY_FILENAME_LEN);
        }
        else
        {
            // Just use the basename of the image, no path. The full path+filename is
            // usually too long for many platforms to show anyway, so the image name is more important.
            // Note: Basename can modify the input, so use a copy of the filename
            filename = strdup(_fnDisks[i].filename);
            strlcpy(diskSlots[i].filename, basename(filename), MAX_DISPLAY_FILENAME_LEN);
            free(filename);
        }

        DEVICE_TYPE *disk_dev = get_disk_dev(i);
        if (disk_dev->device_active && !disk_dev->is_config_device)
            diskSlots[i].mode |= DISK_ACCESS_MODE_MOUNTED;
    }

    transaction_put(&diskSlots, sizeof(disk_slot) * totalDiskDevices);
}

// Read and save disk slot data from computer
void fujiDevice::fujicmd_write_device_slots()
{
    transaction_continue(true);
    Debug_println("Fuji cmd: WRITE DEVICE SLOTS");

    disk_slot diskSlots[MAX_DISK_DEVICES];

    if (!transaction_get(&diskSlots, sizeof(disk_slot) * totalDiskDevices))
    {
        transaction_error();
        return;
    }

    // Load the data into our current device array
    for (int i = 0; i < totalDiskDevices; i++)
        _fnDisks[i].reset(diskSlots[i].filename, diskSlots[i].hostSlot,
                          (disk_access_flags_t) diskSlots[i].mode);

    // Save the data to disk
    populate_config_from_slots();
    Config.save();
    transaction_complete();
}

void fujiDevice::fujicmd_status()
{
    transaction_continue(false);
    Debug_println("Fuji cmd: STATUS");

    char ret[4] = {0};

    transaction_put(ret, sizeof(ret), false);
    return;
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
bool fujiDevice::fujicore_open_app_key(uint16_t creator, uint8_t app, uint8_t key,
                                       appkey_mode mode, uint8_t reserved)
{
    // Basic check for valid data
    if (creator == 0 || mode == APPKEYMODE_INVALID)
    {
        Debug_println("Invalid app key data");
        return false;
    }

    _current_appkey.creator = creator;
    _current_appkey.app = app;
    _current_appkey.key = key;
    _current_appkey.mode = mode;
    _current_appkey.reserved = reserved;

    Debug_printf("App key creator = 0x%04hx, app = 0x%02hhx, key = 0x%02hhx, mode = %hhu, "
                 "filename = \"%s\"\n",
                 _current_appkey.creator, _current_appkey.app, _current_appkey.key,
                 _current_appkey.mode, _generate_appkey_filename(&_current_appkey));
    return true;
}

void fujiDevice::fujicmd_open_app_key()
{
    transaction_continue(true);
    Debug_print("Fuji cmd: OPEN APPKEY\n");

    appkey key;

    // The data expected for this command
    if (!transaction_get(&key, sizeof(key)))
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

    if (!fujicore_open_app_key(key.creator, key.app, key.key, key.mode, key.reserved))
    {
        transaction_error();
        return;
    }
    transaction_complete();
}

/*
  The app key close operation is a placeholder in case we want to provide more robust file
  read/write operations. Currently, the file is closed immediately after the read or write
  operation.
*/
void fujiDevice::fujicmd_close_app_key()
{
    transaction_continue(false);
    Debug_print("Fuji cmd: CLOSE APPKEY\n");
    _current_appkey.creator = 0;
    _current_appkey.mode = APPKEYMODE_INVALID;
    transaction_complete();
}

/*
 Write an "app key" to SD (ONLY!) storage.
*/
int fujiDevice::fujicore_write_app_key(std::vector<uint8_t>&& value, int *err)
{
    // Make sure we have valid app key information
    if (_current_appkey.creator == 0 || _current_appkey.mode != APPKEYMODE_WRITE)
    {
        Debug_println("Invalid app key metadata - aborting");
        return - 1;
    }

    // Make sure we have an SD card mounted
    if (fnSDFAT.running() == false)
    {
        Debug_println("No SD mounted - can't write app key");
        return -1;
    }

    char *filename = _generate_appkey_filename(&_current_appkey);

    // Reset the app key data so we require calling APPKEY OPEN before another attempt
    _current_appkey.creator = 0;
    _current_appkey.mode = APPKEYMODE_INVALID;

    Debug_printf("Writing appkey to \"%s\"\n", filename);

    // Make sure we have a "/FujiNet" directory, since that's where we're putting these files
    fnSDFAT.create_path("/FujiNet");

    FILE *fOut = fnSDFAT.file_open(filename, FILE_WRITE);
    if (fOut == nullptr)
    {
        Debug_printf("Failed to open/create output file: errno=%d\n", errno);
        return -1;
    }
    size_t count = fwrite(value.data(), 1, value.size(), fOut);
    if (*err)
        *err = errno;
    fclose(fOut);
    return count;
}

void fujiDevice::fujicmd_write_app_key(uint16_t keylen)
{
    transaction_continue(true);
    Debug_printf("Fuji cmd: WRITE APPKEY (keylen = %hu)\n", keylen);

    // Data for  FUJICMD_WRITE_APPKEY
    uint8_t value[MAX_APPKEY_LEN];

    if (!transaction_get(value, keylen))
    {
        transaction_error();
        return;
    }

    int err;
    int count = fujicore_write_app_key(std::vector<uint8_t>(value, value + keylen), &err);
    if (count < 0)
    {
        transaction_error();
        return;
    }

    if (count != keylen)
    {
        Debug_printf("Only wrote %u bytes of expected %hu, errno=%d\n", count, keylen, err);
        transaction_error();
    }

    transaction_complete();
}

/*
 Read an "app key" from SD (ONLY!) storage
*/
std::optional<std::vector<uint8_t>> fujiDevice::fujicore_read_app_key()
{
    std::vector<uint8_t> response_data(MAX_APPKEY_LEN);

    // Make sure we have an SD card mounted
    if (fnSDFAT.running() == false)
    {
        Debug_println("No SD mounted - can't read app key");
        return std::nullopt;
    }

    // Make sure we have valid app key information, and the mode is not WRITE
    if (_current_appkey.creator == 0 || _current_appkey.mode == APPKEYMODE_WRITE)
    {
        Debug_println("Invalid app key metadata - aborting");
        return std::nullopt;
    }

    char *filename = _generate_appkey_filename(&_current_appkey);
    Debug_printf("Reading appkey from \"%s\"\n", filename);

    FILE *fIn = fnSDFAT.file_open(filename, FILE_READ);
    if (fIn == nullptr)
    {
        Debug_printf("Failed to open input file: errno=%d\n", errno);
        return std::nullopt;
    }

    size_t count = fread(response_data.data(), 1, MAX_APPKEY_LEN, fIn);
    response_data.resize(count);
    Debug_printf("Read %u bytes from input file\n", (unsigned)count);
    fclose(fIn);

#ifdef DEBUG
    std::string msg = util_hexdump(response_data.data(), response_data.size());
    Debug_printf("\n%s\n", msg.c_str());
#endif

    return response_data;
}

void fujiDevice::fujicmd_read_app_key()
{
    transaction_continue(false);
    Debug_println("Fuji cmd: READ APPKEY");
    std::vector<uint8_t> response_data;
    auto result = fujicore_read_app_key();
    if (result)
        response_data = *result;
    else
        response_data = std::vector<uint8_t>(MAX_APPKEY_LEN + 2, 0);

    transaction_put(response_data.data(), response_data.size(), false);
}

#ifdef SYSTEM_BUS_IS_SERIAL
// Set an external clock rate in kHz defined by speed in steps of 2kHz.
void fujiDevice::fujicmd_set_sio_external_clock(uint16_t speed)
{
    transaction_continue(false);

    int baudRate = speed * 1000;

    Debug_printf("sioFuji::fujicmd_set_external_clock(%u)\n", baudRate);

    if (speed == 0)
    {
        SYSTEM_BUS.setUltraHigh(false, 0);
    }
    else
    {
        SYSTEM_BUS.setUltraHigh(true, baudRate);
    }

    transaction_complete();
}
#endif /* SYSTEM_BUS_IS_SERIAL */

#ifdef SYSTEM_BUS_IS_UDP
// Set UDP Stream HOST & PORT and start it
void fujiDevice::fujicmd_enable_udpstream(int port)
{
    char host[64];

    transaction_continue(true);
    if (!transaction_get(&host, sizeof(host)))
    {
        transaction_error();
        return;
    }

    Debug_printf("Fuji cmd ENABLE UDPSTREAM: HOST:%s PORT: %d\n", host, port);

    // Save the host and port
    Config.store_udpstream_host(host);
    Config.store_udpstream_port(port);
    Config.save();

    transaction_complete();

    // Start the UDP Stream
    SYSTEM_BUS.setUDPHost(host, port);
}
#endif /* SYSTEM_BUS_IS_UDP */
