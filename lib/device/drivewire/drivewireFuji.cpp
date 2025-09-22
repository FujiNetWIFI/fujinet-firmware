#ifdef BUILD_COCO

#include "drivewireFuji.h"
#include "network.h"
#include "fnWiFi.h"
#include "base64.h"
#include "utils.h"
#include "compat_string.h"
#include <endian.h>

#ifdef UNUSED
#ifdef ESP_PLATFORM
#include <driver/ledc.h>
#else
#include <libgen.h>
#endif

#include <cstdint>
#include <cstring>

#include "../../../include/debug.h"

#include "fnSystem.h"
#include "fnConfig.h"
#include "fsFlash.h"

#include "led.h"
#include "string_utils.h"

#include "../../encoding/base64.h"
#include "../../encoding/hash.h"

#define ADDITIONAL_DETAILS_BYTES 13
#endif /* UNUSED */

drivewireFuji platformFuji;
fujiDevice *theFuji = &platformFuji; // Global fuji object.

// drivewireDisk drivewireDiskDevs[MAX_HOSTS];
drivewireNetwork drivewireNetDevs[MAX_NETWORK_DEVICES];

#ifdef NOT_SUBCLASS
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
#endif /* NOT_SUBCLASS */

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
drivewireFuji::drivewireFuji()
{
    // Helpful for debugging
    for (int i = 0; i < MAX_HOSTS; i++)
        _fnHosts[i].slotid = i;
}

#ifdef NOT_SUBCLASS
// Reset FujiNet
void drivewireFuji::reset_fujinet()
{
    Debug_println("Fuji cmd: REBOOT");
    // drivewire_complete();
    fnSystem.reboot();
}
#endif /* NOT_SUBCLASS */

#ifdef NOT_SUBCLASS
// Scan for networks
void drivewireFuji::net_scan_networks()
{
    Debug_println("Fuji cmd: SCAN NETWORKS");

    if (!wifiScanStarted)
    {
        wifiScanStarted = true;
        _countScannedSSIDs = fnWiFi.scan_networks();
    }

#ifdef NOT_TRANSACTION
    response.clear();
    response.shrink_to_fit();

    response += _countScannedSSIDs;
#else
    transaction_put(&_countScannedSSIDs, 1);
#endif /* NOT_TRANSACTION */
}
#endif /* NOT_SUBCLASS */

#ifdef NOT_SUBCLASS
// Return scanned network entry
void drivewireFuji::net_scan_result()
{
    Debug_println("Fuji cmd: GET SCAN RESULT");

    uint8_t n = fnDwCom.read();

    wifiScanStarted = false;

    // Response to  FUJICMD_GET_SCAN_RESULT
    struct
    {
        char ssid[MAX_SSID_LEN + 1];
        uint8_t rssi;
    } detail;

    if (n < _countScannedSSIDs)
        fnWiFi.get_scan_result(n, detail.ssid, &detail.rssi);
    else
    {
        memset(&detail, 0, sizeof(detail));
#ifdef NOT_TRANSACTION
        errorCode = 144;
#else
        transaction_error();
#endif /* NOT_TRANSACTION */
    }

#ifdef NOT_TRANSACTION
    response.clear();
    response.shrink_to_fit();

    response = std::string((const char *)&detail, sizeof(detail));

    errorCode = 1;
#else
    transaction_put(&detail, sizeof(detail));
#endif /* NOT_TRANSACTION */
}
#endif /* NOT_SUBCLASS */

#ifdef NOT_SUBCLASS
//  Get SSID
void drivewireFuji::net_get_ssid()
{
    Debug_println("Fuji cmd: GET SSID");

    // Response to  FUJICMD_GET_SSID
    struct
    {
        char ssid[MAX_SSID_LEN + 1];
        char password[MAX_WIFI_PASS_LEN];
    } cfg;

    memset(&cfg, 0, sizeof(cfg));

    /*
     We memcpy instead of strcpy because technically the SSID and phasephrase aren't strings and aren't null terminated,
     they're arrays of bytes officially and can contain any byte value - including a zero - at any point in the array.
     However, we're not consistent about how we treat this in the different parts of the code.
    */
    std::string s = Config.get_wifi_ssid();
    memcpy(cfg.ssid, s.c_str(),
           s.length() > sizeof(cfg.ssid) ? sizeof(cfg.ssid) : s.length());

    s = Config.get_wifi_passphrase();
    memcpy(cfg.password, s.c_str(),
           s.length() > sizeof(cfg.password) ? sizeof(cfg.password) : s.length());

#ifdef NOT_TRANSACTION
    response.clear();
    response.shrink_to_fit();

    response = std::string((const char *)&cfg, sizeof(cfg));

    errorCode = 1;
#else
    transaction_put(&cfg, sizeof(cfg));
#endif /* NOT_TRANSACTION */
}
#endif /* NOT_SUBCLASS */

#ifdef NOT_SUBCLASS
// Set SSID
void drivewireFuji::net_set_ssid()
{
    Debug_printf("\r\nFuji cmd: SET SSID");
    struct
    {
        char ssid[MAX_SSID_LEN + 1];
        char password[MAX_WIFI_PASS_LEN];
    } cfg;

#ifdef NOT_TRANSACTION
    fnDwCom.readBytes((uint8_t *)&cfg, sizeof(cfg));
#else
    transaction_get(&cfg, sizeof(cfg));
#endif /* NOT_TRANSACTION */

    bool save = false; // for now don't save - to do save if connection was succesful

    // URL Decode SSID/PASSWORD to handle special chars (FIXME)
    //mstr::urlDecode(cfg.ssid, sizeof(cfg.ssid));
    //mstr::urlDecode(cfg.password, sizeof(cfg.password));

    Debug_printf("\r\nConnecting to net: %s password: %s\n", cfg.ssid, cfg.password);

    if (fnWiFi.connect(cfg.ssid, cfg.password) == ESP_OK)
    {
        Config.store_wifi_ssid(cfg.ssid, sizeof(cfg.ssid));
        Config.store_wifi_passphrase(cfg.password, sizeof(cfg.password));
    }

    // Only save these if we're asked to, otherwise assume it was a test for connectivity
    // should only save if connection was successful - i think
    if (save)
    {
        Config.save();
    }
}
#endif /* NOT_SUBCLASS */

#ifdef NOT_SUBCLASS
// Get WiFi Status
void drivewireFuji::net_get_wifi_status()
{
    uint8_t wifiStatus = fnWiFi.connected() ? 3 : 6;
    Debug_printv("Fuji cmd: GET WIFI STATUS: %u", wifiStatus);

#ifdef NOT_TRANSACTION
    response.clear();
    response.shrink_to_fit();

    response += wifiStatus;

    errorCode = 1;
#else
    transaction_put(&wifiStatus, 1);
#endif /* NOT_TRANSACTION */
}
#endif /* NOT_SUBCLASS */

#ifdef NOT_SUBCLASS
// Check if Wifi is enabled
void drivewireFuji::net_get_wifi_enabled()
{
    uint8_t e = Config.get_wifi_enabled() ? 1 : 0;

    Debug_printv("Fuji cmd: GET WIFI ENABLED: %u", e);

#ifdef NOT_TRANSACTION
    response.clear();
    response.shrink_to_fit();

    response += e;

    errorCode = 1; // Set it anyway.
#else
    transaction_put(&e, 1);
#endif /* NOT_TRANSACTION */
}
#endif /* NOT_SUBCLASS */

#ifdef NOT_SUBCLASS
// Mount Server
void drivewireFuji::mount_host()
{
    Debug_println("Fuji cmd: MOUNT HOST");

    unsigned char hostSlot = fnDwCom.read();

    _fnHosts[hostSlot].mount();
}
#endif /* NOT_SUBCLASS */

