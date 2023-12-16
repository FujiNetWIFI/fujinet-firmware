#ifdef BUILD_COCO

#include "fuji.h"

#include <driver/ledc.h>

#include <cstdint>
#include <cstring>

#include "../../../include/debug.h"

#include "fnSystem.h"
#include "fnConfig.h"
#include "fsFlash.h"
#include "fnWiFi.h"

#include "led.h"
#include "utils.h"

drivewireFuji theFuji; // global fuji device object

// drivewireDisk drivewireDiskDevs[MAX_HOSTS];
drivewireNetwork drivewireNetDevs[MAX_NETWORK_DEVICES];

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
        Debug_printf("say_number() - Uncaught number %d", n);
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

// Constructor
drivewireFuji::drivewireFuji()
{
    // Helpful for debugging
    for (int i = 0; i < MAX_HOSTS; i++)
        _fnHosts[i].slotid = i;
}

// Reset FujiNet
void drivewireFuji::reset_fujinet()
{
    Debug_println("Fuji cmd: REBOOT");
    // drivewire_complete();
    fnSystem.reboot();
}

// Scan for networks
void drivewireFuji::net_scan_networks()
{
    Debug_println("Fuji cmd: SCAN NETWORKS");

    if (!wifiScanStarted)
    {
        wifiScanStarted = true;
        _countScannedSSIDs = fnWiFi.scan_networks();
    }

    fnUartBUS.write(_countScannedSSIDs);
}

// Return scanned network entry
void drivewireFuji::net_scan_result()
{
    Debug_println("Fuji cmd: GET SCAN RESULT");

    uint8_t n = fnUartBUS.read();

    wifiScanStarted = false;

    // Response to  FUJICMD_GET_SCAN_RESULT
    struct
    {
        char ssid[MAX_SSID_LEN + 1];
        uint8_t rssi;
    } detail;

    bool err = false;
    if (n < _countScannedSSIDs)
        fnWiFi.get_scan_result(n, detail.ssid, &detail.rssi);
    else
    {
        memset(&detail, 0, sizeof(detail));
        err = true;
    }

    fnUartBUS.write((uint8_t *)&detail, sizeof(detail));
}

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
     We memcpy instead of strcpy because technically the SSID and phasephras aren't strings and aren't null terminated,
     they're arrays of bytes officially and can contain any byte value - including a zero - at any point in the array.
     However, we're not consistent about how we treat this in the different parts of the code.
    */
    std::string s = Config.get_wifi_ssid();
    memcpy(cfg.ssid, s.c_str(),
           s.length() > sizeof(cfg.ssid) ? sizeof(cfg.ssid) : s.length());

    s = Config.get_wifi_passphrase();
    memcpy(cfg.password, s.c_str(),
           s.length() > sizeof(cfg.password) ? sizeof(cfg.password) : s.length());

    fnUartBUS.write((uint8_t *)&cfg, sizeof(cfg));
}

