#ifdef BUILD_ATARI

#include "sioFuji.h"
#include "fsFlash.h"
#include "fnFsSD.h"
#include "utils.h"
#include "base64.h"
#include "compat_string.h"
#include "fuji_endian.h"
#include "siocpm.h"

#include <cstring>

extern sioCPM sioZ;

#define IMAGE_EXTENSION ".atr"
#define LOBBY_URL       "tnfs://tnfs.fujinet.online/ATARI/_lobby.xex"

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

// Announce the new D1: through SAM, out SIO pin 11
void sioFuji::announce_rotation(int drive_slot)
{
    if (!Config.get_general_rotation_sounds())
        return;

    say_swap_label();
    say_number(drive_slot + 1); // because slots start from 0
}

// Constructor
sioFuji::sioFuji() : fujiDevice(MAX_DISK_DEVICES, IMAGE_EXTENSION, LOBBY_URL)
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

// Set SIO baudrate
void sioFuji::sio_set_baudrate(const FujiSIOPacket &packet)
{

    int br = 0;

    switch(packet.param8(0)) {

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
            SYSTEM_BUS.transaction_error();
            return;
    }

    // send complete with current baudrate
    SYSTEM_BUS.transaction_success();

#ifndef ESP_PLATFORM
    fnSystem.delay_microseconds(2000);
#endif
    SYSTEM_BUS.setBaudrate(br);
}

// Do SIO copy
void sioFuji::sio_copy_file(const FujiSIOPacket &packet)
{
    uint8_t csBuf[256];
    std::string copySpec;
    std::string sourcePath;
    std::string destPath;
    uint8_t ck;
    fnFile *sourceFile;
    fnFile *destFile;
    char *dataBuf;
    unsigned char sourceSlot;
    unsigned char destSlot;
#ifndef ESP_PLATFORM
    uint64_t poll_ts = fnSystem.millis();
#endif

    dataBuf = (char *)malloc(532);

    if (dataBuf == nullptr)
    {
        SYSTEM_BUS.transaction_error();
        return;
    }

    memset(&csBuf, 0, sizeof(csBuf));

    SYSTEM_BUS.transaction_accept(TRANS_STATE::WILL_GET); // quick fix to permit copy to work again
    if (!SYSTEM_BUS.transaction_get(csBuf, sizeof(csBuf)))
    {
        SYSTEM_BUS.transaction_error();
        free(dataBuf);
        return;
    }

    copySpec = std::string((char *)csBuf);

    Debug_printf("copySpec: %s\n", copySpec.c_str());

    // Check for malformed copyspec.
    if (copySpec.empty() || copySpec.find_first_of("|") == std::string::npos)
    {
        SYSTEM_BUS.transaction_error();
        free(dataBuf);
        return;
    }

    sourceSlot = packet.param(0);
    destSlot = packet.param(1);

    sourceSlot--, destSlot--;

    if (!validate_host_slot(sourceSlot) || !validate_host_slot(destSlot))
    {
        SYSTEM_BUS.transaction_error();
        free(dataBuf);
        return;
    }

    // All good, after this point...

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
    sourceFile = _fnHosts[sourceSlot].fnfile_open(sourcePath.c_str(), (char *)sourcePath.c_str(), sourcePath.size() + 1, FILE_READ);

    if (sourceFile == nullptr)
    {
        SYSTEM_BUS.transaction_error();
        free(dataBuf);
        return;
    }

    destFile = _fnHosts[destSlot].fnfile_open(destPath.c_str(), (char *)destPath.c_str(), destPath.size() + 1, FILE_WRITE);

    if (destFile == nullptr)
    {
        SYSTEM_BUS.transaction_error();
        fnio::fclose(sourceFile);
        free(dataBuf);
        return;
    }

    size_t readCount = 0;
    size_t readTotal = 0;
    size_t writeCount = 0;
    size_t expected = _fnHosts[sourceSlot].file_size(sourceFile); // get the filesize
    bool err = false;
    do
    {
        readCount = fnio::fread(dataBuf, 1, 532, sourceFile);
        readTotal += readCount;
        // Check if we got enough bytes on the read
        if (readCount < 532 && readTotal != expected)
        {
            err = true;
            break;
        }
        writeCount = fnio::fwrite(dataBuf, 1, readCount, destFile);
        // Check if we sent enough bytes on the write
        if (writeCount != readCount)
        {
            err = true;
            break;
        }
        Debug_printf("Copy File: %d bytes of %d\n", readTotal, expected);
    } while (readTotal < expected);

    if (err == true)
    {
        // Remove the destination file and error
        _fnHosts[destSlot].file_remove((char *)destPath.c_str());
        SYSTEM_BUS.transaction_error();
        Debug_printf("Copy File Error! wCount: %d, rCount: %d, rTotal: %d, Expect: %d\n", writeCount, readCount, readTotal, expected);
    }
    else
    {
        SYSTEM_BUS.transaction_success();
    }

    // copyEnd:
    fnio::fclose(sourceFile);
    fnio::fclose(destFile);
    free(dataBuf);
}