#ifdef NOT_SUBCLASS
// Disk Image Mount
void drivewireFuji::disk_image_mount()
{
    // TAPE or CASSETTE handling: this function can also mount CAS and WAV files
    // to the C: device. Everything stays the same here and the mounting
    // where all the magic happens is done in the drivewireDisk::mount() function.
    // This function opens the file, so cassette does not need to open the file.
    // Cassette needs the file pointer and file size.

    Debug_println("Fuji cmd: MOUNT IMAGE");

    uint8_t deviceSlot = fnDwCom.read();
    uint8_t options = fnDwCom.read(); // DISK_ACCESS_MODE

#ifdef NOT_TRANSACTION
    errorCode = 1;
#else
    transaction_complete();
#endif /* NOT_TRANSACTION */

    // TODO: Implement FETCH?
    char flag[3] = {'r', 0, 0};
    if (options == DISK_ACCESS_MODE_WRITE)
        flag[1] = '+';

    // A couple of reference variables to make things much easier to read...
    fujiDisk &disk = _fnDisks[deviceSlot];
    fujiHost &host = _fnHosts[disk.host_slot];

    Debug_printf("Selecting '%s' from host #%u as %s on D%u:\n",
                 disk.filename, disk.host_slot, flag, deviceSlot + 1);

    // TODO: Refactor along with mount disk image.
    disk.disk_dev.host = &host;

    disk.fileh = host.fnfile_open(disk.filename, disk.filename, sizeof(disk.filename), flag);
    if (disk.fileh == nullptr)
    {
        Debug_printf("disk_image_mount Couldn't open file: \"%s\"\n", disk.filename);
#ifdef NOT_TRANSACTION
        errorCode = 144;
#else
        transaction_error();
#endif /* NOT_TRANSACTION */
        return;
    }

    // We've gotten this far, so make sure our bootable CONFIG disk is disabled
    boot_config = false;

    // We need the file size for loading XEX files and for CASSETTE, so get that too
    disk.disk_size = host.file_size(disk.fileh);

    // And now mount it
    disk.disk_type = disk.disk_dev.mount(disk.fileh, disk.filename, disk.disk_size);
}
#endif /* NOT_SUBCLASS */

#ifdef NOT_SUBCLASS
// Toggle boot config on/off, aux1=0 is disabled, aux1=1 is enabled
void drivewireFuji::set_boot_config()
{
    // boot_config = cmdFrame.aux1;
    // drivewire_complete();
}
#endif /* NOT_SUBCLASS */

#ifdef NOT_SUBCLASS
// Do DRIVEWIRE copy
void drivewireFuji::copy_file()
{
    // uint8_t csBuf[256];
    // string copySpec;
    // string sourcePath;
    // string destPath;
    // uint8_t ck;
    // FILE *sourceFile;
    // FILE *destFile;
    // char *dataBuf;
    // unsigned char sourceSlot;
    // unsigned char destSlot;

    // dataBuf = (char *)malloc(532);

    // if (dataBuf == nullptr)
    // {
    //     drivewire_error();
    //     return;
    // }

    // memset(&csBuf, 0, sizeof(csBuf));

    // ck = bus_to_peripheral(csBuf, sizeof(csBuf));

    // if (ck != drivewire_checksum(csBuf, sizeof(csBuf)))
    // {
    //     drivewire_error();
    //     return;
    // }

    // copySpec = string((char *)csBuf);

    // Debug_printf("copySpec: %s\n", copySpec.c_str());

    // // Check for malformed copyspec.
    // if (copySpec.empty() || copySpec.find_first_of("|") == string::npos)
    // {
    //     drivewire_error();
    //     return;
    // }

    // if (cmdFrame.aux1 < 1 || cmdFrame.aux1 > 8)
    // {
    //     drivewire_error();
    //     return;
    // }

    // if (cmdFrame.aux2 < 1 || cmdFrame.aux2 > 8)
    // {
    //     drivewire_error();
    //     return;
    // }

    // sourceSlot = cmdFrame.aux1 - 1;
    // destSlot = cmdFrame.aux2 - 1;

    // // All good, after this point...

    // // Chop up copyspec.
    // sourcePath = copySpec.substr(0, copySpec.find_first_of("|"));
    // destPath = copySpec.substr(copySpec.find_first_of("|") + 1);

    // // At this point, if last part of dest path is / then copy filename from source.
    // if (destPath.back() == '/')
    // {
    //     Debug_printf("append source file\n");
    //     string sourceFilename = sourcePath.substr(sourcePath.find_last_of("/") + 1);
    //     destPath += sourceFilename;
    // }

    // // Mount hosts, if needed.
    // _fnHosts[sourceSlot].mount();
    // _fnHosts[destSlot].mount();

    // // Open files...
    // sourceFile = _fnHosts[sourceSlot].file_open(sourcePath.c_str(), (char *)sourcePath.c_str(), sourcePath.size() + 1, "r");

    // if (sourceFile == nullptr)
    // {
    //     drivewire_error();
    //     return;
    // }

    // destFile = _fnHosts[destSlot].file_open(destPath.c_str(), (char *)destPath.c_str(), destPath.size() + 1, "w");

    // if (destFile == nullptr)
    // {
    //     drivewire_error();
    //     return;
    // }

    // size_t readCount = 0;
    // size_t readTotal = 0;
    // size_t writeCount = 0;
    // size_t expected = _fnHosts[sourceSlot].file_size(sourceFile); // get the filesize
    // bool err = false;
    // do
    // {
    //     readCount = fread(dataBuf, 1, 532, sourceFile);
    //     readTotal += readCount;
    //     // Check if we got enough bytes on the read
    //     if(readCount < 532 && readTotal != expected)
    //     {
    //         err = true;
    //         break;
    //     }
    //     writeCount = fwrite(dataBuf, 1, readCount, destFile);
    //     // Check if we sent enough bytes on the write
    //     if (writeCount != readCount)
    //     {
    //         err = true;
    //         break;
    //     }
    //     Debug_printf("Copy File: %d bytes of %d\n", readTotal, expected);
    // } while (readTotal < expected);

    // if (err == true)
    // {
    //     // Remove the destination file and error
    //     _fnHosts[destSlot].file_remove((char *)destPath.c_str());
    //     drivewire_error();
    //     Debug_printf("Copy File Error! wCount: %d, rCount: %d, rTotal: %d, Expect: %d\n", writeCount, readCount, readTotal, expected);
    // }
    // else
    // {
    //     drivewire_complete();
    // }

    // // copyEnd:
    // fclose(sourceFile);
    // fclose(destFile);
    // free(dataBuf);
}
#endif /* NOT_SUBCLASS */

#ifdef NOT_SUBCLASS
// Mount all
void drivewireFuji::mount_all()
{
    bool nodisks = true; // Check at the end if no disks are in a slot and disable config

    Debug_printf("drivewireFuji::mount_all()\n");

    for (int i = 0; i < 4; i++)
    {
        fujiDisk &disk = _fnDisks[i];
        fujiHost &host = _fnHosts[disk.host_slot];
        char flag[3] = {'r', 0, 0};

        if (disk.access_mode == DISK_ACCESS_MODE_WRITE)
            flag[1] = '+';

        if (disk.host_slot != INVALID_HOST_SLOT && strlen(disk.filename) > 0)
        {
            nodisks = false; // We have a disk in a slot

            if (host.mount() == false)
            {
                return;
            }

            Debug_printf("Selecting '%s' from host #%u as %s on D%u:\n",
                         disk.filename, disk.host_slot, flag, i + 1);

            disk.fileh = host.fnfile_open(disk.filename, disk.filename, sizeof(disk.filename), flag);

            if (disk.fileh == nullptr)
            {
                return;
            }

            // We've gotten this far, so make sure our bootable CONFIG disk is disabled
            boot_config = false;

            // We need the file size for loading XEX files and for CASSETTE, so get that too
            disk.disk_size = host.file_size(disk.fileh);

            // Set the host slot for high score mode
            // TODO: Refactor along with mount disk image.
            disk.disk_dev.host = &host;

            // And now mount it
            disk.disk_type = disk.disk_dev.mount(disk.fileh, disk.filename, disk.disk_size);
            disk.disk_dev.device_active = true;
        }
    }

    if (nodisks)
    {
        // No disks in a slot, disable config
        boot_config = false;
    }

    Debug_printf("drivewireFuji::mount_all() done.\n");

}
#endif /* NOT_SUBCLASS */