// Set SSID
void drivewireFuji::net_set_ssid()
{
    Debug_printf("\r\nFuji cmd: SET SSID");
    struct
    {
        char ssid[MAX_SSID_LEN + 1];
        char password[MAX_WIFI_PASS_LEN];
    } cfg;

    fnUartBUS.readBytes((uint8_t *)&cfg, sizeof(cfg));

    bool save = false; // for now don't save - to do save if connection was succesful

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

// Get WiFi Status
void drivewireFuji::net_get_wifi_status()
{
    uint8_t wifiStatus = fnWiFi.connected() ? 3 : 6;
    Debug_printv("Fuji cmd: GET WIFI STATUS: %u", wifiStatus);
    fnUartBUS.write(wifiStatus);
}

// Check if Wifi is enabled
void drivewireFuji::net_get_wifi_enabled()
{
    uint8_t e = Config.get_wifi_enabled() ? 1 : 0;

    Debug_printv("Fuji cmd: GET WIFI ENABLED: %u", e);

    fnUartBUS.write(e);
}

// Mount Server
void drivewireFuji::mount_host()
{
    // Debug_println("Fuji cmd: MOUNT HOST");

    // unsigned char hostSlot = cmdFrame.aux1;

    // // Make sure we weren't given a bad hostSlot
    // if (!_validate_host_slot(hostSlot, "drivewire_tnfs_mount_hosts"))
    // {
    //     drivewire_error();
    //     return;
    // }

    // if (!_fnHosts[hostSlot].mount())
    //     drivewire_error();
    // else
    //     drivewire_complete();
}

// Disk Image Mount
void drivewireFuji::disk_image_mount()
{
    // // TAPE or CASSETTE handling: this function can also mount CAS and WAV files
    // // to the C: device. Everything stays the same here and the mounting
    // // where all the magic happens is done in the drivewireDisk::mount() function.
    // // This function opens the file, so cassette does not need to open the file.
    // // Cassette needs the file pointer and file size.

    // Debug_println("Fuji cmd: MOUNT IMAGE");

    // uint8_t deviceSlot = cmdFrame.aux1;
    // uint8_t options = cmdFrame.aux2; // DISK_ACCESS_MODE

    // // TODO: Implement FETCH?
    // char flag[3] = {'r', 0, 0};
    // if (options == DISK_ACCESS_MODE_WRITE)
    //     flag[1] = '+';

    // // Make sure we weren't given a bad hostSlot
    // if (!_validate_device_slot(deviceSlot))
    // {
    //     drivewire_error();
    //     return;
    // }

    // if (!_validate_host_slot(_fnDisks[deviceSlot].host_slot))
    // {
    //     drivewire_error();
    //     return;
    // }

    // // A couple of reference variables to make things much easier to read...
    // fujiDisk &disk = _fnDisks[deviceSlot];
    // fujiHost &host = _fnHosts[disk.host_slot];

    // Debug_printf("Selecting '%s' from host #%u as %s on D%u:\n",
    //              disk.filename, disk.host_slot, flag, deviceSlot + 1);

    // // TODO: Refactor along with mount disk image.
    // disk.disk_dev.host = &host;

    // disk.fileh = host.file_open(disk.filename, disk.filename, sizeof(disk.filename), flag);

    // if (disk.fileh == nullptr)
    // {
    //     drivewire_error();
    //     return;
    // }

    // // We've gotten this far, so make sure our bootable CONFIG disk is disabled
    // boot_config = false;
    // status_wait_count = 0;

    // // We need the file size for loading XEX files and for CASSETTE, so get that too
    // disk.disk_size = host.file_size(disk.fileh);

    // // And now mount it
    // disk.disk_type = disk.disk_dev.mount(disk.fileh, disk.filename, disk.disk_size);

    // drivewire_complete();
}

// Toggle boot config on/off, aux1=0 is disabled, aux1=1 is enabled
void drivewireFuji::set_boot_config()
{
    // boot_config = cmdFrame.aux1;
    // drivewire_complete();
}

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

// Mount all
void drivewireFuji::mount_all()
{
    bool nodisks = true; // Check at the end if no disks are in a slot and disable config

    for (int i = 0; i < 8; i++)
    {
        fujiDisk &disk = _fnDisks[i];
        fujiHost &host = _fnHosts[disk.host_slot];
        char flag[3] = {'r', 0, 0};

        if (disk.access_mode == DISK_ACCESS_MODE_WRITE)
            flag[1] = '+';

        if (disk.host_slot != INVALID_HOST_SLOT)
        {
            nodisks = false; // We have a disk in a slot

            if (host.mount() == false)
            {
                return;
            }

            Debug_printf("Selecting '%s' from host #%u as %s on D%u:\n",
                         disk.filename, disk.host_slot, flag, i + 1);

            disk.fileh = host.file_open(disk.filename, disk.filename, sizeof(disk.filename), flag);

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
        }
    }

    if (nodisks)
    {
        // No disks in a slot, disable config
        boot_config = false;
    }
}

// Set boot mode
void drivewireFuji::set_boot_mode()
{
    // insert_boot_device(cmdFrame.aux1);
    // boot_config = true;
    // drivewire_complete();
}

char *_generate_appkey_filename(appkey *info)
{
    static char filenamebuf[30];

    snprintf(filenamebuf, sizeof(filenamebuf), "/FujiNet/%04hx%02hhx%02hhx.key", info->creator, info->app, info->key);
    return filenamebuf;
}

/*
 Opens an "app key".  This just sets the needed app key parameters (creator, app, key, mode)
 for the subsequent expected read/write command. We could've added this information as part
 of the payload in a WRITE_APPKEY command, but there was no way to do this for READ_APPKEY.
 Requiring a separate OPEN command makes both the read and write commands behave similarly
 and leaves the possibity for a more robust/general file read/write function later.
*/
void drivewireFuji::open_app_key()
{
    // Debug_print("Fuji cmd: OPEN APPKEY\n");

    // // The data expected for this command
    // uint8_t ck = bus_to_peripheral((uint8_t *)&_current_appkey, sizeof(_current_appkey));

    // if (drivewire_checksum((uint8_t *)&_current_appkey, sizeof(_current_appkey)) != ck)
    // {
    //     drivewire_error();
    //     return;
    // }

    // // We're only supporting writing to SD, so return an error if there's no SD mounted
    // if (fnSDFAT.running() == false)
    // {
    //     Debug_println("No SD mounted - returning error");
    //     drivewire_error();
    //     return;
    // }

    // // Basic check for valid data
    // if (_current_appkey.creator == 0 || _current_appkey.mode == APPKEYMODE_INVALID)
    // {
    //     Debug_println("Invalid app key data");
    //     drivewire_error();
    //     return;
    // }

    // Debug_printf("App key creator = 0x%04hx, app = 0x%02hhx, key = 0x%02hhx, mode = %hhu, filename = \"%s\"\n",
    //              _current_appkey.creator, _current_appkey.app, _current_appkey.key, _current_appkey.mode,
    //              _generate_appkey_filename(&_current_appkey));

    // drivewire_complete();
}

/*
  The app key close operation is a placeholder in case we want to provide more robust file
  read/write operations. Currently, the file is closed immediately after the read or write operation.
*/
void drivewireFuji::close_app_key()
{
    Debug_print("Fuji cmd: CLOSE APPKEY\n");
    _current_appkey.creator = 0;
    _current_appkey.mode = APPKEYMODE_INVALID;
    // drivewire_complete();
}

/*
 Write an "app key" to SD (ONLY!) storage.
*/
void drivewireFuji::write_app_key()
{
    // uint16_t keylen = UINT16_FROM_HILOBYTES(cmdFrame.aux2, cmdFrame.aux1);

    // Debug_printf("Fuji cmd: WRITE APPKEY (keylen = %hu)\n", keylen);

    // // Data for  FUJICMD_WRITE_APPKEY
    // uint8_t value[MAX_APPKEY_LEN];

    // uint8_t ck = bus_to_peripheral((uint8_t *)value, sizeof(value));

    // if (drivewire_checksum((uint8_t *)value, sizeof(value)) != ck)
    // {
    //     drivewire_error();
    //     return;
    // }

    // // Make sure we have valid app key information
    // if (_current_appkey.creator == 0 || _current_appkey.mode != APPKEYMODE_WRITE)
    // {
    //     Debug_println("Invalid app key metadata - aborting");
    //     drivewire_error();
    //     return;
    // }

    // // Make sure we have an SD card mounted
    // if (fnSDFAT.running() == false)
    // {
    //     Debug_println("No SD mounted - can't write app key");
    //     drivewire_error();
    //     return;
    // }

    // char *filename = _generate_appkey_filename(&_current_appkey);

    // // Reset the app key data so we require calling APPKEY OPEN before another attempt
    // _current_appkey.creator = 0;
    // _current_appkey.mode = APPKEYMODE_INVALID;

    // Debug_printf("Writing appkey to \"%s\"\n", filename);

    // // Make sure we have a "/FujiNet" directory, since that's where we're putting these files
    // fnSDFAT.create_path("/FujiNet");

    // FILE *fOut = fnSDFAT.file_open(filename, "w");
    // if (fOut == nullptr)
    // {
    //     Debug_printf("Failed to open/create output file: errno=%d\n", errno);
    //     drivewire_error();
    //     return;
    // }
    // size_t count = fwrite(value, 1, keylen, fOut);
    // int e = errno;

    // fclose(fOut);

    // if (count != keylen)
    // {
    //     Debug_printf("Only wrote %u bytes of expected %hu, errno=%d\n", count, keylen, e);
    //     drivewire_error();
    // }

    // drivewire_complete();
}

/*
 Read an "app key" from SD (ONLY!) storage
*/
void drivewireFuji::read_app_key()
{
    // Debug_println("Fuji cmd: READ APPKEY");

    // // Make sure we have an SD card mounted
    // if (fnSDFAT.running() == false)
    // {
    //     Debug_println("No SD mounted - can't read app key");
    //     drivewire_error();
    //     return;
    // }

    // // Make sure we have valid app key information
    // if (_current_appkey.creator == 0 || _current_appkey.mode != APPKEYMODE_READ)
    // {
    //     Debug_println("Invalid app key metadata - aborting");
    //     drivewire_error();
    //     return;
    // }

    // char *filename = _generate_appkey_filename(&_current_appkey);

    // Debug_printf("Reading appkey from \"%s\"\n", filename);

    // FILE *fIn = fnSDFAT.file_open(filename, "r");
    // if (fIn == nullptr)
    // {
    //     Debug_printf("Failed to open input file: errno=%d\n", errno);
    //     drivewire_error();
    //     return;
    // }

    // struct
    // {
    //     uint16_t size;
    //     uint8_t value[MAX_APPKEY_LEN];
    // } __attribute__((packed)) response;
    // memset(&response, 0, sizeof(response));

    // size_t count = fread(response.value, 1, sizeof(response.value), fIn);

    // fclose(fIn);
    // Debug_printf("Read %d bytes from input file\n", count);

    // response.size = count;

    // bus_to_computer((uint8_t *)&response, sizeof(response), false);
}

// DEBUG TAPE
void drivewireFuji::debug_tape()
{
    // // if not mounted then disable cassette and do nothing
    // // if mounted then activate cassette
    // // if mounted and active, then deactivate
    // // no longer need to handle file open/close
    // if (_cassetteDev.is_mounted() == true)
    // {
    //     if (_cassetteDev.is_active() == false)
    //     {
    //         Debug_println("::debug_tape ENABLE");
    //         _cassetteDev.drivewire_enable_cassette();
    //     }
    //     else
    //     {
    //         Debug_println("::debug_tape DISABLE");
    //         _cassetteDev.drivewire_disable_cassette();
    //     }
    // }
    // else
    // {
    //     Debug_println("::debug_tape NO CAS FILE MOUNTED");
    //     Debug_println("::debug_tape DISABLE");
    //     _cassetteDev.drivewire_disable_cassette();
    // }
}

// Disk Image Unmount
void drivewireFuji::disk_image_umount()
{
    // uint8_t deviceSlot = cmdFrame.aux1;

    // Debug_printf("Fuji cmd: UNMOUNT IMAGE 0x%02X\n", deviceSlot);

    // // Handle disk slots
    // if (deviceSlot < MAX_DISK_DEVICES)
    // {
    //     _fnDisks[deviceSlot].disk_dev.unmount();
    //     if (_fnDisks[deviceSlot].disk_type == MEDIATYPE_CAS || _fnDisks[deviceSlot].disk_type == MEDIATYPE_WAV)
    //     {
    //         // tell cassette it unmount
    //         _cassetteDev.umount_cassette_file();
    //         _cassetteDev.drivewire_disable_cassette();
    //     }
    //     _fnDisks[deviceSlot].disk_dev.device_active = false;
    //     _fnDisks[deviceSlot].reset();
    // }
    // // Handle tape
    // // else if (deviceSlot == BASE_TAPE_SLOT)
    // // {
    // // }
    // // Invalid slot
    // else
    // {
    //     drivewire_error();
    //     return;
    // }

    // drivewire_complete();
}

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

// This gets called when we're about to shutdown/reboot
void drivewireFuji::shutdown()
{
    for (int i = 0; i < MAX_DISK_DEVICES; i++)
        _fnDisks[i].disk_dev.unmount();
}

void drivewireFuji::open_directory()
{
    // Debug_println("Fuji cmd: OPEN DIRECTORY");

    // char dirpath[256];
    // uint8_t hostSlot = cmdFrame.aux1;
    // uint8_t ck = bus_to_peripheral((uint8_t *)&dirpath, sizeof(dirpath));

    // if (drivewire_checksum((uint8_t *)&dirpath, sizeof(dirpath)) != ck)
    // {
    //     drivewire_error();
    //     return;
    // }
    // if (!_validate_host_slot(hostSlot))
    // {
    //     drivewire_error();
    //     return;
    // }

    // // If we already have a directory open, close it first
    // if (_current_open_directory_slot != -1)
    // {
    //     Debug_print("Directory was already open - closign it first\n");
    //     _fnHosts[_current_open_directory_slot].dir_close();
    //     _current_open_directory_slot = -1;
    // }

    // // See if there's a search pattern after the directory path
    // const char *pattern = nullptr;
    // int pathlen = strnlen(dirpath, sizeof(dirpath));
    // if (pathlen < sizeof(dirpath) - 3) // Allow for two NULLs and a 1-char pattern
    // {
    //     pattern = dirpath + pathlen + 1;
    //     int patternlen = strnlen(pattern, sizeof(dirpath) - pathlen - 1);
    //     if (patternlen < 1)
    //         pattern = nullptr;
    // }

    // // Remove trailing slash
    // if (pathlen > 1 && dirpath[pathlen - 1] == '/')
    //     dirpath[pathlen - 1] = '\0';

    // Debug_printf("Opening directory: \"%s\", pattern: \"%s\"\n", dirpath, pattern ? pattern : "");

    // if (_fnHosts[hostSlot].dir_open(dirpath, pattern, 0))
    // {
    //     _current_open_directory_slot = hostSlot;
    //     drivewire_complete();
    // }
    // else
    //     drivewire_error();
}

void _set_additional_direntry_details(fsdir_entry_t *f, uint8_t *dest, uint8_t maxlen)
{
    //     // File modified date-time
    //     struct tm *modtime = localtime(&f->modified_time);
    //     modtime->tm_mon++;
    //     modtime->tm_year -= 70;

    //     dest[0] = modtime->tm_year;
    //     dest[1] = modtime->tm_mon;
    //     dest[2] = modtime->tm_mday;
    //     dest[3] = modtime->tm_hour;
    //     dest[4] = modtime->tm_min;
    //     dest[5] = modtime->tm_sec;

    //     // File size
    //     uint16_t fsize = f->size;
    //     dest[6] = LOBYTE_FROM_UINT16(fsize);
    //     dest[7] = HIBYTE_FROM_UINT16(fsize);

    //     // File flags
    // #define FF_DIR 0x01
    // #define FF_TRUNC 0x02

    //     dest[8] = f->isDir ? FF_DIR : 0;

    //     maxlen -= 10; // Adjust the max return value with the number of additional bytes we're copying
    //     if (f->isDir) // Also subtract a byte for a terminating slash on directories
    //         maxlen--;
    //     if (strlen(f->filename) >= maxlen)
    //         dest[8] |= FF_TRUNC;

    //     // File type
    //     dest[9] = MediaType::discover_disktype(f->filename);
}

void drivewireFuji::read_directory_entry()
{
    //     uint8_t maxlen = cmdFrame.aux1;
    //     Debug_printf("Fuji cmd: READ DIRECTORY ENTRY (max=%hu)\n", maxlen);

    //     // Make sure we have a current open directory
    //     if (_current_open_directory_slot == -1)
    //     {
    //         Debug_print("No currently open directory\n");
    //         drivewire_error();
    //         return;
    //     }

    //     char current_entry[256];

    //     fsdir_entry_t *f = _fnHosts[_current_open_directory_slot].dir_nextfile();

    //     if (f == nullptr)
    //     {
    //         Debug_println("Reached end of of directory");
    //         current_entry[0] = 0x7F;
    //         current_entry[1] = 0x7F;
    //     }
    //     else
    //     {
    //         Debug_printf("::read_direntry \"%s\"\n", f->filename);

    //         int bufsize = sizeof(current_entry);
    //         char *filenamedest = current_entry;

    // #define ADDITIONAL_DETAILS_BYTES 10
    //         // If 0x80 is set on AUX2, send back additional information
    //         if (cmdFrame.aux2 & 0x80)
    //         {
    //             _set_additional_direntry_details(f, (uint8_t *)current_entry, maxlen);
    //             // Adjust remaining size of buffer and file path destination
    //             bufsize = sizeof(current_entry) - ADDITIONAL_DETAILS_BYTES;
    //             filenamedest = current_entry + ADDITIONAL_DETAILS_BYTES;
    //         }
    //         else
    //         {
    //             bufsize = maxlen;
    //         }

    //         //int filelen = strlcpy(filenamedest, f->filename, bufsize);
    //         int filelen = util_ellipsize(f->filename, filenamedest, bufsize);

    //         // Add a slash at the end of directory entries
    //         if (f->isDir && filelen < (bufsize - 2))
    //         {
    //             current_entry[filelen] = '/';
    //             current_entry[filelen + 1] = '\0';
    //         }
    //     }

    //     bus_to_computer((uint8_t *)current_entry, maxlen, false);
}

void drivewireFuji::get_directory_position()
{
    // Debug_println("Fuji cmd: GET DIRECTORY POSITION");

    // // Make sure we have a current open directory
    // if (_current_open_directory_slot == -1)
    // {
    //     Debug_print("No currently open directory\n");
    //     drivewire_error();
    //     return;
    // }

    // uint16_t pos = _fnHosts[_current_open_directory_slot].dir_tell();
    // if (pos == FNFS_INVALID_DIRPOS)
    // {
    //     drivewire_error();
    //     return;
    // }
    // // Return the value we read
    // bus_to_computer((uint8_t *)&pos, sizeof(pos), false);
}

void drivewireFuji::set_directory_position()
{
    // Debug_println("Fuji cmd: SET DIRECTORY POSITION");

    // // DAUX1 and DAUX2 hold the position to seek to in low/high order
    // uint16_t pos = UINT16_FROM_HILOBYTES(cmdFrame.aux2, cmdFrame.aux1);

    // // Make sure we have a current open directory
    // if (_current_open_directory_slot == -1)
    // {
    //     Debug_print("No currently open directory\n");
    //     drivewire_error();
    //     return;
    // }

    // bool result = _fnHosts[_current_open_directory_slot].dir_seek(pos);
    // if (result == false)
    // {
    //     drivewire_error();
    //     return;
    // }
    // drivewire_complete();
}

void drivewireFuji::close_directory()
{
    // Debug_println("Fuji cmd: CLOSE DIRECTORY");

    // if (_current_open_directory_slot != -1)
    //     _fnHosts[_current_open_directory_slot].dir_close();

    // _current_open_directory_slot = -1;
    // drivewire_complete();
}

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

    fnUartBUS.write((uint8_t *)&cfg, sizeof(cfg));
}

