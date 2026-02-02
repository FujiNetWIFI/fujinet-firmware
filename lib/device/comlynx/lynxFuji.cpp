#ifdef BUILD_LYNX

#include "lynxFuji.h"

#include <cstring>

#include "../../include/debug.h"

#include "serial.h"

#include "fnSystem.h"
#include "fnConfig.h"
#include "fnWiFi.h"
#include "fsFlash.h"

#include "utils.h"
#include "string_utils.h"

#define IMAGE_EXTENSION ".ddp"
#define COPY_SIZE 532

lynxFuji platformFuji;
fujiDevice *theFuji = &platformFuji;        // global fuji device object
lynxNetwork *theNetwork; // global network device object (temporary)
lynxPrinter *thePrinter; // global printer
lynxSerial *theSerial;   // global serial

using namespace std;

// sioDisk sioDiskDevs[MAX_HOSTS];
// sioNetwork sioNetDevs[MAX_NETWORK_DEVICES];

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

// Status
void lynxFuji::comlynx_control_status()
{
    uint8_t r[6] = {0x8F, 0x00, 0x04, 0x00, 0x00, 0x04};
    comlynx_send_buffer(r, 6);
}

// Toggle boot config on/off, aux1=0 is disabled, aux1=1 is enabled
void lynxFuji::comlynx_set_boot_config()
{
    // does nothing on Lynx -SJ

    /*
    Debug_printf("Boot config is now %d",boot_config);

    boot_config = comlynx_recv();

    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }

    if (_fnDisks[0].disk_dev.is_config_device)
    {
        _fnDisks[0].disk_dev.unmount();
        _fnDisks[0].disk_dev.is_config_device = false;
        _fnDisks[0].reset();
        Debug_printf("Boot config unmounted slot 0");
    }

    comlynx_response_ack(); */
}

/*
 Write an "app key" to SD (ONLY!) storage.
*/
void lynxFuji::comlynx_write_app_key()
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
    return _set_additional_direntry_details(f, dest, maxlen, 100, SIZE_32_LE,
                                            HAS_DIR_ENTRY_FLAGS_SEPARATE, HAS_DIR_ENTRY_TYPE);
}

//  Make new disk and shove into device slot
void lynxFuji::comlynx_new_disk()
{
    uint8_t hs = comlynx_recv();
    uint8_t ds = comlynx_recv();
    uint32_t numBlocks;
    uint8_t *c = (uint8_t *)&numBlocks;
    uint8_t p[256];

    comlynx_recv_buffer(c, sizeof(uint32_t));
    comlynx_recv_buffer(p, 256);

    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }

    fujiDisk &disk = _fnDisks[ds];
    fujiHost &host = _fnHosts[hs];

    if (host.file_exists((const char *)p))
    {
        //SYSTEM_BUS.start_time = esp_timer_get_time();
        comlynx_response_ack();
        return;
    }
    disk.host_slot = hs;
    disk.access_mode = DISK_ACCESS_MODE_WRITE;
    strlcpy(disk.filename, (const char *)p, 256);

    disk.fileh = host.file_open(disk.filename, disk.filename, sizeof(disk.filename), "w");

    Debug_printf("Creating file %s on host slot %u mounting in disk slot %u numblocks: %lu\n", disk.filename, hs, ds, numBlocks);

    disk.disk_dev.write_blank(disk.fileh, numBlocks);

    fclose(disk.fileh);
    comlynx_response_ack();
}

void lynxFuji::comlynx_enable_device()
{
    fujiDeviceID_t d = (fujiDeviceID_t) comlynx_recv();
    Debug_printf("FUJI ENABLE DEVICE %02x\n",d);

    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }

    switch(d)
    {
    case FUJI_DEVICEID_PRINTER:
        Config.store_printer_enabled(true);
        break;
    case FUJI_DEVICEID_DISK:
        Config.store_device_slot_enable_1(true);
        break;
    case FUJI_DEVICEID_DISK2:
        Config.store_device_slot_enable_2(true);
        break;
    case FUJI_DEVICEID_DISK3:
        Config.store_device_slot_enable_3(true);
        break;
    case FUJI_DEVICEID_DISK4:
        Config.store_device_slot_enable_4(true);
        break;
    default:
        break;
    }

    Config.save();
    SYSTEM_BUS.enableDevice(d);
    comlynx_response_ack();
}

