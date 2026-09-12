#ifdef BUILD_COCO

#include "drivewireFuji.h"
#include "NDevice.h"
#include "compat_string.h"

#define IMAGE_EXTENSION ".dsk"
#define LOBBY_URL       "tnfs://tnfs.fujinet.online/COCO/lobby.dsk"

drivewireFuji platformFuji;
fujiDevice *theFuji = &platformFuji; // Global fuji object.

// drivewireDisk drivewireDiskDevs[MAX_HOSTS];
NDevice drivewireNetDevs[MAX_NETWORK_DEVICES];

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

    SYSTEM_BUS.transaction_accept(TRANS_STATE::WILL_GET);
    SYSTEM_BUS.transaction_get(&newDisk, sizeof(newDisk));

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
        SYSTEM_BUS.transaction_error();
        return;
    }

    disk.fileh = host.fnfile_open(disk.filename, disk.filename, sizeof(disk.filename), "w");
    if (disk.fileh == nullptr)
    {
        Debug_printf("drivewire_new_disk Couldn't open file for writing: \"%s\"\n", disk.filename);
        SYSTEM_BUS.transaction_error();
        return;
    }

    bool ok = disk.disk_dev.write_blank(disk.fileh, newDisk.numDisks);

    if (ok)
        SYSTEM_BUS.transaction_success();
    else
        SYSTEM_BUS.transaction_error();

    fnio::fclose(disk.fileh);
}

// Initializes base settings and adds our devices to the DRIVEWIRE bus
void drivewireFuji::setup()
{
    Debug_printf("theFuji->setup()\n");
    // set up Fuji device

    populate_slots_from_config();

    insert_boot_device(Config.get_general_boot_mode(), MEDIATYPE_UNKNOWN, FUJI_BOOTDISK);

    // Disable booting from CONFIG if our settings say to turn it off
    boot_config = Config.get_general_config_enabled();
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
    SYSTEM_BUS.transaction_send(&r, sizeof(r));
}

bool drivewireFuji::processCommand(const FujiDWPacket &packet)
{
    _errorCode = NDEV_STATUS::SUCCESS;

    // Let the base class handle standard commands
    if (fujiDevice::processCommand(packet))
        return true;

    switch (packet.command())
    {
    case CMD::FUJI_NEW_DISK:
        new_disk();
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

ByteBuffer drivewireFuji::appkey_read()
{
    u16ne_t len;
    auto result = fujiDevice::appkey_read();
    len = htole16(result.size());
    result.resize(MAX_APPKEY_LEN, 0);
    const uint8_t *len_bytes = reinterpret_cast<const uint8_t*>(&len);
    result.insert(result.begin(), len_bytes, len_bytes + sizeof(len));
    return result;
}

void drivewireFuji::appkey_write(const FUJI_COMMAND_PACKET &packet)
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::WILL_GET);

    uint16_t keylen = packet.param(0);

    // The controller always sends a fixed MAX_APPKEY_LEN payload with the real
    // length in the params. Draining fewer bytes leaves the rest in the stream
    // and desyncs the bus, so always read the full block and keep only keylen.
    ByteBuffer keydata(MAX_APPKEY_LEN);
    if (!SYSTEM_BUS.transaction_get(keydata.data(), keydata.size()))
    {
        SYSTEM_BUS.transaction_error();
        return;
    }

    if (keylen > MAX_APPKEY_LEN)
        keylen = MAX_APPKEY_LEN;
    keydata.resize(keylen);

    if (fujiDevice::appkey_write(keydata).is_error())
    {
        SYSTEM_BUS.transaction_error();
        return;
    }

    SYSTEM_BUS.transaction_success();
}

#endif /* BUILD_COCO */