//  Make new disk and shove into device slot
void drivewireFuji::new_disk()
{
    // Debug_println("Fuji cmd: NEW DISK");

    // struct
    // {
    //     unsigned short numSectors;
    //     unsigned short sectorSize;
    //     unsigned char hostSlot;
    //     unsigned char deviceSlot;
    //     char filename[MAX_FILENAME_LEN]; // WIll set this to MAX_FILENAME_LEN, later.
    // } newDisk;

    // // Ask for details on the new disk to create
    // uint8_t ck = bus_to_peripheral((uint8_t *)&newDisk, sizeof(newDisk));

    // if (ck != drivewire_checksum((uint8_t *)&newDisk, sizeof(newDisk)))
    // {
    //     Debug_print("drivewire_new_disk Bad checksum\n");
    //     drivewire_error();
    //     return;
    // }
    // if (newDisk.deviceSlot >= MAX_DISK_DEVICES || newDisk.hostSlot >= MAX_HOSTS)
    // {
    //     Debug_print("drivewire_new_disk Bad disk or host slot parameter\n");
    //     drivewire_error();
    //     return;
    // }
    // // A couple of reference variables to make things much easier to read...
    // fujiDisk &disk = _fnDisks[newDisk.deviceSlot];
    // fujiHost &host = _fnHosts[newDisk.hostSlot];

    // disk.host_slot = newDisk.hostSlot;
    // disk.access_mode = DISK_ACCESS_MODE_WRITE;
    // strlcpy(disk.filename, newDisk.filename, sizeof(disk.filename));

    // if (host.file_exists(disk.filename))
    // {
    //     Debug_printf("drivewire_new_disk File exists: \"%s\"\n", disk.filename);
    //     drivewire_error();
    //     return;
    // }

    // disk.fileh = host.file_open(disk.filename, disk.filename, sizeof(disk.filename), "w");
    // if (disk.fileh == nullptr)
    // {
    //     Debug_printf("drivewire_new_disk Couldn't open file for writing: \"%s\"\n", disk.filename);
    //     drivewire_error();
    //     return;
    // }

    // bool ok = disk.disk_dev.write_blank(disk.fileh, newDisk.sectorSize, newDisk.numSectors);
    // fclose(disk.fileh);

    // if (ok == false)
    // {
    //     Debug_print("drivewire_new_disk Data write failed\n");
    //     drivewire_error();
    //     return;
    // }

    // Debug_print("drivewire_new_disk succeeded\n");
    // drivewire_complete();
}

