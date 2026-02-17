#ifdef BUILD_LYNX

#include "lynxFuji.h"

#include <cstring>

#include "../../include/debug.h"

//#include "serial.h"

#include "fnSystem.h"
#include "fnConfig.h"
#include "fnWiFi.h"
#include "fsFlash.h"

#include "utils.h"
#include "string_utils.h"

#define IMAGE_EXTENSION ".lnx"
#define COPY_SIZE 532

lynxFuji platformFuji;
fujiDevice *theFuji = &platformFuji;        // global fuji device object
lynxNetwork *theNetwork;                    // global network device object (temporary)
lynxPrinter *thePrinter;                    // global printer
//lynxSerial *theSerial;                      // global serial

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
}


void lynxFuji::transaction_complete()
{
    Debug_println("transaction_complete - sent ACK");
    comlynx_response_ack();
}

void lynxFuji::transaction_error()
{
    Debug_println("transaction_error - send NAK");
    comlynx_response_nack();
    
    // throw away any waiting bytes
    while (SYSTEM_BUS.available() > 0)
        SYSTEM_BUS.read();
}
    
bool lynxFuji::transaction_get(void *data, size_t len) 
{
    size_t remaining = recvbuffer_len - (recvbuf_pos - recvbuffer);
    size_t to_copy = (len > remaining) ? remaining : len;

    memcpy(data, recvbuf_pos, to_copy);
    recvbuf_pos += to_copy;

    return len;
}


void lynxFuji::transaction_put(const void *data, size_t len, bool err)
{
    uint8_t b;

    // set response buffer
    memcpy(response, data, len);
    response_len = len;

    // send all data back to Lynx
    uint8_t ck = comlynx_checksum(response, response_len);
    comlynx_send_length(response_len);
    comlynx_send_buffer(response, response_len);
    comlynx_send(ck);

    // get ACK or NACK from Lynx, we're ignoring currently
    //uint8_t t = comlynx_recv_timeout(&b, 8000);
    uint8_t r = comlynx_recv();
    #ifdef DEBUG
        //if (!t)
            if (r == FUJICMD_ACK)
                Debug_println("transaction_put - Lynx ACKed");
            else
                Debug_println("transaction put - Lynx NAKed");
        //else
        //    Debug_println("transaction_put - timed out waiting for ACK/NAK from Lynx");
    #endif

    return;
}

/*
 Write an "app key" to SD (ONLY!) storage.
*/
/*void lynxFuji::comlynx_write_app_key()
{
    uint16_t creator = comlynx_recv_length();
    uint8_t app = comlynx_recv();
    uint8_t key = comlynx_recv();
    uint8_t data[64];
    char appkeyfilename[30];
    FILE *fp;

    Debug_printf("Fuji Cmd: WRITE APPKEY %s\n", appkeyfilename);

    snprintf(appkeyfilename, sizeof(appkeyfilename), "/FujiNet/%04hx%02hhx%02hhx.key", creator, app, key);

    comlynx_recv_buffer(data, 64);

    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }

    fp = fnSDFAT.file_open(appkeyfilename, "w");
    if (fp == nullptr)
    {
        Debug_printf("Could not open.\n");
        return;
    }

    fwrite(data, sizeof(uint8_t), sizeof(data), fp);
    fclose(fp);

    comlynx_response_ack();
}*/

// This gets called when we're about to shutdown/reboot
void lynxFuji::shutdown()
{
    for (int i = 0; i < MAX_DISK_DEVICES; i++)
        _fnDisks[i].disk_dev.unmount();
}

size_t lynxFuji::set_additional_direntry_details(fsdir_entry_t *f, uint8_t *dest,
                                                 uint8_t maxlen)
{
    return _set_additional_direntry_details(f, dest, maxlen, 100, SIZE_32_LE,
                                            HAS_DIR_ENTRY_FLAGS_SEPARATE, HAS_DIR_ENTRY_TYPE);
}