#ifdef NOT_SUBCLASS
// Set boot mode
void drivewireFuji::set_boot_mode()
{
    insert_boot_device(fnDwCom.read());
    boot_config = true;
}
#endif /* NOT_SUBCLASS */

#ifdef NOT_SUBCLASS
char *_generate_appkey_filename(appkey *info)
{
    static char filenamebuf[30];

    snprintf(filenamebuf, sizeof(filenamebuf), "/FujiNet/%04hx%02hhx%02hhx.key", info->creator, info->app, info->key);
    return filenamebuf;
}
#endif /* NOT_SUBCLASS */

#ifdef NOT_SUBCLASS
/*
 Opens an "app key".  This just sets the needed app key parameters (creator, app, key, mode)
 for the subsequent expected read/write command. We could've added this information as part
 of the payload in a WRITE_APPKEY command, but there was no way to do this for READ_APPKEY.
 Requiring a separate OPEN command makes both the read and write commands behave similarly
 and leaves the possibity for a more robust/general file read/write function later.
*/
void drivewireFuji::open_app_key()
{
    Debug_print("Fuji cmd: OPEN APPKEY\n");

#ifdef NOT_TRANSACTION
    fnDwCom.readBytes((uint8_t *)&_current_appkey, sizeof(_current_appkey));
#else
    transaction_get(&_current_appkey, sizeof(_current_appkey));
#endif /* NOT_TRANSACTION */

    // Endian swap
    uint16_t tmp = _current_appkey.creator;
    _current_appkey.creator = tmp >> 8 | tmp << 8;

    // Basic check for valid data
    if (_current_appkey.creator == 0 || _current_appkey.mode == APPKEYMODE_INVALID)
    {
        Debug_println("Invalid app key data");
        errorCode = 144;
        return;
    }

    if (fnSDFAT.running() == false)
    {
        Debug_println("No SD mounted - returning error");
        errorCode = 144;
        return;
    }

    errorCode = 1;

    Debug_printf("App key creator = 0x%04hx, app = 0x%02hhx, key = 0x%02hhx, mode = %hhu, filename = \"%s\"\n",
                _current_appkey.creator, _current_appkey.app, _current_appkey.key, _current_appkey.mode,
                _generate_appkey_filename(&_current_appkey));
}
#endif /* NOT_SUBCLASS */

#ifdef NOT_SUBCLASS
/*
  The app key close operation is a placeholder in case we want to provide more robust file
  read/write operations. Currently, the file is closed immediately after the read or write operation.
*/
void drivewireFuji::close_app_key()
{
    Debug_print("Fuji cmd: CLOSE APPKEY\n");
    _current_appkey.creator = 0;
    _current_appkey.mode = APPKEYMODE_INVALID;
    errorCode = 1;
}
#endif /* NOT_SUBCLASS */

#ifdef NOT_SUBCLASS
/*
 Write an "app key" to SD (ONLY!) storage.
*/
void drivewireFuji::write_app_key()
{
    uint8_t lenh = fnDwCom.read();
    uint8_t lenl = fnDwCom.read();
    uint16_t len = lenh << 8 | lenl;
    uint8_t value[MAX_APPKEY_LEN];

    memset(value,0,sizeof(value));

#ifdef NOT_TRANSACTION
    fnDwCom.readBytes(value, len);
#else
    transaction_get(value, len);
#endif /* NOT_TRANSACTION */

    // Make sure we have valid app key information
    if (_current_appkey.creator == 0 || _current_appkey.mode != APPKEYMODE_WRITE)
    {
        Debug_println("Invalid app key metadata - aborting");
        errorCode = 144;
        return;
    }

    // Make sure we have an SD card mounted
    if (fnSDFAT.running() == false)
    {
        Debug_println("No SD mounted - can't write app key");
        errorCode = 144;
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
        errorCode = 144;
        return;
    }
    size_t count = fwrite(value, 1, len, fOut);
    int e = errno;

    fclose(fOut);

    if (count != len)
    {
        Debug_printf("Only wrote %u bytes of expected %hu, errno=%d\n", count, len, e);
        errorCode = 144;
    }
    errorCode = 1;
}
#endif /* NOT_SUBCLASS */

#ifdef NOT_SUBCLASS
/*
 Read an "app key" from SD (ONLY!) storage
*/
void drivewireFuji::read_app_key()
{
    Debug_println("Fuji cmd: READ APPKEY");

    // Make sure we have an SD card mounted
    if (fnSDFAT.running() == false)
    {
        Debug_println("No SD mounted - can't read app key");
        errorCode = 144;
        return;
    }

    // Make sure we have valid app key information
    if (_current_appkey.creator == 0 || _current_appkey.mode != APPKEYMODE_READ)
    {
        Debug_println("Invalid app key metadata - aborting");
        errorCode = 144;
        return;
    }

    char *filename = _generate_appkey_filename(&_current_appkey);

    Debug_printf("Reading appkey from \"%s\"\n", filename);

    FILE *fIn = fnSDFAT.file_open(filename, "r");
    if (fIn == nullptr)
    {
        Debug_printf("Failed to open input file: errno=%d\n", errno);
        errorCode = 144;
        return;
    }

    std::vector<uint8_t> buffer(MAX_APPKEY_LEN);
    size_t count = fread(buffer.data(), 1, buffer.size(), fIn);
    fclose(fIn);
    Debug_printf("Read %d bytes from input file\n", count);

    uint16_t sizeNetOrder = htons(count);

    response.clear();
    response.append(reinterpret_cast<char*>(&sizeNetOrder), sizeof(sizeNetOrder));
    response.append(reinterpret_cast<char*>(buffer.data()), count);

    errorCode = 1;
}
#endif /* NOT_SUBCLASS */

#ifdef NOT_SUBCLASS
// Disk Image Unmount
void drivewireFuji::disk_image_umount()
{
    uint8_t deviceSlot = fnDwCom.read();

    Debug_printf("Fuji cmd: UNMOUNT IMAGE 0x%02X\n", deviceSlot);

    // Handle disk slots
    if (deviceSlot < MAX_DISK_DEVICES)
    {
        _fnDisks[deviceSlot].disk_dev.unmount();
        _fnDisks[deviceSlot].disk_dev.device_active = false;
        _fnDisks[deviceSlot].reset();
    }
}
#endif /* NOT_SUBCLASS */

#ifdef NOT_SUBCLASS
// Disk Image Rotate
/*
  We rotate disks my changing their disk device ID's. That prevents
  us from having to unmount and re-mount devices.
*/
void drivewireFuji::image_rotate()
{
    // Debug_println("Fuji cmd: IMAGE ROTATE");

    // int count = 0;
    // // Find the first empty slot
    // while (_fnDisks[count].fileh != nullptr)
    //     count++;

    // if (count > 1)
    // {
    //     count--;

    //     // Save the device ID of the disk in the last slot
    //     int last_id = _fnDisks[count].disk_dev.id();

    //     for (int n = count; n > 0; n--)
    //     {
    //         int swap = _fnDisks[n - 1].disk_dev.id();
    //         Debug_printf("setting slot %d to ID %hx\n", n, swap);
    //         _drivewire_bus->changeDeviceId(&_fnDisks[n].disk_dev, swap);
    //     }

    //     // The first slot gets the device ID of the last slot
    //     Debug_printf("setting slot %d to ID %hx\n", 0, last_id);
    //    _drivewire_bus->changeDeviceId(&_fnDisks[0].disk_dev, last_id);

    //     // Say whatever disk is in D1:
    //     if (Config.get_general_rotation_sounds())
    //     {
    //         for (int i = 0; i <= count; i++)
    //         {
    //             if (_fnDisks[i].disk_dev.id() == 0x31)
    //             {
    //                 say_swap_label();
    //                 say_number(i + 1); // because i starts from 0
    //             }
    //         }
    //     }
    // }
}
#endif /* NOT_SUBCLASS */

// This gets called when we're about to shutdown/reboot
void drivewireFuji::shutdown()
{
    for (int i = 0; i < MAX_DW_DISK_DEVICES; i++)
        _fnDisks[i].disk_dev.unmount();
}