// Unmount specified host
void drivewireFuji::unmount_host()
{
    // Debug_println("Fuji cmd: UNMOUNT HOST");

    // unsigned char hostSlot = cmdFrame.aux1 - 1;

    // // Make sure we weren't given a bad hostSlot
    // if (!_validate_host_slot(hostSlot, "drivewire_tnfs_mount_hosts"))
    // {
    //     drivewire_error();
    //     return;
    // }

    // // Unmount any disks associated with host slot
    // for (int i = 0; i < MAX_DISK_DEVICES; i++)
    // {
    //     if (_fnDisks[i].host_slot == hostSlot)
    //     {
    //         _fnDisks[i].disk_dev.unmount();
    //         if (_fnDisks[i].disk_type == MEDIATYPE_CAS || _fnDisks[i].disk_type == MEDIATYPE_WAV)
    //         {
    //             // tell cassette it unmount
    //             _cassetteDev.umount_cassette_file();
    //             _cassetteDev.drivewire_disable_cassette();
    //         }
    //         _fnDisks[i].disk_dev.device_active = false;
    //         _fnDisks[i].reset();
    //     }
    // }

    // // Unmount the host
    // if (_fnHosts[hostSlot].umount())
    //     drivewire_error();
    // else
    //     drivewire_complete();
}

