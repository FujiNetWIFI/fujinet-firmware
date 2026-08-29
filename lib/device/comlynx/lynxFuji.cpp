#ifdef BUILD_LYNX

#include "lynxFuji.h"
#include "compat_string.h"
#ifdef ESP_PLATFORM
#include <PSRAMAllocator.h>
#endif /* ESP_PLATFORM */

#ifdef _WIN32
#define setenv(name, value, overwrite) _putenv_s(name, value)
#define unsetenv(name) _putenv_s(name, "")
#endif /* _WIN32 */

#define IMAGE_EXTENSION ".lnx"
#define COPY_SIZE 532

lynxFuji platformFuji;
fujiDevice *theFuji = &platformFuji;        // global fuji device object
lynxPrinter *thePrinter;                    // global printer

#ifdef ESP_PLATFORM
std::unique_ptr<lynxNetwork, PSRAMDeleter<lynxNetwork>> lynxNetDevs[MAX_NETWORK_DEVICES];
#else
std::unique_ptr<lynxNetwork> lynxNetDevs[MAX_NETWORK_DEVICES];
#endif

using namespace std;


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

// Constructor
lynxFuji::lynxFuji() : fujiDevice(MAX_DISK_DEVICES, IMAGE_EXTENSION, std::nullopt)
{
    // Helpful for debugging
    for (int i = 0; i < MAX_HOSTS; i++)
        _fnHosts[i].slotid = i;

    #ifdef ESP_PLATFORM
    for (int i = 0; i < MAX_NETWORK_DEVICES; ++i)
    {
        PSRAMAllocator<lynxNetwork> allocator;
        lynxNetwork* ptr = allocator.allocate(1); // Allocate memory for one sioNetwork object

        if (ptr != nullptr)
        {
            new (ptr) lynxNetwork(); // Construct the object using placement new
            lynxNetDevs[i] = std::unique_ptr<lynxNetwork, PSRAMDeleter<lynxNetwork>>(ptr); // Store in smart pointer
        }
    }
    #else
    for (int i = 0; i < MAX_NETWORK_DEVICES; i++)
    {
        lynxNetwork *ptr = (lynxNetwork *) malloc(sizeof(lynxNetwork));
        if (ptr != nullptr) {
            new (ptr) lynxNetwork();
            lynxNetDevs[i] = std::unique_ptr<lynxNetwork>(ptr);
        }
    }
    #endif
}

// This gets called when we're about to shutdown/reboot
void lynxFuji::shutdown()
{
    for (int i = 0; i < MAX_DISK_DEVICES; i++)
        _fnDisks[i].disk_dev.unmount();
}

size_t lynxFuji::set_additional_direntry_details(fsdir_entry_t *f, uint8_t *dest,
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
void lynxFuji::comlynx_new_disk(const FujiLynxPacket &packet)
{
    uint8_t hs = packet.param(0);
    uint8_t ds = packet.param(1);
    u32le_t numBlocks;
    const char *ptr;

    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);

    ptr = packet.dataAsString()->c_str();
    memcpy(&numBlocks, ptr, sizeof(numBlocks));
    ptr += sizeof(numBlocks);

    fujiDisk &disk = _fnDisks[ds];
    fujiHost &host = _fnHosts[hs];

    if (host.file_exists((const char *)ptr))
    {
        SYSTEM_BUS.transaction_success();
        return;
    }

    disk.host_slot = hs;
    disk.access_mode = DISK_ACCESS_MODE_WRITE;
    strlcpy(disk.filename, (const char *)ptr, sizeof(disk.filename));

    disk.fileh = host.file_open(disk.filename, disk.filename, sizeof(disk.filename), "w");

    Debug_printf("Creating file %s on host slot %u mounting in disk slot %u numblocks: %lu\n", disk.filename, hs, ds, (long unsigned) numBlocks);

    disk.disk_dev.write_blank(disk.fileh, numBlocks);
    fclose(disk.fileh);

    SYSTEM_BUS.transaction_success();
}