//  Make new disk and shove into device slot
void lynxFuji::comlynx_new_disk()
{
    uint8_t hs;
    uint8_t ds;
    uint32_t numBlocks;
    //uint8_t *c = (uint8_t *)&numBlocks;
    uint8_t p[256];

    transaction_get(&hs, sizeof(hs));
    transaction_get(&ds, sizeof(ds));
    transaction_get(&numBlocks, sizeof(numBlocks));
    transaction_get(&p, 256);

    fujiDisk &disk = _fnDisks[ds];
    fujiHost &host = _fnHosts[hs];

    if (host.file_exists((const char *)p))
    {
        //comlynx_response_ack();
        transaction_complete();
        return;
    }

    disk.host_slot = hs;
    disk.access_mode = DISK_ACCESS_MODE_WRITE;
    strlcpy(disk.filename, (const char *)p, 256);

    disk.fileh = host.file_open(disk.filename, disk.filename, sizeof(disk.filename), "w");

    Debug_printf("Creating file %s on host slot %u mounting in disk slot %u numblocks: %lu\n", disk.filename, hs, ds, numBlocks);

    disk.disk_dev.write_blank(disk.fileh, numBlocks);
    fclose(disk.fileh);

    //comlynx_response_ack();
    transaction_complete();
}

// Initializes base settings and adds our devices to the SIO bus
void lynxFuji::setup()
{
    populate_slots_from_config();

    // Disable booting from CONFIG if our settings say to turn it off
    boot_config = false;

    // Add our devices to the bus
    for (int i = 0; i < MAX_DISK_DEVICES; i++)
        SYSTEM_BUS.addDevice(&_fnDisks[i].disk_dev, (fujiDeviceID_t) (FUJI_DEVICEID_DISK + i));

    //SYSTEM_BUS.addDevice(&_fnDisks[0].disk_dev, FUJI_DEVICEID_DISK);
    SYSTEM_BUS.addDevice(theFuji, FUJI_DEVICEID_FUJINET);   // Fuji becomes the gateway device.
    
    theNetwork = new lynxNetwork();
    SYSTEM_BUS.addDevice(theNetwork, FUJI_DEVICEID_NETWORK);
}

void lynxFuji::fujicmd_random_number()
{
    int p;
    p = rand();
    transaction_put(&p, sizeof(p));
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

    transaction_put(time_resp, sizeof(time_resp));
    Debug_printf("comlynx_get_time - Sending %02d/%02d/%02d %02d:%02d:%02d\n",now->tm_mon, now->tm_mday, now->tm_year, now->tm_hour, now->tm_min, now->tm_sec);
}