// Send host slot data to computer
void drivewireFuji::read_host_slots()
{
    Debug_println("Fuji cmd: READ HOST SLOTS");

    char hostSlots[MAX_HOSTS][MAX_HOSTNAME_LEN];
    memset(hostSlots, 0, sizeof(hostSlots));

    for (int i = 0; i < MAX_HOSTS; i++)
        strlcpy(hostSlots[i], _fnHosts[i].get_hostname(), MAX_HOSTNAME_LEN);

    for (int i = 0; i < MAX_HOSTS; i++)
        for (int j = 0; j < MAX_HOSTNAME_LEN; j++)
            fnUartBUS.write(hostSlots[i][j]);
}

// Read and save host slot data from computer
void drivewireFuji::write_host_slots()
{
    Debug_println("Fuji cmd: WRITE HOST SLOTS");

    char hostSlots[MAX_HOSTS][MAX_HOSTNAME_LEN];
    fnUartBUS.readBytes((uint8_t *)&hostSlots, sizeof(hostSlots));

    for (int i = 0; i < MAX_HOSTS; i++)
        _fnHosts[i].set_hostname(hostSlots[i]);

    _populate_config_from_slots();
    Config.save();
}

// Store host path prefix
void drivewireFuji::set_host_prefix()
{
    // char prefix[MAX_HOST_PREFIX_LEN];
    // uint8_t hostSlot = cmdFrame.aux1;

    // uint8_t ck = bus_to_peripheral((uint8_t *)prefix, MAX_FILENAME_LEN);

    // Debug_printf("Fuji cmd: SET HOST PREFIX %uh \"%s\"\n", hostSlot, prefix);

    // if (drivewire_checksum((uint8_t *)prefix, sizeof(prefix)) != ck)
    // {
    //     drivewire_error();
    //     return;
    // }

    // if (!_validate_host_slot(hostSlot))
    // {
    //     drivewire_error();
    //     return;
    // }

    // _fnHosts[hostSlot].set_prefix(prefix);
    // drivewire_complete();
}