size_t read_file_into_vector(FILE* fIn, std::vector<uint8_t>& response_data, size_t size) {
    response_data.resize(size + 2);
    size_t bytes_read = fread(response_data.data() + 2, 1, size, fIn);

    // Insert the size at the beginning of the vector
    response_data[0] = static_cast<uint8_t>(bytes_read & 0xFF); // Low byte of the size
    response_data[1] = static_cast<uint8_t>((bytes_read >> 8) & 0xFF); // High byte of the size
    return bytes_read;
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

size_t sioFuji::set_additional_direntry_details(fsdir_entry_t *f, uint8_t *dest,
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
    custom_details.modified.year -= 70;
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
void sioFuji::sio_new_disk()
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::WILL_GET);
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
    if (!SYSTEM_BUS.transaction_get(&newDisk, sizeof(newDisk)))
    {
        Debug_print("sio_new_disk Bad checksum\n");
        SYSTEM_BUS.transaction_error();
        return;
    }
    if (newDisk.deviceSlot >= MAX_DISK_DEVICES || newDisk.hostSlot >= MAX_HOSTS)
    {
        Debug_print("sio_new_disk Bad disk or host slot parameter\n");
        SYSTEM_BUS.transaction_error();
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
        SYSTEM_BUS.transaction_error();
        return;
    }

    disk.fileh = host.fnfile_open(disk.filename, disk.filename, sizeof(disk.filename), FILE_WRITE);
    if (disk.fileh == nullptr)
    {
        Debug_printf("sio_new_disk Couldn't open file for writing: \"%s\"\n", disk.filename);
        SYSTEM_BUS.transaction_error();
        return;
    }

    bool ok = disk.disk_dev.write_blank(disk.fileh, newDisk.sectorSize, newDisk.numSectors);
    fnio::fclose(disk.fileh);

    if (ok == false)
    {
        Debug_print("sio_new_disk Data write failed\n");
        SYSTEM_BUS.transaction_error();
        return;
    }

    Debug_print("sio_new_disk succeeded\n");
    SYSTEM_BUS.transaction_success();
}

// AUX1 is our index value (from 0 to SIO_HISPEED_LOWEST_INDEX, for FN-PC 0 .. 10, 16)
// AUX2 requests a save of the change if set to 1
void sioFuji::sio_set_hsio_index(const FujiSIOPacket &packet)
{
    Debug_println("Fuji cmd: SET HSIO INDEX");

    // DAUX1 holds the desired index value
    uint8_t index = packet.param(0);

    // Make sure it's a valid value
#ifdef ESP_PLATFORM
    if (index > SIO_HISPEED_LOWEST_INDEX)
#else
    if (index > SIO_HISPEED_LOWEST_INDEX && index != SIO_HISPEED_x2_INDEX) // accept 0 .. 10, 16
#endif
    {
        SYSTEM_BUS.transaction_error();
        return;
    }

    SYSTEM_BUS.setHighSpeedIndex(index);

    // Go ahead and save it if AUX2 = 1
    if (packet.param8(1) & 1)
    {
        Config.store_general_hsioindex(index);
        Config.save();
    }

    SYSTEM_BUS.transaction_success();
}