void lynxFuji::comlynx_disable_device()
{
    fujiDeviceID_t d = (fujiDeviceID_t) comlynx_recv();
    Debug_printf("FUJI DISABLE DEVICE %02x\n",d);

    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }

    switch(d)
    {
    case FUJI_DEVICEID_PRINTER:
        Config.store_printer_enabled(false);
        break;
    case FUJI_DEVICEID_DISK:
        Config.store_device_slot_enable_1(false);
        break;
    case FUJI_DEVICEID_DISK2:
        Config.store_device_slot_enable_2(false);
        break;
    case FUJI_DEVICEID_DISK3:
        Config.store_device_slot_enable_3(false);
        break;
    case FUJI_DEVICEID_DISK4:
        Config.store_device_slot_enable_4(false);
        break;
    default:
        break;
    }

    Config.save();
    SYSTEM_BUS.disableDevice(d);
    comlynx_response_ack();
}

// Initializes base settings and adds our devices to the SIO bus
void lynxFuji::setup()
{
    populate_slots_from_config();

    // Disable booting from CONFIG if our settings say to turn it off
    boot_config = false;

    SYSTEM_BUS.addDevice(&_fnDisks[0].disk_dev, FUJI_DEVICEID_DISK);
    SYSTEM_BUS.addDevice(theFuji, FUJI_DEVICEID_FUJINET);   // Fuji becomes the gateway device.
    theNetwork = new lynxNetwork();
    SYSTEM_BUS.addDevice(theNetwork, FUJI_DEVICEID_NETWORK);
}

void lynxFuji::comlynx_random_number()
{
    int *p = (int *)&response[0];

    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }

    response_len = sizeof(int);
    *p = rand();

    comlynx_response_ack();
}

void lynxFuji::comlynx_get_time()
{
    Debug_println("FUJI GET TIME");

    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }

    time_t tt = time(nullptr);

    setenv("TZ",Config.get_general_timezone().c_str(),1);
    tzset();

    struct tm * now = localtime(&tt);

    now->tm_mon++;
    now->tm_year-=100;

    response[0] = now->tm_mday;
    response[1] = now->tm_mon;
    response[2] = now->tm_year;
    response[3] = now->tm_hour;
    response[4] = now->tm_min;
    response[5] = now->tm_sec;

    response_len = 6;

    Debug_printf("Sending %02X %02X %02X %02X %02X %02X\n",now->tm_mday, now->tm_mon, now->tm_year, now->tm_hour, now->tm_min, now->tm_sec);
    comlynx_response_ack();
}

void lynxFuji::comlynx_device_enable_status()
{
    fujiDeviceID_t d = (fujiDeviceID_t) comlynx_recv();

    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }

    if (SYSTEM_BUS.deviceExists(d))
        comlynx_response_ack();
    else
        comlynx_response_nack();

    response_len=1;
    response[0]=SYSTEM_BUS.deviceEnabled(d);
}

void lynxFuji::comlynx_hello()
{
    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }

    const char resp[] = "HI FROM FUJINET!\n";
    response_len = strlen(resp);
    memcpy(response,resp,response_len);

    Debug_printf("lynxFuji::comlynx_hello()\n");
    comlynx_response_ack();

    Debug_printf("HELLO FROM LYNX.\n");

}