// Retrieve host path prefix
void drivewireFuji::get_host_prefix()
{
    // uint8_t hostSlot = cmdFrame.aux1;
    // Debug_printf("Fuji cmd: GET HOST PREFIX %uh\n", hostSlot);

    // if (!_validate_host_slot(hostSlot))
    // {
    //     drivewire_error();
    //     return;
    // }
    // char prefix[MAX_HOST_PREFIX_LEN];
    // _fnHosts[hostSlot].get_prefix(prefix, sizeof(prefix));

    // bus_to_computer((uint8_t *)prefix, sizeof(prefix), false);
}

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
            strlcpy(diskSlots[i].filename, basename(filename), MAX_DISPLAY_FILENAME_LEN);
            free(filename);
        }
    }

    returnsize = sizeof(disk_slot) * MAX_DISK_DEVICES;

    fnUartBUS.write((uint8_t *)&diskSlots, returnsize);
}

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

    fnUartBUS.readBytes((uint8_t *)&diskSlots, sizeof(diskSlots));

    // Load the data into our current device array
    for (int i = 0; i < MAX_DISK_DEVICES; i++)
        _fnDisks[i].reset(diskSlots[i].filename, diskSlots[i].hostSlot, diskSlots[i].mode);

    // Save the data to disk
    _populate_config_from_slots();
    Config.save();
}

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