#ifdef NOT_SUBCLASS
void drivewireFuji::open_directory()
{
    Debug_println("Fuji cmd: OPEN DIRECTORY");

#ifdef NOT_TRANSACTION
    errorCode = 1;
#else
    transaction_complete();
#endif /* NOT_TRANSACTION */

    uint8_t hostSlot = fnDwCom.read();

#ifdef NOT_TRANSACTION
    fnDwCom.readBytes((uint8_t *)&dirpath, 256);
#else
    transaction_get(&dirpath, 256);
#endif /* NOT_TRANSACTION */

    if (_current_open_directory_slot == -1)
    {
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

        Debug_printf("Opening directory: \"%s\", pattern: \"%s\"\n", dirpath, pattern ? pattern : "");

        if (_fnHosts[hostSlot].dir_open(dirpath, pattern, 0))
        {
            _current_open_directory_slot = hostSlot;
        }
        else
        {
#ifdef NOT_TRANSACTION
            errorCode = 144;
#else
            transaction_error();
#endif /* NOT_TRANSACTION */
        }
    }
}
#endif /* NOT_SUBCLASS */

#ifdef NOT_SUBCLASS
void _set_additional_direntry_details(fsdir_entry_t *f, uint8_t *dest, uint8_t maxlen)
{
    // File modified date-time
    struct tm *modtime = localtime(&f->modified_time);
    modtime->tm_mon++;
    modtime->tm_year -= 100;

    dest[0] = modtime->tm_year;
    dest[1] = modtime->tm_mon;
    dest[2] = modtime->tm_mday;
    dest[3] = modtime->tm_hour;
    dest[4] = modtime->tm_min;
    dest[5] = modtime->tm_sec;

    // File size
    uint32_t fsize = f->size;
    dest[6] = (fsize >> 24) & 0xFF;
    dest[7] = (fsize >> 16) & 0xFF;
    dest[8] = (fsize >> 8) & 0xFF;
    dest[9] = fsize & 0xFF;

    // File flags
#define FF_DIR 0x01
#define FF_TRUNC 0x02

    dest[10] = f->isDir ? FF_DIR : 0;

    maxlen -= ADDITIONAL_DETAILS_BYTES; // Adjust the max return value with the number of additional bytes we're copying
    if (f->isDir)                       // Also subtract a byte for a terminating slash on directories
        maxlen--;
    if (strlen(f->filename) >= maxlen)
        dest[11] |= FF_TRUNC;

    // File type
    dest[12] = MediaType::discover_mediatype(f->filename);

    Debug_printf("Addtl: ");
    for (int i = 0; i < ADDITIONAL_DETAILS_BYTES; i++)
        Debug_printf("%02x ", dest[i]);
    Debug_printf("\n");
}
#else
// For some reason the directory entry structure that is sent back
// isn't standardized across platforms.
typedef struct {
    uint8_t year, month, day, hour, minute, second;
    uint32_t file_size;
    uint8_t flags, truncated, file_type;
}  __attribute__((packed)) DirEntAttrib;

size_t drivewireFuji::setDirEntryDetails(fsdir_entry_t *f, uint8_t *dest, uint8_t maxlen)
{
    int idx;
    DirEntAttrib *attrib = (DirEntAttrib *) dest;


    // File modified date-time
    struct tm *modtime = localtime(&f->modified_time);
    attrib->year = modtime->tm_year - 100;
    attrib->month = modtime->tm_mon + 1;
    attrib->day = modtime->tm_mday;
    attrib->hour = modtime->tm_hour;
    attrib->minute = modtime->tm_min;
    attrib->second = modtime->tm_sec;

    attrib->file_size = htobe32(f->size);

    // File flags
#define FF_DIR 0x01
#define FF_TRUNC 0x02

    attrib->flags = f->isDir ? FF_DIR : 0;

    maxlen -= sizeof(*attrib); // Adjust the max return value with the number of additional
                              // bytes we're copying
    if (f->isDir)             // Also subtract a byte for a terminating slash on directories
        maxlen--;
    attrib->truncated = strlen(f->filename) >= maxlen ? FF_TRUNC : 0;

    // File type
    attrib->file_type = MediaType::discover_mediatype(f->filename);

    Debug_printf("Addtl: ");
    for (int i = 0; i < sizeof(*attrib); i++)
        Debug_printf("%02x ", dest[i]);
    Debug_printf("\n");
    return sizeof(*attrib);
}
#endif /* NOT_SUBCLASS */

#ifdef NOT_SUBCLASS
char current_entry[256];

void drivewireFuji::read_directory_entry()
{
    uint8_t maxlen = fnDwCom.read();
    uint8_t addtl = fnDwCom.read();

    Debug_printf("Fuji cmd: READ DIRECTORY ENTRY (max=%hu) (addtl=%02x)\n", maxlen, addtl);

    memset(current_entry, 0, sizeof(current_entry));

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
        int fno=0;

        // If 0x80 is set on AUX2, send back additional information
        if (addtl & 0x80)
        {
            Debug_printf("Add additional info.\n");
            _set_additional_direntry_details(f, (uint8_t *)current_entry, maxlen);
            // Adjust remaining size of buffer and file path destination
            bufsize = sizeof(dirpath) - ADDITIONAL_DETAILS_BYTES;
            fno += ADDITIONAL_DETAILS_BYTES;
        }
        else
        {
            bufsize = maxlen;
        }

        // int filelen = strlcpy(filenamedest, f->filename, bufsize);
        int filelen = util_ellipsize(f->filename, &current_entry[fno], bufsize);

        // Add a slash at the end of directory entries
        if (f->isDir && filelen < (bufsize - 2))
        {
            current_entry[filelen] = '/';
            current_entry[filelen + 1] = '\0';
        }
    }

#ifdef NOT_TRANSACTION
    response.clear();
    response.shrink_to_fit();

    response = std::string((const char *)current_entry, maxlen);
#else
    transaction_put(current_entry, maxlen);
#endif /* NOT_TRANSACTION */
}
#endif /* NOT_SUBCLASS */

#ifdef NOT_SUBCLASS
void drivewireFuji::get_directory_position()
{
    Debug_println("Fuji cmd: GET DIRECTORY POSITION");

    uint16_t pos = _fnHosts[_current_open_directory_slot].dir_tell();

    // Return the value we read
    fnDwCom.write(pos << 8);
    fnDwCom.write(pos & 0xFF);

#ifdef NOT_TRANSACTION
    errorCode = 1;
#else
    transaction_complete();
#endif /* NOT_TRANSACTION */
}
#endif /* NOT_SUBCLASS */

#ifdef NOT_SUBCLASS
void drivewireFuji::set_directory_position()
{
    uint8_t h, l;

    Debug_println("Fuji cmd: SET DIRECTORY POSITION");

    // DAUX1 and DAUX2 hold the position to seek to in low/high order
    h = fnDwCom.read();
    l = fnDwCom.read();

    Debug_printf("H: %02x L: %02x", h, l);

    uint16_t pos = UINT16_FROM_HILOBYTES(h, l);

    bool result = _fnHosts[_current_open_directory_slot].dir_seek(pos);

#ifdef NOT_TRANSACTION
    errorCode = (result == true);
#else
    if (result == true)
        transaction_complete();
    else
        transaction_error();
#endif /* NOT_TRANSACTION */
}
#endif /* NOT_SUBCLASS */

#ifdef NOT_SUBCLASS
void drivewireFuji::close_directory()
{
    Debug_println("Fuji cmd: CLOSE DIRECTORY");

    if (_current_open_directory_slot != -1)
        _fnHosts[_current_open_directory_slot].dir_close();

    _current_open_directory_slot = -1;
#ifdef NOT_TRANSACTION
    errorCode = 1;
#else
    transaction_complete();
#endif /* NOT_TRANSACTION */
}
#endif /* NOT_SUBCLASS */

#ifdef NOT_SUBCLASS
// Get network adapter configuration
void drivewireFuji::get_adapter_config()
{
    Debug_println("Fuji cmd: GET ADAPTER CONFIG");

    // Response to  FUJICMD_GET_ADAPTERCONFIG
    AdapterConfig cfg;

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

#ifdef NOT_TRANSACTION
    response.clear();
    response.shrink_to_fit();

    errorCode = 1;
    response = std::string((const char *)&cfg, sizeof(cfg));
#else
    transaction_put(&cfg, sizeof(cfg));
#endif /* NOT_TRANSACTION */
}
#endif /* NOT_SUBCLASS */