// Mounts the desired boot disk, honoring alternate SD config and CONFIG-NG settings.
void sioFuji::insert_boot_device(uint8_t image_id, mediatype_t disk_type,
                                 DISK_DEVICE *disk_dev)
{
    if (image_id != 0)
    {
        fujiDevice::insert_boot_device(image_id, disk_type, disk_dev);
        return;
    }

    std::string altconfigfile = Config.get_config_filename();
    fnFile *fBoot = nullptr;
    size_t image_size = 0;
    std::string boot_img;

    if (!altconfigfile.empty() && fnSDFAT.running())
    {
        fBoot = fnSDFAT.fnfile_open(altconfigfile.c_str());
        if (fBoot != nullptr)
        {
            boot_img = altconfigfile;
            image_size = FileSystem::filesize(fBoot);
            Debug_printf("Mounted Alternate CONFIG %s\n", boot_img.c_str());
            disk_dev->mount(fBoot, boot_img.c_str(), image_size, DISK_ACCESS_MODE_READ, disk_type);
            disk_dev->is_config_device = true;
            return;
        }
    }

    if (Config.get_general_config_ng())
    {
        boot_img = "/autorun-cng" + _diskImageExtension;
        Debug_printf("Mounted CONFIG-NG\n");
    }
    else
    {
        boot_img = "/autorun" + _diskImageExtension;
    }

    fBoot = fsFlash.fnfile_open(boot_img.c_str());
    if (fBoot == nullptr)
    {
        Debug_printf("Failed to open boot disk image: %s\n", boot_img.c_str());
        return;
    }

    image_size = fsFlash.filesize(fBoot);
    disk_dev->mount(fBoot, boot_img.c_str(), image_size, DISK_ACCESS_MODE_READ, disk_type);
    disk_dev->is_config_device = true;
}

// Initializes base settings and adds our devices to the SIO bus
void sioFuji::setup()
{
    // set up Fuji device
    populate_slots_from_config();

    insert_boot_device(Config.get_general_boot_mode(), MEDIATYPE_UNKNOWN, &bootdisk);

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
    SYSTEM_BUS.addDevice(&sioZ, FUJI_DEVICEID_CPM);
    cassette()->set_buttons(Config.get_cassette_buttons());
    cassette()->set_pulldown(Config.get_cassette_pulldown());

    SYSTEM_BUS.addDevice(&_streamDev, FUJI_DEVICEID_MIDI);
}

// Set NetStream HOST, PORT, and options, then start it.
void sioFuji::sio_enable_netstream(const FujiSIOPacket &packet)
{
    char host[64];

    SYSTEM_BUS.transaction_accept(TRANS_STATE::WILL_GET);
    if (!SYSTEM_BUS.transaction_get(&host, sizeof(host)))
    {
        SYSTEM_BUS.transaction_error();
        return;
    }

    size_t host_len = 0;
    uint8_t flags = 0;
    uint8_t audf3 = 0;
    bool has_audf3 = false;
    char host_out[64];

    const char *nul = static_cast<const char *>(memchr(host, '\0', sizeof(host)));
    host_len = nul ? static_cast<size_t>(nul - host) : sizeof(host);

    if (nul != nullptr)
    {
        size_t nul_index = static_cast<size_t>(nul - host);
        if (nul_index + 1 < sizeof(host))
            flags = static_cast<uint8_t>(host[nul_index + 1]);
        if (nul_index + 2 < sizeof(host))
        {
            audf3 = static_cast<uint8_t>(host[nul_index + 2]);
            has_audf3 = true;
        }
    }

    int stream_mode = (flags & 0x01) ? 1 : 0;
    bool register_enabled = (flags & 0x02) != 0;
    bool tx_clock_external = (flags & 0x04) != 0;
    bool rx_clock_external = (flags & 0x08) != 0;
    bool video_pal = (flags & 0x10) != 0;

    size_t copy_len = host_len;
    if (copy_len > sizeof(host_out) - 1)
        copy_len = sizeof(host_out) - 1;
    memcpy(host_out, host, copy_len);
    host_out[copy_len] = '\0';

    uint16_t port = (static_cast<uint16_t>(packet.param8(0)) << 8) |
                    packet.param8(1);

    Debug_printf("Fuji cmd ENABLE NETSTREAM: HOST:%s PORT: %d\n", host_out, port);
#ifdef DEBUG_NETSTREAM
    Debug_printf("NETSTREAM opts: transport=%s register=%s flags=0x%02X audf3=%u\n",
                 (stream_mode == 0) ? "udp" : "tcp",
                 register_enabled ? "on" : "off",
                 flags,
                 audf3);
#endif

    Config.store_netstream_host(host_out);
    Config.store_netstream_port(port);
    Config.store_netstream_mode(stream_mode);
    Config.store_netstream_register(register_enabled);
    Config.save();

    SYSTEM_BUS.transaction_success();

    SYSTEM_BUS.setStreamHostWithOptions(host_out,
                                        port,
                                        stream_mode,
                                        register_enabled,
                                        has_audf3 ? audf3 : 0,
                                        video_pal,
                                        tx_clock_external,
                                        rx_clock_external,
                                        has_audf3);
}