// AUX1 is our index value (from 0 to DRIVEWIRE_HISPEED_LOWEST_INDEX)
// AUX2 requests a save of the change if set to 1
void drivewireFuji::set_hdrivewire_index()
{
    // Debug_println("Fuji cmd: SET HDRIVEWIRE INDEX");

    // // DAUX1 holds the desired index value
    // uint8_t index = cmdFrame.aux1;

    // // Make sure it's a valid value
    // if (index > DRIVEWIRE_HISPEED_LOWEST_INDEX)
    // {
    //     drivewire_error();
    //     return;
    // }

    // DRIVEWIRE.setHighSpeedIndex(index);

    // // Go ahead and save it if AUX2 = 1
    // if (cmdFrame.aux2 & 1)
    // {
    //     Config.store_general_hdrivewireindex(index);
    //     Config.save();
    // }

    // drivewire_complete();
}

// Write a 256 byte filename to the device slot
void drivewireFuji::set_device_filename()
{
    // char tmp[MAX_FILENAME_LEN];

    // // AUX1 is the desired device slot
    // uint8_t slot = cmdFrame.aux1;
    // // AUX2 contains the host slot and the mount mode (READ/WRITE)
    // uint8_t host = cmdFrame.aux2 >> 4;
    // uint8_t mode = cmdFrame.aux2 & 0x0F;

    // uint8_t ck = bus_to_peripheral((uint8_t *)tmp, MAX_FILENAME_LEN);

    // Debug_printf("Fuji cmd: SET DEVICE SLOT 0x%02X/%02X/%02X FILENAME: %s\n", slot, host, mode, tmp);

    // if (drivewire_checksum((uint8_t *)tmp, MAX_FILENAME_LEN) != ck)
    // {
    //     drivewire_error();
    //     return;
    // }

    // // Handle DISK slots
    // if (slot < MAX_DISK_DEVICES)
    // {
    //     memcpy(_fnDisks[cmdFrame.aux1].filename, tmp, MAX_FILENAME_LEN);
    //     _fnDisks[cmdFrame.aux1].host_slot = host;
    //     _fnDisks[cmdFrame.aux1].access_mode = mode;
    //     _populate_config_from_slots();
    // }
    // // Handle TAPE slots
    // // else if (slot == BASE_TAPE_SLOT) // TODO? currently do not use this option for CAS image filenames
    // // {
    // //     // Just save the filename until we need it mount the tape
    // //     // TODO: allow read and write options
    // //     Config.store_mount(0, host, tmp, fnConfig::mount_mode_t::MOUNTMODE_READ, fnConfig::MOUNTTYPE_TAPE);
    // // }
    // // Bad slot
    // else
    // {
    //     Debug_println("BAD DEVICE SLOT");
    //     drivewire_error();
    //     return;
    // }

    // Config.save();
    // drivewire_complete();
}