// Get network adapter configuration - extended
void drivewireFuji::get_adapter_config_extended()
{
    // also return string versions of the data to save the host some computing
    Debug_printf("Fuji cmd: GET ADAPTER CONFIG EXTENDED\r\n");
    AdapterConfigExtended cfg;
    memset(&cfg, 0, sizeof(cfg));       // ensures all strings are null terminated

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
    strlcpy(cfg.sDnsIP,   fnSystem.Net.get_ip4_dns_str().c_str(),     16);
    strlcpy(cfg.sNetmask, fnSystem.Net.get_ip4_mask_str().c_str(),    16);

    sprintf(cfg.sMacAddress, "%02X:%02X:%02X:%02X:%02X:%02X", cfg.macAddress[0], cfg.macAddress[1], cfg.macAddress[2], cfg.macAddress[3], cfg.macAddress[4], cfg.macAddress[5]);
    sprintf(cfg.sBssid,      "%02X:%02X:%02X:%02X:%02X:%02X", cfg.bssid[0], cfg.bssid[1], cfg.bssid[2], cfg.bssid[3], cfg.bssid[4], cfg.bssid[5]);

#ifdef NOT_TRANSACTION
    response.clear();
    response.shrink_to_fit();

    errorCode = 1;
    response = std::string((const char *)&cfg, sizeof(cfg));
#else
    transaction_put(&cfg, sizeof(cfg));
#endif /* NOT_TRANSACTION */
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

#ifdef NOT_TRANSACTION
    fnDwCom.readBytes((uint8_t *)&newDisk, sizeof(newDisk));
#else
    transaction_get(&newDisk, sizeof(newDisk));
#endif /* NOT_TRANSACTION */

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
#ifdef NOT_TRANSACTION
        errorCode = 144;
#else
        transaction_error();
#endif /* NOT_TRANSACTION */
        return;
    }

    disk.fileh = host.fnfile_open(disk.filename, disk.filename, sizeof(disk.filename), "w");
    if (disk.fileh == nullptr)
    {
        Debug_printf("drivewire_new_disk Couldn't open file for writing: \"%s\"\n", disk.filename);
        return;
    }

    bool ok = disk.disk_dev.write_blank(disk.fileh, newDisk.numDisks);

#ifdef NOT_TRANSACTION
    errorCode = (ok == NETWORK_ERROR_SUCCESS);
#else
    if (ok == NETWORK_ERROR_SUCCESS)
        transaction_complete();
    else
        transaction_error();
#endif /* NOT_TRANSACTION */

    fnio::fclose(disk.fileh);
}

#ifdef NOT_SUBCLASS
// Unmount specified host
void drivewireFuji::unmount_host()
{
    Debug_println("Fuji cmd: UNMOUNT HOST");

    unsigned char hostSlot = fnDwCom.read();

    // Unmount any disks associated with host slot
    for (int i = 0; i < MAX_DISK_DEVICES; i++)
    {
        if (_fnDisks[i].host_slot == hostSlot)
        {
            _fnDisks[i].disk_dev.unmount();
            _fnDisks[i].disk_dev.device_active = false;
            _fnDisks[i].reset();
        }
    }

    // Unmount the host
    _fnHosts[hostSlot].unmount_success();
}
#endif /* NOT_SUBCLASS */

#ifdef NOT_SUBCLASS
// Send host slot data to computer
void drivewireFuji::read_host_slots()
{
    Debug_println("Fuji cmd: READ HOST SLOTS");

    char hostSlots[MAX_HOSTS][MAX_HOSTNAME_LEN];
    memset(hostSlots, 0, sizeof(hostSlots));

    for (int i = 0; i < MAX_HOSTS; i++)
        strlcpy(hostSlots[i], _fnHosts[i].get_hostname(), MAX_HOSTNAME_LEN);

#ifdef NOT_TRANSACTION
    response.clear();
    response.shrink_to_fit();

    response = std::string((const char *)hostSlots,256);
    errorCode = 1;
#else
    transaction_put(hostSlots, 256);
#endif /* NOT_TRANSACTION */
}
#endif /* NOT_SUBCLASS */

#ifdef NOT_SUBCLASS
// Read and save host slot data from computer
void drivewireFuji::write_host_slots()
{
    Debug_println("Fuji cmd: WRITE HOST SLOTS");

    char hostSlots[MAX_HOSTS][MAX_HOSTNAME_LEN];
#ifdef NOT_TRANSACTION
    fnDwCom.readBytes((uint8_t *)&hostSlots, sizeof(hostSlots));
#else
    transaction_get(&hostSlots, sizeof(hostSlots));
#endif /* NOT_TRANSACTION */

    for (int i = 0; i < MAX_HOSTS; i++)
        _fnHosts[i].set_hostname(hostSlots[i]);

    _populate_config_from_slots();
    Config.save();
}
#endif /* NOT_SUBCLASS */

#ifdef NOT_SUBCLASS
// Send device slot data to computer
void drivewireFuji::read_device_slots()
{
    Debug_println("Fuji cmd: READ DEVICE SLOTS");

    struct disk_slot
    {
        uint8_t hostSlot;
        uint8_t mode;
        char filename[MAX_DISPLAY_FILENAME_LEN];
    };
    disk_slot diskSlots[MAX_DISK_DEVICES];

    int returnsize;
    char *filename;

    // Load the data from our current device array
    for (int i = 0; i < MAX_DISK_DEVICES; i++)
    {
        diskSlots[i].mode = _fnDisks[i].access_mode;
        diskSlots[i].hostSlot = _fnDisks[i].host_slot;
        if (_fnDisks[i].filename[0] == '\0')
        {
            strlcpy(diskSlots[i].filename, "", MAX_DISPLAY_FILENAME_LEN);
        }
        else
        {
            // Just use the basename of the image, no path. The full path+filename is
            // usually too long for the Atari to show anyway, so the image name is more important.
            // Note: Basename can modify the input, so use a copy of the filename
            filename = strdup(_fnDisks[i].filename);
#ifdef ESP_PLATFORM
            strlcpy(diskSlots[i].filename, basename(filename), MAX_DISPLAY_FILENAME_LEN);
#else
            strlcpy(diskSlots[i].filename, basename(filename), MAX_DISPLAY_FILENAME_LEN);
#endif
            free(filename);
        }
    }

    returnsize = sizeof(disk_slot) * MAX_DISK_DEVICES;

#ifdef NOT_TRANSACTION
    response.clear();
    response.shrink_to_fit();

    errorCode = 1;

    response = std::string((const char *)&diskSlots, returnsize);
#else
    transaction_put(&diskSlots, returnsize);
#endif /* NOT_TRANSACTION */
}
#endif /* NOT_SUBCLASS */

#ifdef NOT_SUBCLASS
// Read and save disk slot data from computer
void drivewireFuji::write_device_slots()
{
    Debug_println("Fuji cmd: WRITE DEVICE SLOTS");

    struct
    {
        uint8_t hostSlot;
        uint8_t mode;
        char filename[MAX_DISPLAY_FILENAME_LEN];
    } diskSlots[MAX_DISK_DEVICES];

#ifdef NOT_TRANSACTION
    fnDwCom.readBytes((uint8_t *)&diskSlots, sizeof(diskSlots));
#else
    transaction_get(&diskSlots, sizeof(diskSlots));
#endif /* NOT_TRANSACTION */

    // Load the data into our current device array
    for (int i = 0; i < MAX_DISK_DEVICES; i++)
        _fnDisks[i].reset(diskSlots[i].filename, diskSlots[i].hostSlot, diskSlots[i].mode);

    // Save the data to disk
    _populate_config_from_slots();
    Config.save();
}
#endif /* NOT_SUBCLASS */