void lynxFuji::comlynx_process()
{
    uint8_t c;
    uint8_t slot;


    // Reset the recvbuffer
    //recvbuffer_len = 0;         // happens in recv_length, but may remove from there -SJ
    
    // Get the entire payload from Lynx
    uint16_t len = comlynx_recv_length();
    Debug_printf("lynxFuji::comlynx_process - len: %ld, ", len);

    comlynx_recv_buffer(recvbuffer, len);
    if (comlynx_recv_ck()) {
        Debug_printf("checksum good\n");
        comlynx_response_ack();        // good checksum
    }
    else {
        Debug_printf(" checksum bad\n");
        comlynx_response_nack();       // good checksum
        return;
    }

    // get command
    transaction_get(&c, sizeof(c));
    Debug_printf("lynxFuji::process - command: %02X\n", c);

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
        int8_t index;
        transaction_get(&index, sizeof(index));
        fujicmd_net_scan_result(index);
        break;
    case FUJICMD_SET_SSID:
        {
            SSIDConfig cfg;
            transaction_get(&cfg, sizeof(cfg));
            fujicmd_net_set_ssid_success(cfg.ssid, cfg.password, false);
        }
        break;
    case FUJICMD_GET_WIFISTATUS:
        fujicmd_net_get_wifi_status();
        break;
    case FUJICMD_MOUNT_HOST:   
        transaction_get(&slot, sizeof(slot));
        fujicmd_mount_host_success(slot);
        break;
    case FUJICMD_UNMOUNT_HOST:
        transaction_get(&slot, sizeof(slot));
        fujicmd_unmount_host_success(slot);
        break;
    case FUJICMD_MOUNT_IMAGE:
        {
            uint8_t mode;
            transaction_get(&slot, sizeof(slot));
            transaction_get(&mode, sizeof(mode));
            fujicmd_mount_disk_image_success(slot, (disk_access_flags_t) mode);
        }
        break;
    case FUJICMD_OPEN_DIRECTORY:
        transaction_get(&slot, sizeof(slot));
        fujicmd_open_directory_success(slot);
        break;
    case FUJICMD_READ_DIR_ENTRY:
        {
            uint8_t maxlen;
            uint8_t addtl;
            transaction_get(&maxlen, sizeof(maxlen));
            transaction_get(&addtl, sizeof(addtl));
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
        transaction_get(&slot, sizeof(slot));
        fujicmd_unmount_disk_image_success(slot);
        break;
    case FUJICMD_GET_ADAPTERCONFIG:
        fujicmd_get_adapter_config();
        break;
    case FUJICMD_NEW_DISK:
        comlynx_new_disk();
        break;
    case FUJICMD_GET_DIRECTORY_POSITION:
        fujicmd_get_directory_position();
        break;
    case FUJICMD_SET_DIRECTORY_POSITION:
        {
            int16_t pos;
            transaction_get(&pos, sizeof(pos));
            fujicmd_set_directory_position(pos);
        }
        break;
    case FUJICMD_SET_DEVICE_FULLPATH:
        {
            uint8_t deviceSlot;
            transaction_get(&deviceSlot, sizeof(deviceSlot));
            char filename[256];
            transaction_get(filename, len - 2);
            fujicore_set_device_filename_success(deviceSlot, _fnDisks[deviceSlot].host_slot,
                                                 _fnDisks[deviceSlot].access_mode,
                                                 std::string(filename, sizeof(filename)));
        }
        break;
    case FUJICMD_GET_DEVICE_FULLPATH:
        transaction_get(&slot, sizeof(slot));
        fujicmd_get_device_filename(slot);
        break;
    /*case FUJICMD_CONFIG_BOOT:
        comlynx_set_boot_config();
        break;
    case FUJICMD_ENABLE_DEVICE:
        comlynx_enable_device();
        break;
    case FUJICMD_DISABLE_DEVICE:
        comlynx_disable_device();
        break;*/
    case FUJICMD_MOUNT_ALL:
        fujicmd_mount_all_success();
        break;
    /*case FUJICMD_SET_BOOT_MODE:
        fujicmd_set_boot_mode(*recvbuf_pos, MEDIATYPE_UNKNOWN, &bootdisk);
        break;*/
    case FUJICMD_WRITE_APPKEY:
        fujicmd_write_app_key(0, 0);
        break;
    case FUJICMD_READ_APPKEY:
        fujicmd_read_app_key();
        break;
    case FUJICMD_RANDOM_NUMBER:
        fujicmd_random_number();
        break;
    case FUJICMD_GET_TIME:
        fujicmd_get_time();
        break;
    /*case FUJICMD_DEVICE_ENABLE_STATUS:
        comlynx_device_enable_status();
        break;*/
    case FUJICMD_COPY_FILE:
        {
            uint8_t source;
            uint8_t dest;
            char dirpath[256];
            transaction_get(&source, sizeof(source));
            transaction_get(&dest, sizeof(dest));
            transaction_get(dirpath, len - 3);
            fujicmd_copy_file_success(source, dest, dirpath);
        }
        break;
    case FUJICMD_ENABLE_UDPSTREAM:
        uint16_t port;
        transaction_get(&port, sizeof(port));
        fujicmd_enable_udpstream(port);
        break;
    default:
        Debug_printf("lynxFuji::process - unknown command: %02X\n", c);
        transaction_error();
        break;
    }
}

#endif /* BUILD_LYNX */