// Get a 256 byte filename from device slot
void drivewireFuji::get_device_filename()
{
    // char tmp[MAX_FILENAME_LEN];
    // unsigned char err = false;

    // // AUX1 is the desired device slot
    // uint8_t slot = cmdFrame.aux1;

    // if (slot > 7)
    // {
    //     err = true;
    // }

    // memcpy(tmp, _fnDisks[cmdFrame.aux1].filename, MAX_FILENAME_LEN);
    // bus_to_computer((uint8_t *)tmp, MAX_FILENAME_LEN, err);
}

// Set an external clock rate in kHz defined by aux1/aux2, aux2 in steps of 2kHz.
void drivewireFuji::set_drivewire_external_clock()
{
    // unsigned short speed = drivewire_get_aux();
    // int baudRate = speed * 1000;

    // Debug_printf("drivewireFuji::set_external_clock(%u)\n", baudRate);

    // if (speed == 0)
    // {
    //     DRIVEWIRE.setUltraHigh(false, 0);
    // }
    // else
    // {
    //     DRIVEWIRE.setUltraHigh(true, baudRate);
    // }

    // drivewire_complete();
}

// Mounts the desired boot disk number
void drivewireFuji::insert_boot_device(uint8_t d)
{
    Debug_printf("insert_boot_device()\n");

    const char *config_atr = "/autorun.dsk";
    FILE *fBoot;
    size_t sz = 0;

    _bootDisk.unmount();

    switch (d)
    {
    case 0:
        fBoot = fsFlash.file_open(config_atr);
        fseek(fBoot, 0, SEEK_END);
        sz = ftell(fBoot);
        fseek(fBoot, 0, SEEK_SET);
        _bootDisk.mount(fBoot, config_atr, sz);
        break;
    }

    _bootDisk.is_config_device = true;
    _bootDisk.device_active = true;
}

// Set UDP Stream HOST & PORT and start it
void drivewireFuji::enable_udpstream()
{
    // char host[64];

    // uint8_t ck = bus_to_peripheral((uint8_t *)&host, sizeof(host));

    // if (drivewire_checksum((uint8_t *)&host, sizeof(host)) != ck)
    //     drivewire_error();
    // else
    // {
    //     int port = (cmdFrame.aux1 << 8) | cmdFrame.aux2;

    //     Debug_printf("Fuji cmd ENABLE UDPSTREAM: HOST:%s PORT: %d\n", host, port);

    //     // Save the host and port
    //     Config.store_udpstream_host(host);
    //     Config.store_udpstream_port(port);
    //     Config.save();

    //     drivewire_complete();

    //     // Start the UDP Stream
    //     DRIVEWIRE.setUDPHost(host, port);
    // }
}

// Initializes base settings and adds our devices to the DRIVEWIRE bus
void drivewireFuji::setup(systemBus *drivewirebus)
{
    Debug_printf("theFuji.setup()\n");
    // set up Fuji device
    _drivewire_bus = drivewirebus;

    _populate_slots_from_config();

    insert_boot_device(Config.get_general_boot_mode());

    // Disable booting from CONFIG if our settings say to turn it off
    boot_config = Config.get_general_config_enabled();

    // Disable status_wait if our settings say to turn it off
    status_wait_enabled = Config.get_general_status_wait_enabled();
}

drivewireDisk *drivewireFuji::bootdisk()
{
    return &_bootDisk;
}

int drivewireFuji::get_disk_id(int drive_slot)
{
    return drive_slot; // silly
    // return _fnDisks[drive_slot].disk_dev.id();
}

std::string drivewireFuji::get_host_prefix(int host_slot)
{
    return _fnHosts[host_slot].get_prefix();
}

void drivewireFuji::process()
{
    uint8_t c = fnUartBUS.read();

    switch (c)
    {
    case FUJICMD_GET_ADAPTERCONFIG:
        get_adapter_config();
        break;
    case FUJICMD_GET_SCAN_RESULT:
        net_scan_result();
        break;
    case FUJICMD_SCAN_NETWORKS:
        net_scan_networks();
        break;
    case FUJICMD_SET_SSID:
        net_set_ssid();
        break;
    case FUJICMD_GET_SSID:
        net_get_ssid();
        break;
    case FUJICMD_READ_HOST_SLOTS:
        read_host_slots();
        break;
    case FUJICMD_READ_DEVICE_SLOTS:
        read_device_slots();
        break;
    case FUJICMD_WRITE_HOST_SLOTS:
        write_host_slots();
        break;
    case FUJICMD_GET_WIFI_ENABLED:
        net_get_wifi_enabled();
        break;
    case FUJICMD_GET_WIFISTATUS:
        net_get_wifi_status();
        break;
    default:
        break;
    }
}

#endif /* BUILD_COCO */