#ifdef NOT_SUBCLASS
// Temporary(?) function while we move from old config storage to new
void drivewireFuji::_populate_slots_from_config()
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
            if (Config.get_mount_host_slot(i) >= 0 && Config.get_mount_host_slot(i) <= MAX_HOSTS)
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
void drivewireFuji::_populate_config_from_slots()
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
                              htype == HOSTTYPE_TNFS ? fnConfig::host_types::HOSTTYPE_TNFS : fnConfig::host_types::HOSTTYPE_SD);
        }
    }

    for (int i = 0; i < MAX_DISK_DEVICES; i++)
    {
        if (_fnDisks[i].host_slot >= MAX_HOSTS || _fnDisks[i].filename[0] == '\0')
            Config.clear_mount(i);
        else
            Config.store_mount(i, _fnDisks[i].host_slot, _fnDisks[i].filename,
                               _fnDisks[i].access_mode == DISK_ACCESS_MODE_WRITE ? fnConfig::mount_modes::MOUNTMODE_WRITE : fnConfig::mount_modes::MOUNTMODE_READ);
    }
}
#endif /* NOT_SUBCLASS */

#ifdef NOT_SUBCLASS
// Write a 256 byte filename to the device slot
void drivewireFuji::set_device_filename()
{
    char tmp[MAX_FILENAME_LEN];

    // AUX1 is the desired device slot
    uint8_t slot = fnDwCom.read();
    // AUX2 contains the host slot and the mount mode (READ/WRITE)
    uint8_t host = fnDwCom.read();
    uint8_t mode = fnDwCom.read();

#ifdef NOT_TRANSACTION
    fnDwCom.readBytes((uint8_t *)tmp, MAX_FILENAME_LEN);
#else
    transaction_get(tmp, MAX_FILENAME_LEN);
#endif /* NOT_TRANSACTION */

    Debug_printf("Fuji cmd: SET DEVICE SLOT 0x%02X/%02X/%02X FILENAME: %s\n", slot, host, mode, tmp);

    // Handle DISK slots
    if (slot < MAX_DISK_DEVICES)
    {
        memcpy(_fnDisks[slot].filename, tmp, MAX_FILENAME_LEN);
        // If the filename is empty, mark this as an invalid host, so that mounting will ignore it too
        if (strlen(_fnDisks[slot].filename) == 0) {
            _fnDisks[slot].host_slot = INVALID_HOST_SLOT;
        } else {
            _fnDisks[slot].host_slot = host;
        }
        _fnDisks[slot].access_mode = mode;
        _populate_config_from_slots();
    }

    Config.save();
}
#endif /* NOT_SUBCLASS */

#ifdef NOT_SUBCLASS
// Get a 256 byte filename from device slot
void drivewireFuji::get_device_filename()
{
    char tmp[MAX_FILENAME_LEN];

    // AUX1 is the desired device slot
    uint8_t slot = fnDwCom.read();

    if (slot > 7)
    {
#ifdef NOT_TRANSACTION
        errorCode = 144;
#else
        transaction_error();
#endif /* NOT_TRANSACTION */
    }

    memcpy(tmp, _fnDisks[slot].filename, MAX_FILENAME_LEN);
#ifdef NOT_TRANSACTION
    response.clear();
    response.shrink_to_fit();

    errorCode = 1;

    response = std::string(tmp, MAX_FILENAME_LEN);
#else
    transaction_put(tmp, MAX_FILENAME_LEN);
#endif /* NOT_TRANSACTION */
}
#endif /* NOT_SUBCLASS */

#ifdef NOT_SUBCLASS
// Mounts the desired boot disk number
void drivewireFuji::insert_boot_device(uint8_t d)
{
    Debug_printf("insert_boot_device()\n");

    const char *config_atr_coco = "/autorun.dsk";
    const char *config_atr_dragon = "/autorund.vdk";
    const char *mount_and_boot_atr = "/mount-and-boot.dsk";
    bool bIsDragon = false;
#ifdef ESP_PLATFORM

    fnSystem.set_pin_mode(PIN_EPROM_A14, gpio_mode_t::GPIO_MODE_INPUT, SystemManager::pull_updown_t::PULL_NONE);
    fnSystem.set_pin_mode(PIN_EPROM_A15, gpio_mode_t::GPIO_MODE_INPUT, SystemManager::pull_updown_t::PULL_NONE);
    bIsDragon = (fnSystem.digital_read(PIN_EPROM_A14) == DIGI_HIGH && fnSystem.digital_read(PIN_EPROM_A15) == DIGI_HIGH);
#endif  /* ESP_PLATFORM */

    fnFile *fBoot = NULL;
    size_t sz = 0;

    _bootDisk.unmount();

    switch (d)
    {
    case 0:
        if  (bIsDragon)
        {
            fBoot = fsFlash.fnfile_open(config_atr_dragon);
        }
        else
        {
            fBoot = fsFlash.fnfile_open(config_atr_coco);
        }
        break;
    case 1:
        fBoot = fsFlash.fnfile_open(mount_and_boot_atr);
        break;
    }

    if (fBoot)
    {
        fnio::fseek(fBoot, 0, SEEK_END);
        sz = fnio::ftell(fBoot);
        fnio::fseek(fBoot, 0, SEEK_SET);
        _bootDisk.mount(fBoot, bIsDragon ? config_atr_dragon : config_atr_coco, sz);

        _bootDisk.is_config_device = true;
        _bootDisk.device_active = true;
    }
}
#endif /* NOT_SUBCLASS */

void drivewireFuji::base64_encode_input()
{
    uint8_t lenh = SYSTEM_BUS.read();
    uint8_t lenl = SYSTEM_BUS.read();
    uint16_t len = lenh << 8 | lenl;

    if (!len)
    {
        Debug_printf("Zero length. Aborting.\n");
#ifdef NOT_TRANSACTION
        errorCode = 144;
#else
        transaction_error();
#endif /* NOT_TRANSACTION */
        return;
    }

    std::vector<unsigned char> p(len);
#ifdef NOT_TRANSACTION
    SYSTEM_BUS.read(p.data(), len);
#else
    transaction_get(p.data(), len);
#endif /* NOT_TRANSACTION */
    base64.base64_buffer += std::string((const char *)p.data(), len);
#ifdef NOT_TRANSACTION
    errorCode = 1;
#else
    transaction_complete();
#endif /* NOT_TRANSACTION */
}

void drivewireFuji::base64_encode_compute()
{
    size_t out_len;

    std::unique_ptr<char[]> p = Base64::encode(base64.base64_buffer.c_str(), base64.base64_buffer.size(), &out_len);

    if (!p)
    {
        Debug_printf("base64_encode_compute() failed.\n");
#ifdef NOT_TRANSACTION
        errorCode = 144;
#else
        transaction_error();
#endif /* NOT_TRANSACTION */
        return;
    }

    base64.base64_buffer.clear();
    base64.base64_buffer = std::string(p.get(), out_len);

#ifdef NOT_TRANSACTION
    errorCode = 1;
#else
    transaction_complete();
#endif /* NOT_TRANSACTION */
}

void drivewireFuji::base64_encode_length()
{
    size_t l = base64.base64_buffer.length();
    uint8_t o[4] =
    {
        (uint8_t)(l >> 24),
        (uint8_t)(l >> 16),
        (uint8_t)(l >> 8),
        (uint8_t)(l)
    };

#ifdef NOT_TRANSACTION
    response.clear();
    response.shrink_to_fit();

    response = std::string((const char *)&o, 4);

    errorCode = 1;
#else
    transaction_put(&o, 4);
#endif /* NOT_TRANSACTION */
}

void drivewireFuji::base64_encode_output()
{
    uint8_t lenl = SYSTEM_BUS.read();
    uint8_t lenh = SYSTEM_BUS.read();
    uint16_t len = lenh << 8 | lenl;

    if (!len)
    {
        Debug_printf("Refusing to send zero byte buffer. Exiting.");
#ifdef NOT_TRANSACTION
        errorCode = 144;
#else
        transaction_error();
#endif /* NOT_TRANSACTION */
        return;
    }

    std::vector<unsigned char> p(len);
    std::memcpy(p.data(), base64.base64_buffer.data(), len);
    base64.base64_buffer.erase(0, len);
    base64.base64_buffer.shrink_to_fit();

#ifdef NOT_TRANSACTION
    response = std::string((const char *)p.data(), len);
    errorCode = 1;
#else
    transaction_put(p.data(), len);
#endif /* NOT_TRANSACTION */
}