void lynxFuji::comlynx_control_send()
{
    // Reset the recvbuffer
    recvbuffer_len = 0;         // happens in recv_length, but may remove from there -SJ

    uint16_t s = comlynx_recv_length();
    uint8_t c = comlynx_recv();

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
        fujicmd_net_scan_result(comlynx_recv());
        break;
    case FUJICMD_SET_SSID:
        {
            SSIDConfig cfg;
            comlynx_recv_buffer((uint8_t *)&cfg, s);
            fujicmd_net_set_ssid_success(cfg.ssid, cfg.password, false);
        }
        break;
    case FUJICMD_GET_WIFISTATUS:
        fujicmd_net_get_wifi_status();
        break;
    case FUJICMD_MOUNT_HOST:
        fujicmd_mount_host_success(comlynx_recv());
        break;
    case FUJICMD_UNMOUNT_HOST:
        fujicmd_unmount_host_success(comlynx_recv());
        break;
    case FUJICMD_MOUNT_IMAGE:
        {
            uint8_t slot = comlynx_recv();
            uint8_t mode = comlynx_recv();
            fujicmd_mount_disk_image_success(slot, (disk_access_flags_t) mode);
        }
        break;
    case FUJICMD_OPEN_DIRECTORY:
        fujicmd_open_directory_success(comlynx_recv());
        break;
    case FUJICMD_READ_DIR_ENTRY:
        {
            uint8_t maxlen = comlynx_recv();
            uint8_t addtl = comlynx_recv();
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
        fujicmd_unmount_disk_image_success(comlynx_recv());
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
            uint16_t pos = 0;
            comlynx_recv_buffer((uint8_t *)&pos, sizeof(uint16_t));
            fujicmd_set_directory_position(pos);
        }
        break;
    case FUJICMD_SET_DEVICE_FULLPATH:
        {
            uint8_t deviceSlot = comlynx_recv();
            char filename[256];
            transaction_get(filename, s - 2);
            fujicore_set_device_filename_success(deviceSlot, _fnDisks[deviceSlot].host_slot,
                                                 _fnDisks[deviceSlot].access_mode,
                                                 std::string(filename, s - 2));
        }
        break;
    case FUJICMD_GET_DEVICE_FULLPATH:
        fujicmd_get_device_filename(comlynx_recv());
        break;
    case FUJICMD_CONFIG_BOOT:
        comlynx_set_boot_config();
        break;
    case FUJICMD_ENABLE_DEVICE:
        comlynx_enable_device();
        break;
    case FUJICMD_DISABLE_DEVICE:
        comlynx_disable_device();
        break;
    case FUJICMD_MOUNT_ALL:
        fujicmd_mount_all_success();
        break;
    case FUJICMD_SET_BOOT_MODE:
        fujicmd_set_boot_mode(comlynx_recv(), MEDIATYPE_UNKNOWN, &bootdisk);
        break;
    case FUJICMD_WRITE_APPKEY:
        comlynx_write_app_key();
        break;
    case FUJICMD_READ_APPKEY:
        fujicmd_read_app_key();
        break;
    case FUJICMD_RANDOM_NUMBER:
        comlynx_random_number();
        break;
    case FUJICMD_GET_TIME:
        comlynx_get_time();
        break;
    case FUJICMD_DEVICE_ENABLE_STATUS:
        comlynx_device_enable_status();
        break;
    case FUJICMD_COPY_FILE:
        {
            uint8_t source = comlynx_recv();
            uint8_t dest = comlynx_recv();
            char dirpath[256];
            transaction_get(dirpath, sizeof(dirpath));
            fujicmd_copy_file_success(source, dest, dirpath);
        }
        break;
    case FUJICMD_ENABLE_UDPSTREAM:
        fujicmd_enable_udpstream(s);
        break;
    case FUJICMD_GENERATE_GUID:
        fujicmd_generate_guid();
        break;
    case 0x01:
        comlynx_hello();
        break;
    }
}

void lynxFuji::comlynx_control_clr()
{
    uint8_t b;

    comlynx_send(0xBF);
    comlynx_send_length(response_len);
    comlynx_send_buffer(response, response_len);
    comlynx_send(comlynx_checksum(response, response_len));
    b = comlynx_recv();             // get the ack or nack
    // ignore response from Lynx, if they didn't receive the data properly
    // they should resend the entire command -SJ

    Debug_printf("comlynx_control_clr: %02X\n", b);

    // Reset response buffer
    memset(response, 0, sizeof(response));
    response_len = 0;
}

void lynxFuji::comlynx_process(uint8_t b)
{
    unsigned char c = b >> 4;
    //Debug_printf("%02x \n",c);

    switch (c)
    {
    case MN_STATUS:
        comlynx_control_status();
        break;
    case MN_CLR:
        comlynx_control_clr();
        break;
    case MN_RECEIVE:
        comlynx_response_ack();
        break;
    case MN_SEND:
        comlynx_control_send();
        break;
    case MN_READY:
        comlynx_control_ready();
        break;
    }
}

#endif /* BUILD_LYNX */