// Initializes base settings and adds our devices to the SIO bus
void lynxFuji::setup()
{
    populate_slots_from_config();

    // Disable booting from CONFIG if our settings say to turn it off
    boot_config = false;

    // Add our devices to the bus
    SYSTEM_BUS.addDevice(theFuji, FUJI_DEVICEID::FUJINET);   // Fuji becomes the gateway device.

    for (int i = 0; i < MAX_DISK_DEVICES; i++)
        SYSTEM_BUS.addDevice(&_fnDisks[i].disk_dev, (fujiDeviceID_t) (FUJI_DEVICEID::DISK + i));

    for (int i = 0; i < MAX_NETWORK_DEVICES; i++)
        SYSTEM_BUS.addDevice(lynxNetDevs[i].get(), (fujiDeviceID_t) (FUJI_DEVICEID::NETWORK + i));

    SYSTEM_BUS.addDevice(&_streamDev, FUJI_DEVICEID::MIDI);
}

void lynxFuji::fujicmd_get_time()
{
    uint8_t time_resp[6];

    Debug_println("Fuji cmd: GET TIME");

    time_t tt = time(nullptr);

    setenv("TZ",Config.get_general_timezone().c_str(),1);
    tzset();

    struct tm * now = localtime(&tt);

    now->tm_mon++;
    now->tm_year-=100;

    time_resp[0] = now->tm_mday;
    time_resp[1] = now->tm_mon;
    time_resp[2] = now->tm_year;
    time_resp[3] = now->tm_hour;
    time_resp[4] = now->tm_min;
    time_resp[5] = now->tm_sec;

    SYSTEM_BUS.transaction_send(time_resp, sizeof(time_resp));
    Debug_printf("comlynx_get_time - Sending %02d/%02d/%02d %02d:%02d:%02d\n",now->tm_mon, now->tm_mday, now->tm_year, now->tm_hour, now->tm_min, now->tm_sec);
}

void lynxFuji::fujicmd_enable_netstream(int port, size_t host_payload_len)
{
    char host[64] = {};

    SYSTEM_BUS.transaction_accept(TRANS_STATE::WILL_GET);
    if (!SYSTEM_BUS.transaction_get(&host, sizeof(host)))
    {
        SYSTEM_BUS.transaction_error();
        return;
    }

    const char *nul = static_cast<const char *>(memchr(host, '\0', sizeof(host)));
    size_t host_len = nul ? static_cast<size_t>(nul - host) : sizeof(host);
    bool has_flags = false;
    uint8_t flags = 0;

    if (nul != nullptr)
    {
        size_t nul_index = static_cast<size_t>(nul - host);
        has_flags = (host_payload_len > nul_index + 1) &&
                    (host_payload_len < sizeof(host) || host[nul_index + 1] != 0);
        if (has_flags && nul_index + 1 < sizeof(host))
            flags = static_cast<uint8_t>(host[nul_index + 1]);
    }

    int stream_mode = (flags & 0x01) ? 1 : 0;
    bool register_enabled = (flags & 0x02) != 0;
    bool redeye_enabled = has_flags ? ((flags & 0x04) != 0) : true;

    char host_out[64];
    size_t copy_len = host_len;
    if (copy_len > sizeof(host_out) - 1)
        copy_len = sizeof(host_out) - 1;
    memcpy(host_out, host, copy_len);
    host_out[copy_len] = '\0';

    Debug_printf("Fuji cmd ENABLE NETSTREAM: HOST:%s PORT: %d\n", host_out, port);

    Config.store_netstream_host(host_out);
    Config.store_netstream_port(port);
    Config.store_netstream_mode(stream_mode);
    Config.store_netstream_register(register_enabled);
    Config.save();

    SYSTEM_BUS.transaction_success();

    SYSTEM_BUS.setStreamHostWithOptions(host_out, port, stream_mode, register_enabled, redeye_enabled);
}

void lynxFuji::comlynx_process(const FujiLynxPacket &packet)
{
    Debug_printf("lynxFuji::process - command: %02X\n", (uint8_t) packet.command());

    // Let the base class handle standard commands
    if (fujiDevice::processCommand(packet))
        return;

    switch (packet.command())
    {
    case CMD::FUJI_ENABLE_UDPSTREAM:
        {
            uint16_t port = packet.param(0);
            fujicmd_enable_netstream(port, packet.data()->size());
        }
        break;
    default:
        Debug_printf("lynxFuji::process - unknown command: %02X\n", (uint8_t) packet.command());
        SYSTEM_BUS.transaction_error();
        break;
    }
}

#endif /* BUILD_LYNX */