void drivewireFuji::base64_decode_input()
{
    uint8_t lenl = SYSTEM_BUS.read();
    uint8_t lenh = SYSTEM_BUS.read();
    uint16_t len = lenh << 8 | lenl;

    if (!len)
    {
        Debug_printf("Refusing to input zero length. Exiting.\n");
#ifdef NOT_TRANSACTION
        errorCode = 144;
#else
        transaction_error();
#endif /* NOT_TRANSACTION */
        return;
    }

    std::vector<unsigned char> p(len);
#ifdef NOT_TRANSACTION
    SYSTEM_BUS.read(p.data(), len);
#else
    transaction_get(p.data(), len);
#endif /* NOT_TRANSACTION */
    base64.base64_buffer += std::string((const char *)p.data(), len);

#ifdef NOT_TRANSACTION
    errorCode = 1;
#else
    transaction_complete();
#endif /* NOT_TRANSACTION */
}

void drivewireFuji::base64_decode_compute()
{
    size_t out_len;

    Debug_printf("FUJI: BASE64 DECODE COMPUTE\n");

    std::unique_ptr<unsigned char[]> p = Base64::decode(base64.base64_buffer.c_str(), base64.base64_buffer.size(), &out_len);
    if (!p)
    {
        Debug_printf("base64_encode compute failed\n");
#ifdef NOT_TRANSACTION
        errorCode = 144;
#else
        transaction_error();
#endif /* NOT_TRANSACTION */
        return;
    }

    base64.base64_buffer.clear();
    base64.base64_buffer = std::string((const char *)p.get(), out_len);

    Debug_printf("Resulting BASE64 encoded data is: %u bytes\n", out_len);
#ifdef NOT_TRANSACTION
    errorCode = 1;
#else
    transaction_complete();
#endif /* NOT_TRANSACTION */
}

void drivewireFuji::base64_decode_length()
{
    Debug_printf("FUJI: BASE64 DECODE LENGTH\n");

    size_t len = base64.base64_buffer.length();
    uint8_t _response[4] = {
        (uint8_t)(len >>  24),
        (uint8_t)(len >>  16),
        (uint8_t)(len >>  8),
        (uint8_t)(len >>  0)
    };

    if (!len)
    {
        Debug_printf("BASE64 buffer is 0 bytes, sending error.\n");
#ifdef NOT_TRANSACTION
        errorCode = 144;
#else
        transaction_error();
#endif /* NOT_TRANSACTION */
        return;
    }

    Debug_printf("base64 buffer length: %u bytes\n", len);

#ifdef NOT_TRANSACTION
    response.clear();
    response.shrink_to_fit();

    response = std::string((const char *)_response, 4);
    errorCode = 1;
#else
    transaction_put(_response, 4);
#endif /* NOT_TRANSACTION */
}

void drivewireFuji::base64_decode_output()
{
    Debug_printf("FUJI: BASE64 DECODE OUTPUT\n");

    uint8_t lenl = SYSTEM_BUS.read();
    uint8_t lenh = SYSTEM_BUS.read();
    uint16_t len = lenh << 8 | lenl;

    if (!len)
    {
        Debug_printf("Refusing to send a zero byte buffer. Aborting\n");
#ifdef NOT_TRANSACTION
        errorCode = 144;
#else
        transaction_error();
#endif /* NOT_TRANSACTION */
        return;
    }
    else if (len > base64.base64_buffer.length())
    {
        Debug_printf("Requested %u bytes, but buffer is only %u bytes, aborting.\n", len, base64.base64_buffer.length());
#ifdef NOT_TRANSACTION
        errorCode = 144;
#else
        transaction_error();
#endif /* NOT_TRANSACTION */
        return;
    }
    else
    {
        Debug_printf("Requested %u bytes\n", len);
    }

    std::vector<unsigned char> p(len);
    memcpy(p.data(), base64.base64_buffer.data(), len);
    base64.base64_buffer.erase(0, len);
    base64.base64_buffer.shrink_to_fit();
#ifdef NOT_TRANSACTION
    response.clear();
    response.shrink_to_fit();
    response = std::string((const char *)p.data(), len);

    errorCode = 1;
#else
    transaction_put(p.data(), len);
#endif /* NOT_TRANSACTION */
}

void drivewireFuji::hash_input()
{
    Debug_printf("FUJI: HASH INPUT\n");
    uint8_t lenl = SYSTEM_BUS.read();
    uint8_t lenh = SYSTEM_BUS.read();
    uint16_t len = lenh << 8 | lenl;


    if (!len)
    {
        Debug_printf("Invalid length. Aborting");
#ifdef NOT_TRANSACTION
        errorCode = 144;
#else
        transaction_error();
#endif /* NOT_TRANSACTION */
        return;
    }

    std::vector<uint8_t> p(len);
#ifdef NOT_TRANSACTION
    SYSTEM_BUS.read(p.data(), len);
#else
    transaction_get(p.data(), len);
#endif /* NOT_TRANSACTION */
    hasher.add_data(p);
#ifdef NOT_TRANSACTION
    errorCode = 1;
#else
    transaction_complete();
#endif /* NOT_TRANSACTION */
}

void drivewireFuji::hash_compute(bool clear_data)
{
    Debug_printf("FUJI: HASH COMPUTE\n");
    algorithm = Hash::to_algorithm(SYSTEM_BUS.read());
    hasher.compute(algorithm, clear_data);
#ifdef NOT_TRANSACTION
    errorCode = 1;
#else
    transaction_complete();
#endif /* NOT_TRANSACTION */
}

void drivewireFuji::hash_length()
{
    Debug_printf("FUJI: HASH LENGTH\n");
    uint8_t is_hex = SYSTEM_BUS.read() == 1;
    uint8_t r = hasher.hash_length(algorithm, is_hex);
#ifdef NOT_TRANSACTION
    response = std::string((const char *)&r, 1);
    errorCode = 1;
#else
    transaction_put(&r, 1);
#endif /* NOT_TRANSACTION */
}

void drivewireFuji::hash_output()
{
    Debug_printf("FUJI: HASH OUTPUT\n");

    uint8_t is_hex = SYSTEM_BUS.read() == 1;
    if (is_hex) {
#ifdef NOT_TRANSACTION
        response = hasher.output_hex();
#else
        std::string output = hasher.output_hex();
        transaction_put(output.c_str(), output.size());
#endif /* NOT_TRANSACTION */
    } else {
        std::vector<uint8_t> hashed_data = hasher.output_binary();
#ifdef NOT_TRANSACTION
        response = std::string(hashed_data.begin(), hashed_data.end());
#else
        transaction_put(hashed_data.data(), hashed_data.size());
#endif /* NOT_TRANSACTION */
    }
#ifdef NOT_TRANSACTION
    errorCode = 1;
#endif /* NOT_TRANSACTION */
}

void drivewireFuji::hash_clear()
{
    Debug_printf("FUJI: HASH INIT\n");
    hasher.clear();
#ifdef NOT_TRANSACTION
    errorCode = 1;
#else
    transaction_complete();
#endif /* NOT_TRANSACTION */
}

// Initializes base settings and adds our devices to the DRIVEWIRE bus
void drivewireFuji::setup()
{
    Debug_printf("theFuji.setup()\n");
    // set up Fuji device

    populate_slots_from_config();

    insert_boot_device(Config.get_general_boot_mode(), IMAGE_EXTENSION, MEDIATYPE_UNKNOWN, &bootdisk);

    // Disable booting from CONFIG if our settings say to turn it off
    boot_config = Config.get_general_config_enabled();

    // Disable status_wait if our settings say to turn it off
    status_wait_enabled = Config.get_general_status_wait_enabled();
}

#ifdef NOT_SUBCLASS
drivewireDisk *drivewireFuji::bootdisk()
{
    return &_bootDisk;
}
#endif /* NOT_SUBCLASS */

