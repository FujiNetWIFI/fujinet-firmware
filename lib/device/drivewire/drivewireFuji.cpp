#ifdef BUILD_COCO

#include "drivewireFuji.h"
#include "fujiCommandID.h"
#include "network.h"
#include "fnWiFi.h"
#include "utils.h"
#include "compat_string.h"
#include "endianness.h"
#include "fuji_endian.h"
#include "../../bus/drivewire/drivewire.h"

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
    custom_details.size = htobe32(details.size);
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

// This gets called when we're about to shutdown/reboot
void drivewireFuji::shutdown()
{
    for (int i = 0; i < MAX_DWDISK_DEVICES; i++)
        _fnDisks[i].disk_dev.unmount();
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

    transaction_begin(TRANS_STATE::WILL_GET);
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
        transaction_error();
        return;
    }

    bool ok = disk.disk_dev.write_blank(disk.fileh, newDisk.numDisks);

    if (ok)
        transaction_complete();
    else
        transaction_error();

    fnio::fclose(disk.fileh);
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

// On Dragon, boot mode 2 additionally switches the named-object fallback
// used by op_readex() from /AUTOLOAD.DWL to /DGNLOBBY.DWL for the next
// szNamedMount reads. The normal lobby disk mount below still happens for
// both Dragon and CoCo, unchanged.
void drivewireFuji::insert_boot_device(uint8_t image_id, mediatype_t disk_type,
                                       DISK_DEVICE *disk_dev)
{
    if (image_id == 2 && SYSTEM_BUS.isDragon())
    {
        Debug_printf("Boot mode 2 (Dragon): using DGNLOBBY.DWL for named object fallback\n");
        SYSTEM_BUS.useLobbyDwl = true;
    }

    fujiDevice::insert_boot_device(image_id, disk_type, disk_dev);
}

void drivewireFuji::random()
{
    int r = rand();
    Debug_printf("drivewireFuji::random(%u)\n",r);
    transaction_put(&r, sizeof(r));
}

bool drivewireFuji::processCommand(const FujiDWPacket &packet)
{
    // Let the base class handle standard commands
    if (fujiDevice::processCommand(packet))
        return true;

    _errorCode = NDEV_STATUS::SUCCESS;
    switch (packet.command())
    {
    case FUJICMD_RESET:
        fnSystem.reboot();
        break;
    case FUJICMD_GET_ADAPTERCONFIG:
        fujicmd_get_adapter_config();
        break;
    case FUJICMD_GET_ADAPTERCONFIG_EXTENDED:
        fujicmd_get_adapter_config_extended();
        break;
    case FUJICMD_GET_SCAN_RESULT:
        fujicmd_net_scan_result(packet.param(0));
        break;
    case FUJICMD_SCAN_NETWORKS:
        fujicmd_net_scan_networks();
        break;
    case FUJICMD_SET_SSID:
        {
            SSIDConfig cfg;
            // Handler owns the transaction because it must transaction_get the
            // payload first, so call the core (not fujicmd_) set-ssid helper.
            transaction_begin(TRANS_STATE::WILL_GET);
            if (!transaction_get(&cfg, sizeof(cfg)))
                transaction_error();
            else if (fujicore_net_set_ssid_success(cfg.ssid, cfg.password, true).is_error())
                transaction_error();
            else
                transaction_complete();
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
        fujicmd_mount_host_success(packet.param8(0));
        break;
    case FUJICMD_OPEN_DIRECTORY:
        fujicmd_open_directory_success(packet.param(0));
        break;
    case FUJICMD_CLOSE_DIRECTORY:
        fujicmd_close_directory();
        break;
    case FUJICMD_READ_DIR_ENTRY:
        fujicmd_read_directory_entry(packet.param8(0), packet.param(1));
        break;
    case FUJICMD_SET_DIRECTORY_POSITION:
        fujicmd_set_directory_position(packet.param(0));
        break;
    case FUJICMD_SET_DEVICE_FULLPATH:
        fujicmd_set_device_filename_success(packet.param(0), packet.param(1),
                                            (disk_access_flags_t) packet.param8(2));
        break;
    case FUJICMD_GET_DEVICE_FULLPATH:
        fujicmd_get_device_filename(packet.param(0));
        break;
    case FUJICMD_MOUNT_IMAGE:
        fujicmd_mount_disk_image_success(packet.param(0),
                                         (disk_access_flags_t) packet.param8(1));
        break;
    case FUJICMD_UNMOUNT_HOST:
        fujicmd_unmount_host_success(packet.param(0));
        break;
    case FUJICMD_UNMOUNT_IMAGE:
        fujicmd_unmount_disk_image_success(packet.param(0));
        break;
    case FUJICMD_NEW_DISK:
        new_disk();
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
        // fujinet-lib always sends MAX_APPKEY_LEN data bytes
        // regardless of len. Drain the full payload so leftover
        // bytes don't get interpreted as bus opcodes.
        fujicmd_write_app_key(packet.param(0), MAX_APPKEY_LEN);
        break;
    case FUJICMD_RANDOM_NUMBER:
        random();
        break;
    case FUJICMD_SET_BOOT_MODE:
        fujicmd_set_boot_mode(packet.param(0), MEDIATYPE_UNKNOWN, &bootdisk);
        break;
    case FUJICMD_MOUNT_ALL:
        fujicmd_mount_all_success();
        break;
    case FUJICMD_GET_HOST_PREFIX:
        fujicmd_get_host_prefix(packet.param(0));
        break;
    case FUJICMD_SET_HOST_PREFIX:
        fujicmd_set_host_prefix(packet.param(0));
        break;
    case FUJICMD_COPY_FILE:
        {
            uint8_t source = packet.param(0);
            uint8_t dest = packet.param(1);
            char dirpath[256];
            transaction_begin(TRANS_STATE::WILL_GET);
            transaction_get(dirpath, sizeof(dirpath));
            if (fujicore_copy_file_success(source, dest, dirpath).is_error())
                transaction_error();
            else
                transaction_complete();
        }
        break;
    case FUJICMD_GENERATE_GUID:
        fujicmd_generate_guid();
        break;
    case FUJICMD_STATUS:
        fujicmd_status();
        break;
    case FUJICMD_GET_DIRECTORY_POSITION:
        fujicmd_get_directory_position();
        break;
    default:
        return false;
    }

    return true;
}

success_is_true drivewireFuji::fujicore_mount_disk_image_success(uint8_t deviceSlot,
                                                                   disk_access_flags_t access_mode)
{
    if (!fujiDevice::fujicore_mount_disk_image_success(deviceSlot, access_mode))
        RETURN_ERROR_AS_FALSE();

    fujiDisk &disk = *get_disk(deviceSlot);
    fujiHost &host = _fnHosts[disk.host_slot];
    get_disk_dev(deviceSlot)->set_media_host(&host);

    RETURN_SUCCESS_AS_TRUE();
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

void drivewireFuji::fujicmd_open_app_key()
{
    // fujinet-lib for coco sends appkey creator with backwards
    // endianness, we'll fix it here

    transaction_begin(TRANS_STATE::WILL_GET);
    Debug_print("Fuji cmd: OPEN APPKEY\n");

    appkey key;

    // The data expected for this command
    if (!transaction_get(&key, sizeof(key)))
    {
        transaction_error();
        return;
    }

    if (!fujicore_open_app_key(be16toh(key.creator), key.app, key.key, key.mode, key.reserved))
    {
        transaction_error();
        return;
    }
    transaction_complete();
}

#endif /* BUILD_COCO */