// Set an external clock rate in kHz defined by speed in steps of 2kHz.
void sioFuji::fujicmd_set_sio_external_clock(uint16_t speed)
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);

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

    SYSTEM_BUS.transaction_success();
}

void sioFuji::sio_process(const FujiSIOPacket &packet)
{
    Debug_printf("sioFuji::fujicmd_process() called, baud: %d\n", SYSTEM_BUS.getBaudrate());

    // Let the base class handle standard commands
    if (fujiDevice::processCommand(packet))
        return;

    switch (packet.command())
    {
    case FUJICMD_HSIO_INDEX:
        /* ACK is required here since it's not done elsewhere for this device/command. The bus should probably
        * handle this instead. Disk and network devices currently send their own ACK for this
        */
        sio_high_speed();
        break;
    case FUJICMD_SET_HSIO_INDEX:
        sio_set_hsio_index(packet);
        break;
        break;
    case FUJICMD_SET_BAUDRATE:
        sio_set_baudrate(packet);
        break;
    case FUJICMD_NEW_DISK:
        sio_new_disk();
        break;
    case FUJICMD_COPY_FILE:
        sio_copy_file(packet);
        break;
    case FUJICMD_ENABLE_UDPSTREAM:
        sio_enable_netstream(packet);
        break;
    case FUJICMD_SET_SIO_EXTERNAL_CLOCK:
        fujicmd_set_sio_external_clock(packet.param(0));
        break;

    default:
        SYSTEM_BUS.transaction_error();
    }
}

success_is_true sioFuji::fujicore_mount_disk_image_success(uint8_t deviceSlot,
                                                           disk_access_flags_t access_mode)
{
    if (!fujiDevice::fujicore_mount_disk_image_success(deviceSlot, access_mode))
        RETURN_ERROR_AS_FALSE();
    status_wait_count = 0;
    RETURN_SUCCESS_AS_TRUE();
}

// Atari expects this field as a 32-bit little-endian value.
// fujiDevice::fujicmd_net_scan_networks only writes a single byte, so
// we override it here to pad/encode the same computed value as LE32.
void sioFuji::fujicmd_net_scan_networks()
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    Debug_println("Fuji cmd: SCAN NETWORKS");

    char ret[4] = {0};
    fujicore_net_scan_networks();
    ret[0] = _countScannedSSIDs;
    SYSTEM_BUS.transaction_send((uint8_t *)ret, 4, false);
}

ByteBuffer sioFuji::appkey_read()
{
    u16ne_t len;
    auto result = fujiDevice::appkey_read();
    len = htole16(result.size());
    result.resize(MAX_APPKEY_LEN, 0);
    const uint8_t *len_bytes = reinterpret_cast<const uint8_t*>(&len);
    result.insert(result.begin(), len_bytes, len_bytes + sizeof(len));
    return result;
}

void sioFuji::appkey_write(const FUJI_COMMAND_PACKET &packet)
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

#endif /* BUILD_ATARI */