#ifdef NOT_SUBCLASS
int drivewireFuji::get_disk_id(int drive_slot)
{
    return drive_slot; // silly
    // return _fnDisks[drive_slot].disk_dev.id();
}
#endif /* NOT_SUBCLASS */

#ifdef NOT_SUBCLASS
std::string drivewireFuji::get_host_prefix(int host_slot)
{
    return _fnHosts[host_slot].get_prefix();
}
#endif /* NOT_SUBCLASS */

#ifdef NOT_SUBCLASS
// Public method to update host in specific slot
fujiHost *drivewireFuji::set_slot_hostname(int host_slot, char *hostname)
{
    _fnHosts[host_slot].set_hostname(hostname);
    populate_config_from_slots();
    return &_fnHosts[host_slot];
}
#endif /* NOT_SUBCLASS */

void drivewireFuji::send_error()
{
    Debug_printf("drivewireFuji::send_error(%u)\n",_errorCode);
    SYSTEM_BUS.write(_errorCode);
}

void drivewireFuji::random()
{
    int r = rand();
    Debug_printf("drivewireFuji::random(%u)\n",r);

#ifdef NOT_TRANSACTION
    response.clear();
    response.shrink_to_fit();

    // Endianness does not matter, so long as it is random.
    response = std::string((const char *)&r,sizeof(r));
#else
    transaction_put(&r, sizeof(r));
#endif /* NOT_TRANSACTION */
}

void drivewireFuji::send_response()
{
    // Send body
    SYSTEM_BUS.write((uint8_t *)_response.c_str(),_response.length());

    // Clear the response
    _response.clear();
    _response.shrink_to_fit();
}

void drivewireFuji::ready()
{
    SYSTEM_BUS.write(0x01); // Yes, ready.
}

void drivewireFuji::process()
{
    uint8_t c = SYSTEM_BUS.read();

    _errorCode = 1;
    switch (c)
    {
    case FUJICMD_SEND_ERROR:
        send_error();
        break;
    case FUJICMD_RESET:
        fnSystem.reboot();
        break;
    case FUJICMD_GET_ADAPTERCONFIG:
        fujicmd_get_adapter_config();
        break;
    case FUJICMD_GET_ADAPTERCONFIG_EXTENDED:
        get_adapter_config_extended();
        break;
    case FUJICMD_GET_SCAN_RESULT:
        fujicmd_net_scan_result(SYSTEM_BUS.read());
        break;
    case FUJICMD_SCAN_NETWORKS:
        fujicmd_net_scan_networks();
        break;
    case FUJICMD_SET_SSID:
        {
            SSIDConfig cfg;
            if (!transaction_get(&cfg, sizeof(cfg)))
                transaction_error();
            else
                fujicmd_net_set_ssid_success(cfg.ssid, cfg.password, false);
        }
        break;
    case FUJICMD_GET_SSID:
        fujicmd_net_get_ssid();
        break;
    case FUJICMD_READ_HOST_SLOTS:
        fujicmd_read_host_slots();
        break;
    case FUJICMD_READ_DEVICE_SLOTS:
        fujicmd_read_device_slots(MAX_DW_DISK_DEVICES);
        break;
    case FUJICMD_WRITE_DEVICE_SLOTS:
        fujicmd_write_device_slots(MAX_DW_DISK_DEVICES);
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
        fujicmd_mount_host_success(SYSTEM_BUS.read());
        break;
    case FUJICMD_OPEN_DIRECTORY:
        {
            uint8_t hostSlot = SYSTEM_BUS.read();
            char dirpath[256];
            transaction_get(dirpath, sizeof(dirpath));
            fujicmd_open_directory_success(hostSlot, dirpath, sizeof(dirpath));
        }
        break;
    case FUJICMD_CLOSE_DIRECTORY:
        fujicmd_close_directory();
        break;
    case FUJICMD_READ_DIR_ENTRY:
        {
            uint8_t maxlen = SYSTEM_BUS.read();
            uint8_t addtl = SYSTEM_BUS.read();
            fujicmd_read_directory_entry(maxlen, addtl);
        }
        break;
    case FUJICMD_SET_DIRECTORY_POSITION:
        {
            uint8_t h, l;
            h = SYSTEM_BUS.read();
            l = SYSTEM_BUS.read();
            uint16_t pos = UINT16_FROM_HILOBYTES(h, l);

            fujicmd_set_directory_position(pos);
        }
        break;
    case FUJICMD_SET_DEVICE_FULLPATH:
        {
            uint8_t slot = SYSTEM_BUS.read();
            uint8_t host = SYSTEM_BUS.read();
            uint8_t mode = SYSTEM_BUS.read();
            fujicmd_set_device_filename_success(slot, host, mode);
        }
        break;
    case FUJICMD_GET_DEVICE_FULLPATH:
        fujicmd_get_device_filename(SYSTEM_BUS.read());
        break;
    case FUJICMD_MOUNT_IMAGE:
        {
            uint8_t slot = SYSTEM_BUS.read();
            uint8_t mode = SYSTEM_BUS.read();
            fujicmd_mount_disk_image_success(slot, mode);
        }
        break;
    case FUJICMD_UNMOUNT_HOST:
        fujicmd_unmount_host_success(SYSTEM_BUS.read());
        break;
    case FUJICMD_UNMOUNT_IMAGE:
        fujicmd_unmount_disk_image_success(SYSTEM_BUS.read());
        break;
    case FUJICMD_NEW_DISK:
        new_disk();
        break;
    case FUJICMD_SEND_RESPONSE:
        send_response();
        break;
    case FUJICMD_DEVICE_READY:
        ready();
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
        {
            uint8_t lenh = SYSTEM_BUS.read();
            uint8_t lenl = SYSTEM_BUS.read();
            uint16_t len = lenh << 8 | lenl;
            fujicmd_write_app_key(len);
        }
        break;
    case FUJICMD_RANDOM_NUMBER:
        random();
        break;
    case FUJICMD_BASE64_ENCODE_INPUT:
        base64_encode_input();
        break;
    case FUJICMD_BASE64_ENCODE_COMPUTE:
        base64_encode_compute();
        break;
    case FUJICMD_BASE64_ENCODE_LENGTH:
        base64_encode_length();
        break;
    case FUJICMD_BASE64_ENCODE_OUTPUT:
        base64_encode_output();
        break;
    case FUJICMD_BASE64_DECODE_INPUT:
        base64_decode_input();
        break;
    case FUJICMD_BASE64_DECODE_COMPUTE:
        base64_decode_compute();
        break;
    case FUJICMD_BASE64_DECODE_LENGTH:
        base64_decode_length();
        break;
    case FUJICMD_BASE64_DECODE_OUTPUT:
        base64_decode_output();
        break;
    case FUJICMD_HASH_INPUT:
        hash_input();
        break;
    case FUJICMD_HASH_COMPUTE:
        hash_compute(true);
        break;
    case FUJICMD_HASH_COMPUTE_NO_CLEAR:
        hash_compute(false);
        break;
    case FUJICMD_HASH_LENGTH:
        hash_length();
        break;
    case FUJICMD_HASH_OUTPUT:
        hash_output();
        break;
    case FUJICMD_HASH_CLEAR:
        hash_clear();
        break;
    case FUJICMD_SET_BOOT_MODE:
        fujicmd_set_boot_mode(SYSTEM_BUS.read(), IMAGE_EXTENSION, MEDIATYPE_UNKNOWN, &bootdisk);
        break;
    case FUJICMD_MOUNT_ALL:
        fujicmd_mount_all_success();
        break;
    case FUJICMD_GET_HOST_PREFIX:
        fujicmd_get_host_prefix(SYSTEM_BUS.read());
        break;
    case FUJICMD_SET_HOST_PREFIX:
        fujicmd_set_host_prefix(SYSTEM_BUS.read());
        break;
    case FUJICMD_COPY_FILE:
        {
            uint8_t source = SYSTEM_BUS.read();
            uint8_t dest = SYSTEM_BUS.read();
            char dirpath[256];
            transaction_get(dirpath, sizeof(dirpath));
            fujicmd_copy_file_success(source, dest, dirpath);
        }
        break;
    default:
        break;
    }
}

#endif /* BUILD_COCO */
