#ifdef BUILD_ATARI

#include "fuji.h"

#ifdef ESP_PLATFORM
#include <driver/ledc.h>
#endif

#include <cstdint>
#include <cstring>
#include <errno.h>
#ifndef ESP_PLATFORM // why ESP does not like it? it throws a linker error undefined reference to 'basename'
#include <libgen.h>
#endif
#include <map>
#include <vector>
#include "compat_string.h"

#include "../../../include/debug.h"

#include "fnSystem.h"
#include "fnConfig.h"
#include "fsFlash.h"
#include "fnFsTNFS.h"
#include "fnWiFi.h"

#include "led.h"
#include "utils.h"
#include "string_utils.h"

#include "base64.h"
#include "hash.h"

#define ADDITIONAL_DETAILS_BYTES 10

sioFuji theFuji; // global fuji device object

// sioDisk sioDiskDevs[MAX_HOSTS];
sioNetwork sioNetDevs[MAX_NETWORK_DEVICES];

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

// Constructor
sioFuji::sioFuji()
{
    // Helpful for debugging
    for (int i = 0; i < MAX_HOSTS; i++)
        _fnHosts[i].slotid = i;
}

// Status
void sioFuji::sio_status()
{
    Debug_println("Fuji cmd: STATUS");

    char ret[4] = {0};

    bus_to_computer((uint8_t *)ret, sizeof(ret), false);
    return;
}

// Reset FujiNet
void sioFuji::sio_reset_fujinet()
{
    Debug_println("Fuji cmd: REBOOT");
    sio_complete();
    fnSystem.reboot();
}

// Scan for networks
void sioFuji::sio_net_scan_networks()
{
    Debug_println("Fuji cmd: SCAN NETWORKS");

    char ret[4] = {0};

    _countScannedSSIDs = fnWiFi.scan_networks();

    ret[0] = _countScannedSSIDs;

    bus_to_computer((uint8_t *)ret, 4, false);
}

// Return scanned network entry
void sioFuji::sio_net_scan_result()
{
    Debug_println("Fuji cmd: GET SCAN RESULT");

    // Response to  FUJICMD_GET_SCAN_RESULT
    struct
    {
        char ssid[MAX_SSID_LEN + 1];
        uint8_t rssi;
    } detail;

    bool err = false;
    if (cmdFrame.aux1 < _countScannedSSIDs)
        fnWiFi.get_scan_result(cmdFrame.aux1, detail.ssid, &detail.rssi);
    else
    {
        memset(&detail, 0, sizeof(detail));
        err = true;
    }

    bus_to_computer((uint8_t *)&detail, sizeof(detail), err);
}

//  Get SSID
void sioFuji::sio_net_get_ssid()
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

    bus_to_computer((uint8_t *)&cfg, sizeof(cfg), false);
}

// Set SSID
void sioFuji::sio_net_set_ssid()
{
    Debug_println("Fuji cmd: SET SSID");
    int i;

    // Data for  FUJICMD_SET_SSID
    struct
    {
        char ssid[MAX_SSID_LEN + 1];
        char password[MAX_WIFI_PASS_LEN];
    } cfg;

    uint8_t ck = bus_to_peripheral((uint8_t *)&cfg, sizeof(cfg));

    if (sio_checksum((uint8_t *)&cfg, sizeof(cfg)) != ck) {
        sio_error();
        return;
    }

    bool save = cmdFrame.aux1 != 0;

    // Debug_printf("Connecting to net: >%s< password: >%s<\r\n", cfg.ssid, cfg.password);

    int test_result = fnWiFi.test_connect(cfg.ssid, cfg.password);
    if (test_result != 0)
    {
        Debug_println("Could not connect to target SSID. Aborting save.");
        sio_error();
        return;
    }

    // Only save these if we're asked to, otherwise assume it was a test for connectivity
    if (save)
    {
        // 1. if this is a new SSID and not in the old stored, we should push the current one to the top of the stored configs, and everything else down.
        // 2. If this was already in the stored configs, push the stored one to the top, remove the new one from stored so it becomes current only.
        // 3. if this is same as current, then just save it again. User reconnected to current, nothing to change in stored. This is default if above don't happen

        int ssid_in_stored = -1;
        for (i = 0; i < MAX_WIFI_STORED; i++)
        {
            if (Config.get_wifi_stored_ssid(i) == cfg.ssid)
            {
                ssid_in_stored = i;
                break;
            }
        }

        // case 1
        if (ssid_in_stored == -1 && Config.have_wifi_info() && Config.get_wifi_ssid() != cfg.ssid)
        {
            Debug_println("Case 1: Didn't find new ssid in stored, and it's new. Pushing everything down 1 and old current to 0");
            // Move enabled stored down one, last one will drop off
            for (int j = MAX_WIFI_STORED - 1; j > 0; j--)
            {
                bool enabled = Config.get_wifi_stored_enabled(j - 1);
                if (!enabled)
                    continue;

                Config.store_wifi_stored_ssid(j, Config.get_wifi_stored_ssid(j - 1));
                Config.store_wifi_stored_passphrase(j, Config.get_wifi_stored_passphrase(j - 1));
                Config.store_wifi_stored_enabled(j, true); // already confirmed this is enabled
            }
            // push the current to the top of stored
            Config.store_wifi_stored_ssid(0, Config.get_wifi_ssid());
            Config.store_wifi_stored_passphrase(0, Config.get_wifi_passphrase());
            Config.store_wifi_stored_enabled(0, true);
        }

        // case 2
        if (ssid_in_stored != -1 && Config.have_wifi_info() && Config.get_wifi_ssid() != cfg.ssid)
        {
            Debug_printf("Case 2: Found new ssid in stored at %d, and it's not current (should never happen). Pushing everything down 1 and old current to 0\n", ssid_in_stored);
            // found the new SSID at ssid_in_stored, so move everything above it down one slot, and store the current at 0
            for (int j = ssid_in_stored; j > 0; j--)
            {
                Config.store_wifi_stored_ssid(j, Config.get_wifi_stored_ssid(j - 1));
                Config.store_wifi_stored_passphrase(j, Config.get_wifi_stored_passphrase(j - 1));
                Config.store_wifi_stored_enabled(j, true);
            }

            // push the current to the top of stored
            Config.store_wifi_stored_ssid(0, Config.get_wifi_ssid());
            Config.store_wifi_stored_passphrase(0, Config.get_wifi_passphrase());
            Config.store_wifi_stored_enabled(0, true);
        }

        // save the new SSID as current
        Config.store_wifi_ssid(cfg.ssid, sizeof(cfg.ssid));
        // Clear text here, it will be encrypted internally if enabled for encryption
        Config.store_wifi_passphrase(cfg.password, sizeof(cfg.password));

        Config.save();
    }
    Debug_println("Restarting WiFiManager");
    fnWiFi.start();

    // give it a few seconds to restart the WiFi before we return to the client, who will immediately start checking status
    // and get errors if we're not up yet
    fnSystem.delay(3000);

    sio_complete();
}

// Get WiFi Status
void sioFuji::sio_net_get_wifi_status()
{
    Debug_println("Fuji cmd: GET WIFI STATUS");
    // WL_CONNECTED = 3, WL_DISCONNECTED = 6
    uint8_t wifiStatus = fnWiFi.connected() ? 3 : 6;
    bus_to_computer(&wifiStatus, sizeof(wifiStatus), false);
}

// Check if Wifi is enabled
void sioFuji::sio_net_get_wifi_enabled()
{
    uint8_t e = Config.get_wifi_enabled() ? 1 : 0;
    Debug_printf("Fuji cmd: GET WIFI ENABLED: %d\n", e);
    bus_to_computer(&e, sizeof(e), false);
}

// Mount Server
#ifdef ESP_PLATFORM // TODO merge
void sioFuji::sio_mount_host()
{
    Debug_println("Fuji cmd: MOUNT HOST");

    unsigned char hostSlot = cmdFrame.aux1;

    // Make sure we weren't given a bad hostSlot
    if (!_validate_host_slot(hostSlot, "sio_tnfs_mount_hosts"))
    {
        sio_error();
        return;
    }

    if (!_fnHosts[hostSlot].mount())
        sio_error();
    else
        sio_complete();
}
#else
int sioFuji::sio_mount_host(bool siomode, int slot)
{
    Debug_println("Fuji cmd: MOUNT HOST");

    unsigned char hostSlot = siomode ? cmdFrame.aux1 : slot;

    // Make sure we weren't given a bad hostSlot
    if (!_validate_host_slot(hostSlot, "sio_tnfs_mount_hosts"))
    {
        return _on_error(siomode);
    }

    if (!_fnHosts[hostSlot].mount())
        return _on_error(siomode);
    else
        return _on_ok(siomode);
}
#endif

// Disk Image Mount
#ifdef ESP_PLATFORM  // TODO merge
void sioFuji::sio_disk_image_mount()
{
    // TAPE or CASSETTE handling: this function can also mount CAS and WAV files
    // to the C: device. Everything stays the same here and the mounting
    // where all the magic happens is done in the sioDisk::mount() function.
    // This function opens the file, so cassette does not need to open the file.
    // Cassette needs the file pointer and file size.

    Debug_println("Fuji cmd: MOUNT IMAGE");

    uint8_t deviceSlot = cmdFrame.aux1;
    uint8_t options = cmdFrame.aux2; // DISK_ACCESS_MODE

    // TODO: Implement FETCH?
    char flag[3] = {'r', 0, 0};
    if (options == DISK_ACCESS_MODE_WRITE)
        flag[1] = '+';

    // Make sure we weren't given a bad hostSlot
    if (!_validate_device_slot(deviceSlot))
    {
        sio_error();
        return;
    }

    if (!_validate_host_slot(_fnDisks[deviceSlot].host_slot))
    {
        sio_error();
        return;
    }

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
        sio_error();
        return;
    }

    // We've gotten this far, so make sure our bootable CONFIG disk is disabled
    boot_config = false;
    status_wait_count = 0;

    // We need the file size for loading XEX files and for CASSETTE, so get that too
    disk.disk_size = host.file_size(disk.fileh);

    // And now mount it
    disk.disk_type = disk.disk_dev.mount(disk.fileh, disk.filename, disk.disk_size);

    sio_complete();
}
#else
int sioFuji::sio_disk_image_mount(bool siomode, int slot)
{
    // TAPE or CASSETTE handling: this function can also mount CAS and WAV files
    // to the C: device. Everything stays the same here and the mounting
    // where all the magic happens is done in the sioDisk::mount() function.
    // This function opens the file, so cassette does not need to open the file.
    // Cassette needs the file pointer and file size.

    uint8_t deviceSlot = siomode ? cmdFrame.aux1 : slot;
    uint8_t options = siomode ? cmdFrame.aux2 : _fnDisks[slot].access_mode; // DISK_ACCESS_MODE

    Debug_printf("Fuji cmd: MOUNT IMAGE 0x%02X 0x%02X\n", deviceSlot, options);

    // TODO: Implement FETCH?
    char flag[4] = {'r', 'b', 0, 0};
    if (options == DISK_ACCESS_MODE_WRITE)
        flag[2] = '+';

    // Make sure we weren't given a bad hostSlot
    if (!_validate_device_slot(deviceSlot))
    {
        return _on_error(siomode);
    }

    if (!_validate_host_slot(_fnDisks[deviceSlot].host_slot))
    {
        return _on_error(siomode);
    }

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
        return _on_error(siomode);
    }

    // We've gotten this far, so make sure our bootable CONFIG disk is disabled
    boot_config = false;
    status_wait_count = 0;

    // We need the file size for loading XEX files and for CASSETTE, so get that too
    disk.disk_size = host.file_size(disk.fileh);

    // And now mount it
    disk.disk_type = disk.disk_dev.mount(disk.fileh, disk.filename, disk.disk_size);

    return _on_ok(siomode);
}
#endif

// Toggle boot config on/off, aux1=0 is disabled, aux1=1 is enabled
void sioFuji::sio_set_boot_config()
{
    boot_config = cmdFrame.aux1;
    sio_complete();
}

// Do SIO copy
void sioFuji::sio_copy_file()
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

    dataBuf = (char *)malloc(532);

    if (dataBuf == nullptr)
    {
        sio_error();
        return;
    }

    memset(&csBuf, 0, sizeof(csBuf));

    ck = bus_to_peripheral(csBuf, sizeof(csBuf));

    if (ck != sio_checksum(csBuf, sizeof(csBuf)))
    {
        sio_error();
        free(dataBuf);
        return;
    }

    copySpec = std::string((char *)csBuf);

    Debug_printf("copySpec: %s\n", copySpec.c_str());

    // Check for malformed copyspec.
    if (copySpec.empty() || copySpec.find_first_of("|") == std::string::npos)
    {
        sio_error();
        free(dataBuf);
        return;
    }

    if (cmdFrame.aux1 < 1 || cmdFrame.aux1 > 8)
    {
        sio_error();
        free(dataBuf);
        return;
    }

    if (cmdFrame.aux2 < 1 || cmdFrame.aux2 > 8)
    {
        sio_error();
        free(dataBuf);
        return;
    }

    sourceSlot = cmdFrame.aux1 - 1;
    destSlot = cmdFrame.aux2 - 1;

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
        sio_error();
        free(dataBuf);
        return;
    }

    destFile = _fnHosts[destSlot].fnfile_open(destPath.c_str(), (char *)destPath.c_str(), destPath.size() + 1, FILE_WRITE);

    if (destFile == nullptr)
    {
        sio_error();
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
        sio_error();
        Debug_printf("Copy File Error! wCount: %d, rCount: %d, rTotal: %d, Expect: %d\n", writeCount, readCount, readTotal, expected);
    }
    else
    {
        sio_complete();
    }

    // copyEnd:
    fnio::fclose(sourceFile);
    fnio::fclose(destFile);
    free(dataBuf);
}

// Mount all
#ifdef ESP_PLATFORM
void sioFuji::mount_all()
#else
int sioFuji::mount_all(bool siomode)
#endif
{
    bool nodisks = true; // Check at the end if no disks are in a slot and disable config

    for (int i = 0; i < 8; i++)
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
#ifdef ESP_PLATFORM
                sio_error();
                return;
#else
                return _on_error(siomode);
#endif
            }

            Debug_printf("Selecting '%s' from host #%u as %s on D%u:\n",
                         disk.filename, disk.host_slot, flag, i + 1);

            disk.fileh = host.fnfile_open(disk.filename, disk.filename, sizeof(disk.filename), flag);

            if (disk.fileh == nullptr)
            {
#ifdef ESP_PLATFORM
                sio_error();
                return;
#else
                return _on_error(siomode);
#endif
            }

            // We've gotten this far, so make sure our bootable CONFIG disk is disabled
            boot_config = false;
            status_wait_count = 0;

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

#ifdef ESP_PLATFORM
    sio_complete();
#else
    return _on_ok(siomode);
#endif
}

// Set boot mode
void sioFuji::sio_set_boot_mode()
{
    insert_boot_device(cmdFrame.aux1);
    boot_config = true;
    sio_complete();
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
void sioFuji::sio_open_app_key()
{
    Debug_print("Fuji cmd: OPEN APPKEY\n");

    // The data expected for this command
    uint8_t ck = bus_to_peripheral((uint8_t *)&_current_appkey, sizeof(_current_appkey));

    if (sio_checksum((uint8_t *)&_current_appkey, sizeof(_current_appkey)) != ck)
    {
        sio_error();
        return;
    }

    // We're only supporting writing to SD, so return an error if there's no SD mounted
    if (fnSDFAT.running() == false)
    {
        Debug_println("No SD mounted - returning error");
        sio_error();
        return;
    }

    // Basic check for valid data
    if (_current_appkey.creator == 0 || _current_appkey.mode == APPKEYMODE_INVALID)
    {
        Debug_println("Invalid app key data");
        sio_error();
        return;
    }

    appkey_size = get_value_or_default(mode_to_keysize,  _current_appkey.mode, 64);

    Debug_printf("App key creator = 0x%04hx, app = 0x%02hhx, key = 0x%02hhx, mode = %hhu, filename = \"%s\"\n",
                 _current_appkey.creator, _current_appkey.app, _current_appkey.key, _current_appkey.mode,
                 _generate_appkey_filename(&_current_appkey));

    sio_complete();
}

/*
  The app key close operation is a placeholder in case we want to provide more robust file
  read/write operations. Currently, the file is closed immediately after the read or write operation.
*/
void sioFuji::sio_close_app_key()
{
    Debug_print("Fuji cmd: CLOSE APPKEY\n");
    _current_appkey.creator = 0;
    _current_appkey.mode = APPKEYMODE_INVALID;
    sio_complete();
}

/*
 Write an "app key" to SD (ONLY!) storage.
*/
void sioFuji::sio_write_app_key()
{
    uint16_t keylen = UINT16_FROM_HILOBYTES(cmdFrame.aux2, cmdFrame.aux1);
    // std::copy(&data_buffer[0], &data_buffer[0] + keylen, data.begin());

    Debug_printf("Fuji cmd: WRITE APPKEY (keylen = %hu)\n", keylen);

    std::vector<uint8_t> value(appkey_size, 0);

    uint8_t ck = bus_to_peripheral((uint8_t *)value.data(), value.size());
    if (sio_checksum(value.data(), value.size()) != ck)
    {
        // apc: don't send 'E' on checksum error, 'N' was sent already
        // sio_error();
        return;
    }

    // Make sure we have valid app key information
    if (_current_appkey.creator == 0 || _current_appkey.mode != APPKEYMODE_WRITE)
    {
        Debug_println("Invalid app key metadata - aborting");
        sio_error();
        return;
    }

    // Make sure we have an SD card mounted
    if (fnSDFAT.running() == false)
    {
        Debug_println("No SD mounted - can't write app key");
        sio_error();
        return;
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
        sio_error();
        return;
    }
    size_t count = fwrite(value.data(), 1, keylen, fOut);
    int e = errno;

    fclose(fOut);

    if (count != keylen)
    {
        Debug_printf("Only wrote %u bytes of expected %hu, errno=%d\n", (unsigned)count, keylen, e);
        sio_error();
    }

    sio_complete();
}

size_t read_file_into_vector(FILE* fIn, std::vector<uint8_t>& response_data, size_t size) {
    response_data.resize(size + 2);
    size_t bytes_read = fread(response_data.data() + 2, 1, size, fIn);

    // Insert the size at the beginning of the vector
    response_data[0] = static_cast<uint8_t>(bytes_read & 0xFF); // Low byte of the size
    response_data[1] = static_cast<uint8_t>((bytes_read >> 8) & 0xFF); // High byte of the size
    return bytes_read;
}

/*
 Read an "app key" from SD (ONLY!) storage
*/
void sioFuji::sio_read_app_key()
{
    Debug_println("Fuji cmd: READ APPKEY");
    std::vector<uint8_t> response_data(appkey_size + 2);

    // Make sure we have an SD card mounted
    if (fnSDFAT.running() == false)
    {
        Debug_println("No SD mounted - can't read app key");
        bus_to_computer(response_data.data(), response_data.size(), true);
        return;
    }

    // Make sure we have valid app key information, and the mode is not WRITE
    if (_current_appkey.creator == 0 || _current_appkey.mode == APPKEYMODE_WRITE)
    {
        Debug_println("Invalid app key metadata - aborting");
        bus_to_computer(response_data.data(), response_data.size(), true);
        return;
    }

    char *filename = _generate_appkey_filename(&_current_appkey);
    Debug_printf("Reading appkey from \"%s\"\n", filename);

    FILE *fIn = fnSDFAT.file_open(filename, FILE_READ);
    if (fIn == nullptr)
    {
        Debug_printf("Failed to open input file: errno=%d\n", errno);
        bus_to_computer(response_data.data(), response_data.size(), true);
        return;
    }

    size_t count = read_file_into_vector(fIn, response_data, appkey_size);
    Debug_printf("Read %u bytes from input file\n", (unsigned)count);
    fclose(fIn);

#ifdef DEBUG
	std::string msg = util_hexdump(response_data.data(), appkey_size);
	Debug_printf("%s\n", msg.c_str());
#endif

    bus_to_computer(response_data.data(), response_data.size(), false);
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

#ifndef ESP_PLATFORM
int sioFuji::_on_ok(bool siomode)
{
    if (siomode) sio_complete();
    return 0;
}

int sioFuji::_on_error(bool siomode, int rc)
{
    if (siomode) sio_error();
    return rc;
}
#endif

// Disk Image Unmount
#ifdef ESP_PLATFORM
void sioFuji::sio_disk_image_umount()
{
    uint8_t deviceSlot = cmdFrame.aux1;
#else
int sioFuji::sio_disk_image_umount(bool siomode, int slot)
{
    uint8_t deviceSlot = siomode ? cmdFrame.aux1 : slot;
#endif

    Debug_printf("Fuji cmd: UNMOUNT IMAGE 0x%02X\n", deviceSlot);

    // Handle disk slots
    if (deviceSlot < MAX_DISK_DEVICES)
    {
        _fnDisks[deviceSlot].disk_dev.unmount();
        if (_fnDisks[deviceSlot].disk_type == MEDIATYPE_CAS || _fnDisks[deviceSlot].disk_type == MEDIATYPE_WAV)
        {
            // tell cassette it unmount
            _cassetteDev.umount_cassette_file();
            _cassetteDev.sio_disable_cassette();
        }
        _fnDisks[deviceSlot].disk_dev.device_active = false;
        _fnDisks[deviceSlot].reset();
    }
    // Handle tape
    // else if (deviceSlot == BASE_TAPE_SLOT)
    // {
    // }
    // Invalid slot
#ifdef ESP_PLATFORM
    else
    {
        sio_error();
        return;
    }

    sio_complete();
#else
    else
    {
        return _on_error(siomode);
    }

    return _on_ok(siomode);
#endif
}

// Disk Image Rotate
/*
  We rotate disks my changing their disk device ID's. That prevents
  us from having to unmount and re-mount devices.
*/
void sioFuji::image_rotate()
{
    Debug_println("Fuji cmd: IMAGE ROTATE");

    int count = 0;
    // Find the first empty slot, stop at 8 so we don't catch the cassette
    while (_fnDisks[count].fileh != nullptr && count < 8)
        count++;

    if (count > 1)
    {
        count--;

        // Save the device ID of the disk in the last slot
        int last_id = _fnDisks[count].disk_dev.id();

        for (int n = count; n > 0; n--)
        {
            int swap = _fnDisks[n - 1].disk_dev.id();
            Debug_printf("setting slot %d to ID %x\n", n, swap);
            _sio_bus->changeDeviceId(&_fnDisks[n].disk_dev, swap);
        }

        // The first slot gets the device ID of the last slot
        Debug_printf("setting slot %d to ID %x\n", 0, last_id);
        _sio_bus->changeDeviceId(&_fnDisks[0].disk_dev, last_id);

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
    }
}

// This gets called when we're about to shutdown/reboot
void sioFuji::shutdown()
{
    for (int i = 0; i < MAX_DISK_DEVICES; i++)
        _fnDisks[i].disk_dev.unmount();
}

void sioFuji::sio_open_directory()
{
    Debug_println("Fuji cmd: OPEN DIRECTORY");

    char dirpath[256];
    uint8_t hostSlot = cmdFrame.aux1;
    uint8_t ck = bus_to_peripheral((uint8_t *)&dirpath, sizeof(dirpath));

    if (sio_checksum((uint8_t *)&dirpath, sizeof(dirpath)) != ck)
    {
        sio_error();
        return;
    }
    if (!_validate_host_slot(hostSlot))
    {
        sio_error();
        return;
    }

    // If we already have a directory open, close it first
    if (_current_open_directory_slot != -1)
    {
        Debug_print("Directory was already open - closing it first\n");
        _fnHosts[_current_open_directory_slot].dir_close();
        _current_open_directory_slot = -1;
    }

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
        sio_complete();
    }
    else
        sio_error();
}

void _set_additional_direntry_details(fsdir_entry_t *f, uint8_t *dest, uint8_t maxlen)
{
    // File modified date-time
    struct tm *modtime = localtime(&f->modified_time);
    modtime->tm_mon++;
    modtime->tm_year -= 70;

    dest[0] = modtime->tm_year;
    dest[1] = modtime->tm_mon;
    dest[2] = modtime->tm_mday;
    dest[3] = modtime->tm_hour;
    dest[4] = modtime->tm_min;
    dest[5] = modtime->tm_sec;

    // File size
    uint16_t fsize = f->size;
    dest[6] = LOBYTE_FROM_UINT16(fsize);
    dest[7] = HIBYTE_FROM_UINT16(fsize);

    // File flags
#define FF_DIR 0x01
#define FF_TRUNC 0x02

    dest[8] = f->isDir ? FF_DIR : 0;

    maxlen -= 10; // Adjust the max return value with the number of additional bytes we're copying
    if (f->isDir) // Also subtract a byte for a terminating slash on directories
        maxlen--;
    if (strlen(f->filename) >= maxlen)
        dest[8] |= FF_TRUNC;

    // File type
    dest[9] = MediaType::discover_disktype(f->filename);
}

// TODO: VERIFY THIS CODE. THE STASH SEEMED CORRUPT
void sioFuji::sio_read_directory_block()
{
    // aux1 holds entry size for each record
    uint8_t maxlen = cmdFrame.aux1;

    // aux2:
    // b0-2 = number of pages - 1 (i.e. 1 to 8)
    // b3,4 = not used
    // b5   = extended entry information (as per normal, adds 10 bytes of information to each entry at start)
    // b6,7 = block mode marker already checked.

    bool is_extended = ((cmdFrame.aux2 & 0x20) == 0x20);
    uint8_t pages = (cmdFrame.aux2 & 0x07) + 1;

    Debug_printf("Fuji cmd: READ DIRECTORY BLOCK (pages=%d, maxlen=%d, extended: %d)\n", pages, maxlen, is_extended);

    std::vector<uint8_t> response;
    std::vector<uint8_t> start_offsets; // holds all the offsets for each dir entry in the response
    std::vector<uint8_t> data_block;    // the data for each dir entry. no terminator char needed as we track the offsets. Double 0x7f is end of dir, and no more entries will come

    uint16_t response_max = pages * 256;

    if (_current_open_directory_slot == -1)
    {
        Debug_print("No currently open directory\n");
        sio_error();
        return;
    }

    bool is_eod = false;
    char current_entry[256];
    uint16_t num_entries = 0;
    uint16_t total_size = 9; // header bytes

    uint16_t initial_pos = _fnHosts[_current_open_directory_slot].dir_tell();

    // keep filling buffers up until it can't fit another maxlen (plus header bytes etc)
    // or we hit end of dir
    while ( !is_eod && num_entries < 256 )
    {
        uint16_t additional_size = 0;
        uint16_t pos_before_next = _fnHosts[_current_open_directory_slot].dir_tell();
        fsdir_entry_t *f = _fnHosts[_current_open_directory_slot].dir_nextfile();
        if (f == nullptr)
        {
            // reached end of dir
            is_eod = true;
            current_entry[0] = 0x7F;
            current_entry[1] = 0x7F;
            current_entry[2] = 0;
            additional_size = 2;
        }
        else
        {
            Debug_printf("::read_direntry \"%s\"\n", f->filename);

            int bufsize;
            char *filenamedest = current_entry;

            // If 0x80 is set on AUX2, send back additional information
            if (is_extended)
            {
                _set_additional_direntry_details(f, (uint8_t *)current_entry, maxlen);
                // Adjust remaining size of buffer and file path destination
                bufsize = sizeof(current_entry) - ADDITIONAL_DETAILS_BYTES;
                filenamedest = current_entry + ADDITIONAL_DETAILS_BYTES;
            }
            else
            {
                bufsize = maxlen;
            }

            int filelen = util_ellipsize(f->filename, filenamedest, bufsize);
            additional_size = filelen + (is_extended ? ADDITIONAL_DETAILS_BYTES : 0);

            // Add a slash at the end of directory entries
            if (f->isDir && filelen < (bufsize - 2))
            {
                current_entry[filelen] = '/';
                current_entry[filelen + 1] = '\0';
                additional_size++;
            }

        }

        // would this take us over the limit? 2 for start_offset bytes.
        uint16_t new_size = total_size + 2 + additional_size;

        if (new_size > response_max) {
            Debug_printf("skipping add, would have taken us to %d size. additional was: %d\n", new_size, additional_size);
            // reset to previous pos, and exit loop
            _fnHosts[_current_open_directory_slot].dir_seek(pos_before_next);
            break;
        } else {
            Debug_printf("adding additional entry with size: %d\n", additional_size);
        }


        start_offsets.push_back(static_cast<uint8_t>(data_block.size() & 0xFF)); // lo byte of current size (which is same as offset)
        start_offsets.push_back(static_cast<uint8_t>((data_block.size() >> 8) & 0xFF)); // high byte

        // add the string to the data block without the terminating null
        if (is_extended)
        {
            // strlen doesn't work as we have prepended some additional information
            // add the additional bytes first
            for(int i=0; i < ADDITIONAL_DETAILS_BYTES; i++)
            {
                data_block.push_back(static_cast<uint8_t>(current_entry[i]));
            }
            // Then add the string part
            //int s_len = std::strlen(current_entry + ADDITIONAL_DETAILS_BYTES);
            data_block.insert(data_block.end(), current_entry + ADDITIONAL_DETAILS_BYTES, current_entry + ADDITIONAL_DETAILS_BYTES + std::strlen(current_entry + ADDITIONAL_DETAILS_BYTES));
        } else {
            data_block.insert(data_block.end(), current_entry, current_entry + std::strlen(current_entry));
        }

        total_size = 9 + data_block.size() + start_offsets.size();
        Debug_printf("current sizes, data: %d, offsets: %d, total: %d\n", data_block.size(), start_offsets.size(), data_block.size() + start_offsets.size());


        num_entries++;
    }

    // ###################################################################
    // CREATE THE RESPONSE BLOCK:
    // ###################################################################
    // byte 0-1 = "MF" (Multi-File, take your pick :D )
    // byte 2   = Flags (currently 0x80 = Extended Information)
    // byte 3   = Max Size Per Entry (maxlen from input)
    // byte 4   = Num Entries in block (max 255)
    // byte 5-6 = Total size of block (i.e. size without padding)
    // byte 7-8 = First Position in block (i.e. dir pos value at start), allows up to 64k entries over all blocks
    // Num Entries x 2 = Offsets in Data for each entry
    // Data x Num Entries = data for each dir.
    //
    // All above is < pages x 256 in size

    // HEADER BYTES
    std::string headerBytes = "MF";
    response.insert(response.end(), headerBytes.begin(), headerBytes.end());

    // FLAGS
    uint8_t header_flags = is_extended ? 0x80 : 0;  // more flags may come
    response.push_back(header_flags);

    // MAX SIZE PER ENTRY
    response.push_back(maxlen);

    // NUM ENTRIES
    response.push_back(static_cast<uint8_t>(num_entries));

    // Total size
    int final_size = 9 + data_block.size() + start_offsets.size();
    response.push_back(static_cast<uint8_t>(final_size & 0xFF));
    response.push_back(static_cast<uint8_t>((final_size >> 8) & 0xFF));

    // INITIAL POS VALUE
    response.push_back(static_cast<uint8_t>(initial_pos & 0xFF));
    response.push_back(static_cast<uint8_t>((initial_pos >> 8) & 0xFF));

    // OFFSETS
    response.insert(response.end(), start_offsets.begin(), start_offsets.end());

    // DATA
    response.insert(response.end(), data_block.begin(), data_block.end());

    Debug_printf("Actual data size: %d to atari\n", response.size());
    std::string s = util_hexdump(response.data(), response.size());
    Debug_printf("dump: \n%s\n", s.c_str());

    // buffer with 0s to requested size
    response.resize(response_max, 0);

    bus_to_computer(response.data(), response_max, false);
}

void sioFuji::sio_read_directory_entry()
{
     if ((cmdFrame.aux2 & 0xC0) == 0xC0) {
        // Block mode directory entry
        sio_read_directory_block();
        return;
    }

    uint8_t maxlen = cmdFrame.aux1;
    Debug_printf("Fuji cmd: READ DIRECTORY ENTRY (max=%hu)\n", maxlen);

    // Make sure we have a current open directory
    if (_current_open_directory_slot == -1)
    {
        Debug_print("No currently open directory\n");
        sio_error();
        return;
    }

    char current_entry[256];

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
        char *filenamedest = current_entry;

        // If 0x80 is set on AUX2, send back additional information
        if (cmdFrame.aux2 & 0x80)
        {
            _set_additional_direntry_details(f, (uint8_t *)current_entry, maxlen);
            // Adjust remaining size of buffer and file path destination
            bufsize = maxlen - ADDITIONAL_DETAILS_BYTES;
            filenamedest = current_entry + ADDITIONAL_DETAILS_BYTES;
        }
        else
        {
            bufsize = maxlen;
        }

        // int filelen = strlcpy(filenamedest, f->filename, bufsize);
        int filelen = util_ellipsize(f->filename, filenamedest, bufsize);

        // Add a slash at the end of directory entries
        if (f->isDir && filelen < (bufsize - 2))
        {
            current_entry[filelen] = '/';
            current_entry[filelen + 1] = '\0';
        }
    }

    bus_to_computer((uint8_t *)current_entry, maxlen, false);
}

void sioFuji::sio_get_directory_position()
{
    Debug_println("Fuji cmd: GET DIRECTORY POSITION");

    // Make sure we have a current open directory
    if (_current_open_directory_slot == -1)
    {
        Debug_print("No currently open directory\n");
        sio_error();
        return;
    }

    uint16_t pos = _fnHosts[_current_open_directory_slot].dir_tell();
    if (pos == FNFS_INVALID_DIRPOS)
    {
        sio_error();
        return;
    }
    // Return the value we read
    bus_to_computer((uint8_t *)&pos, sizeof(pos), false);
}

void sioFuji::sio_set_directory_position()
{
    Debug_println("Fuji cmd: SET DIRECTORY POSITION");

    // DAUX1 and DAUX2 hold the position to seek to in low/high order
    uint16_t pos = UINT16_FROM_HILOBYTES(cmdFrame.aux2, cmdFrame.aux1);

    // Make sure we have a current open directory
    if (_current_open_directory_slot == -1)
    {
        Debug_print("No currently open directory\n");
        sio_error();
        return;
    }

    bool result = _fnHosts[_current_open_directory_slot].dir_seek(pos);
    if (result == false)
    {
        sio_error();
        return;
    }
    sio_complete();
}

void sioFuji::sio_close_directory()
{
    Debug_println("Fuji cmd: CLOSE DIRECTORY");

    if (_current_open_directory_slot != -1)
        _fnHosts[_current_open_directory_slot].dir_close();

    _current_open_directory_slot = -1;
    sio_complete();
}

void sioFuji::sio_get_adapter_config_extended()
{
    // return string versions of the data rather than just bytes
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

    bus_to_computer((uint8_t *)&cfg, sizeof(cfg), false);

}

// Get network adapter configuration
void sioFuji::sio_get_adapter_config()
{
    Debug_printf("Fuji cmd: GET ADAPTER CONFIG\r\n");

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

    bus_to_computer((uint8_t *)&cfg, sizeof(cfg), false);
}

//  Make new disk and shove into device slot
void sioFuji::sio_new_disk()
{
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
    uint8_t ck = bus_to_peripheral((uint8_t *)&newDisk, sizeof(newDisk));

    if (ck != sio_checksum((uint8_t *)&newDisk, sizeof(newDisk)))
    {
        Debug_print("sio_new_disk Bad checksum\n");
        sio_error();
        return;
    }
    if (newDisk.deviceSlot >= MAX_DISK_DEVICES || newDisk.hostSlot >= MAX_HOSTS)
    {
        Debug_print("sio_new_disk Bad disk or host slot parameter\n");
        sio_error();
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
        sio_error();
        return;
    }

    disk.fileh = host.fnfile_open(disk.filename, disk.filename, sizeof(disk.filename), FILE_WRITE);
    if (disk.fileh == nullptr)
    {
        Debug_printf("sio_new_disk Couldn't open file for writing: \"%s\"\n", disk.filename);
        sio_error();
        return;
    }

    bool ok = disk.disk_dev.write_blank(disk.fileh, newDisk.sectorSize, newDisk.numSectors);
    fnio::fclose(disk.fileh);

    if (ok == false)
    {
        Debug_print("sio_new_disk Data write failed\n");
        sio_error();
        return;
    }

    Debug_print("sio_new_disk succeeded\n");
    sio_complete();
}

// Unmount specified host
void sioFuji::sio_unmount_host()
{
    Debug_println("Fuji cmd: UNMOUNT HOST");

    unsigned char hostSlot = cmdFrame.aux1 - 1;

    // Make sure we weren't given a bad hostSlot
    if (!_validate_host_slot(hostSlot, "sio_tnfs_mount_hosts"))
    {
        sio_error();
        return;
    }

    // Unmount any disks associated with host slot
    for (int i = 0; i < MAX_DISK_DEVICES; i++)
    {
        if (_fnDisks[i].host_slot == hostSlot)
        {
            _fnDisks[i].disk_dev.unmount();
            if (_fnDisks[i].disk_type == MEDIATYPE_CAS || _fnDisks[i].disk_type == MEDIATYPE_WAV)
            {
                // tell cassette it unmount
                _cassetteDev.umount_cassette_file();
                _cassetteDev.sio_disable_cassette();
            }
            _fnDisks[i].disk_dev.device_active = false;
            _fnDisks[i].reset();
        }
    }

    // Unmount the host
    if (_fnHosts[hostSlot].umount())
        sio_error();
    else
        sio_complete();
}

// Send host slot data to computer
void sioFuji::sio_read_host_slots()
{
    Debug_println("Fuji cmd: READ HOST SLOTS");

    char hostSlots[MAX_HOSTS][MAX_HOSTNAME_LEN];
    memset(hostSlots, 0, sizeof(hostSlots));

    for (int i = 0; i < MAX_HOSTS; i++)
        strlcpy(hostSlots[i], _fnHosts[i].get_hostname(), MAX_HOSTNAME_LEN);

    bus_to_computer((uint8_t *)&hostSlots, sizeof(hostSlots), false);
}

// Read and save host slot data from computer
void sioFuji::sio_write_host_slots()
{
    Debug_println("Fuji cmd: WRITE HOST SLOTS");

    char hostSlots[MAX_HOSTS][MAX_HOSTNAME_LEN];
    uint8_t ck = bus_to_peripheral((uint8_t *)&hostSlots, sizeof(hostSlots));

    if (sio_checksum((uint8_t *)hostSlots, sizeof(hostSlots)) == ck)
    {
        for (int i = 0; i < MAX_HOSTS; i++)
            _fnHosts[i].set_hostname(hostSlots[i]);

        _populate_config_from_slots();
        Config.save();

        sio_complete();
    }
    else
        sio_error();
}

// Store host path prefix
void sioFuji::sio_set_host_prefix()
{
    char prefix[MAX_HOST_PREFIX_LEN];
    uint8_t hostSlot = cmdFrame.aux1;

    uint8_t ck = bus_to_peripheral((uint8_t *)prefix, MAX_FILENAME_LEN);

    Debug_printf("Fuji cmd: SET HOST PREFIX %uh \"%s\"\n", hostSlot, prefix);

    if (sio_checksum((uint8_t *)prefix, sizeof(prefix)) != ck)
    {
        sio_error();
        return;
    }

    if (!_validate_host_slot(hostSlot))
    {
        sio_error();
        return;
    }

    _fnHosts[hostSlot].set_prefix(prefix);
    sio_complete();
}

// Retrieve host path prefix
void sioFuji::sio_get_host_prefix()
{
    uint8_t hostSlot = cmdFrame.aux1;
    Debug_printf("Fuji cmd: GET HOST PREFIX %uh\n", hostSlot);

    if (!_validate_host_slot(hostSlot))
    {
        sio_error();
        return;
    }
    char prefix[MAX_HOST_PREFIX_LEN];
    _fnHosts[hostSlot].get_prefix(prefix, sizeof(prefix));

    bus_to_computer((uint8_t *)prefix, sizeof(prefix), false);
}

// Send device slot data to computer
void sioFuji::sio_read_device_slots()
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

    // AUX1 specifies which slots to return
    // Handle disk slots
    if (cmdFrame.aux1 == READ_DEVICE_SLOTS_DISKS1)
    {
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
    }
    // Handle tape slot
    // else if (cmdFrame.aux1 == READ_DEVICE_SLOTS_TAPE)
    // {
    //     // TODO: Populate this with real values
    //     // TODO: allow read and write
    //     // TODO: why [0] and not [8] (device 9)?
    //     diskSlots[0].mode = 0; // Always READ
    //     diskSlots[0].hostSlot = 0;
    //     strlcpy(diskSlots[0].filename, "TAPETEST.CAS", MAX_DISPLAY_FILENAME_LEN);

    //     returnsize = sizeof(disk_slot);
    // }
    // Bad AUX1 value
    else
    {
        sio_error();
        return;
    }

    bus_to_computer((uint8_t *)&diskSlots, returnsize, false);
}

// Read and save disk slot data from computer
void sioFuji::sio_write_device_slots()
{
    Debug_println("Fuji cmd: WRITE DEVICE SLOTS");

    struct
    {
        uint8_t hostSlot;
        uint8_t mode;
        char filename[MAX_DISPLAY_FILENAME_LEN];
    } diskSlots[MAX_DISK_DEVICES];

    uint8_t ck = bus_to_peripheral((uint8_t *)&diskSlots, sizeof(diskSlots));

    if (ck == sio_checksum((uint8_t *)&diskSlots, sizeof(diskSlots)))
    {
        // Load the data into our current device array
        for (int i = 0; i < MAX_DISK_DEVICES; i++)
            _fnDisks[i].reset(diskSlots[i].filename, diskSlots[i].hostSlot, diskSlots[i].mode);

        // Save the data to disk
        _populate_config_from_slots();
        Config.save();

        sio_complete();
    }
    else
        sio_error();
}

// Temporary(?) function while we move from old config storage to new
void sioFuji::_populate_slots_from_config()
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
void sioFuji::_populate_config_from_slots()
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

// AUX1 is our index value (from 0 to SIO_HISPEED_LOWEST_INDEX, for FN-PC 0 .. 10, 16)
// AUX2 requests a save of the change if set to 1
void sioFuji::sio_set_hsio_index()
{
    Debug_println("Fuji cmd: SET HSIO INDEX");

    // DAUX1 holds the desired index value
    uint8_t index = cmdFrame.aux1;

    // Make sure it's a valid value
#ifdef ESP_PLATFORM
    if (index > SIO_HISPEED_LOWEST_INDEX)
#else
    if (index > SIO_HISPEED_LOWEST_INDEX && index != SIO_HISPEED_x2_INDEX) // accept 0 .. 10, 16
#endif
    {
        sio_error();
        return;
    }

    SIO.setHighSpeedIndex(index);

    // Go ahead and save it if AUX2 = 1
    if (cmdFrame.aux2 & 1)
    {
        Config.store_general_hsioindex(index);
        Config.save();
    }

    sio_complete();
}

// Write a 256 byte filename to the device slot
void sioFuji::sio_set_device_filename()
{
    char tmp[MAX_FILENAME_LEN];

    // AUX1 is the desired device slot
    uint8_t deviceSlot = cmdFrame.aux1;
    // AUX2 contains the host slot and the mount mode (READ/WRITE)
    uint8_t host = cmdFrame.aux2 >> 4;
    uint8_t mode = cmdFrame.aux2 & 0x0F;

    uint8_t ck = bus_to_peripheral((uint8_t *)tmp, MAX_FILENAME_LEN);

    Debug_printf("Fuji cmd: SET DEVICE SLOT 0x%02X/%02X/%02X FILENAME: %s\n", deviceSlot, host, mode, tmp);

    if (sio_checksum((uint8_t *)tmp, MAX_FILENAME_LEN) != ck)
    {
        sio_error();
        return;
    }

    // Handle DISK slots
    if (deviceSlot < MAX_DISK_DEVICES)
    {
        memcpy(_fnDisks[deviceSlot].filename, tmp, MAX_FILENAME_LEN);
        // If the filename is empty, mark this as an invalid host, so that mounting will ignore it too
        if (strlen(_fnDisks[deviceSlot].filename) == 0) {
            _fnDisks[deviceSlot].host_slot = INVALID_HOST_SLOT;
        } else {
            _fnDisks[deviceSlot].host_slot = host;
        }
        _fnDisks[deviceSlot].access_mode = mode;
        _populate_config_from_slots();
    }
    // Handle TAPE slots
    // else if (slot == BASE_TAPE_SLOT) // TODO? currently do not use this option for CAS image filenames
    // {
    //     // Just save the filename until we need it mount the tape
    //     // TODO: allow read and write options
    //     Config.store_mount(0, host, tmp, fnConfig::mount_mode_t::MOUNTMODE_READ, fnConfig::MOUNTTYPE_TAPE);
    // }
    // Bad slot
    else
    {
        Debug_println("BAD DEVICE SLOT");
        sio_error();
        return;
    }

    Config.save();
    sio_complete();
}

// Get a 256 byte filename from device slot
void sioFuji::sio_get_device_filename()
{
    char tmp[MAX_FILENAME_LEN];
    unsigned char err = false;

    // AUX1 is the desired device slot
    uint8_t slot = cmdFrame.aux1;

    if (slot > 7)
    {
        err = true;
    }

    memcpy(tmp, _fnDisks[cmdFrame.aux1].filename, MAX_FILENAME_LEN);
    bus_to_computer((uint8_t *)tmp, MAX_FILENAME_LEN, err);
}

// Set an external clock rate in kHz defined by aux1/aux2, aux2 in steps of 2kHz.
void sioFuji::sio_set_sio_external_clock()
{
    unsigned short speed = sio_get_aux();
    int baudRate = speed * 1000;

    Debug_printf("sioFuji::sio_set_external_clock(%u)\n", baudRate);

    if (speed == 0)
    {
        SIO.setUltraHigh(false, 0);
    }
    else
    {
        SIO.setUltraHigh(true, baudRate);
    }

    sio_complete();
}

// Mounts the desired boot disk number
void sioFuji::insert_boot_device(uint8_t d)
{
    const char *config_atr = "/autorun.atr";
    std::string altconfigfile = Config.get_config_filename();
    const char *alt_config_atr = altconfigfile.c_str();
    const char *mount_all_atr = "/mount-and-boot.atr";
#ifdef ESP_PLATFORM // TODO merge
    fnFile *fBoot;

    _bootDisk.unmount();

    switch (d)
    {
    case 0:
        if( !altconfigfile.empty() && fnSDFAT.running() != false )
        {
            fBoot = fnSDFAT.fnfile_open(alt_config_atr);
            // if open fails, fall back to default config
            if (fBoot != nullptr)
            {
                _bootDisk.mount(fBoot, alt_config_atr, 0);
                Debug_printf("Mounted Alternate CONFIG %s\n", alt_config_atr);
                break;
            }
        }
        fBoot = fsFlash.fnfile_open(config_atr);
        _bootDisk.mount(fBoot, config_atr, 0);
        break;
    case 1:
        fBoot = fsFlash.fnfile_open(mount_all_atr);
        _bootDisk.mount(fBoot, mount_all_atr, 0);
        break;
    case 2:
        Debug_printf("Mounting lobby server\n");
        if (fnTNFS.start("tnfs.fujinet.online"))
        {
            Debug_printf("opening lobby.\n");
            fBoot = fnTNFS.fnfile_open("/ATARI/_lobby.xex");
            _bootDisk.mount(fBoot, "/ATARI/_lobby.xex", 0);
        }
        break;
    }
#else
    const char *lobby_tnfs = "tnfs.fujinet.online";
    const char *lobby_xex = "/ATARI/_lobby.xex";
    const char *boot_img;

    fnFile *fBoot = nullptr;

    _bootDisk.unmount();

    switch (d)
    {
    case 0:
        if( !altconfigfile.empty() && fnSDFAT.running() != false )
        {
            fBoot = fnSDFAT.fnfile_open(alt_config_atr);
            // if open fails, fall back to default config
            if (fBoot != nullptr)
            {
                Debug_printf("Using Alternate CONFIG %s\n", alt_config_atr);
                boot_img = alt_config_atr;
                break;
            }
        }
        boot_img = config_atr;
        fBoot = fsFlash.fnfile_open(boot_img);
        break;
    case 1:
        boot_img = mount_all_atr;
        fBoot = fsFlash.fnfile_open(boot_img);
        break;
    case 2:
        Debug_printf("Mounting lobby server\n");
        if (fnTNFS.start(lobby_tnfs))
        {
            Debug_printf("opening lobby.\n");
            boot_img = lobby_xex;
            fBoot = fnTNFS.fnfile_open(boot_img);
        }
        break;
    default:
        Debug_printf("Invalid boot mode: %d\n", d);
        return;
    }

    // check if open was successfull
    if (fBoot == nullptr)
    {
        Debug_printf("Failed to open boot disk image: %s\n", boot_img);
        return;
    }

    _bootDisk.mount(fBoot, boot_img ,0);
#endif

    _bootDisk.is_config_device = true;
    _bootDisk.device_active = false;
}

// Set UDP Stream HOST & PORT and start it
void sioFuji::sio_enable_udpstream()
{
    char host[64];

    uint8_t ck = bus_to_peripheral((uint8_t *)&host, sizeof(host));

    if (sio_checksum((uint8_t *)&host, sizeof(host)) != ck)
        sio_error();
    else
    {
        int port = (cmdFrame.aux1 << 8) | cmdFrame.aux2;

        Debug_printf("Fuji cmd ENABLE UDPSTREAM: HOST:%s PORT: %d\n", host, port);

        // Save the host and port
        Config.store_udpstream_host(host);
        Config.store_udpstream_port(port);
        Config.save();

        sio_complete();

        // Start the UDP Stream
        SIO.setUDPHost(host, port);
    }
}

// Initializes base settings and adds our devices to the SIO bus
void sioFuji::setup(systemBus *siobus)
{
    // set up Fuji device
    _sio_bus = siobus;

    _populate_slots_from_config();

    insert_boot_device(Config.get_general_boot_mode());

    // Disable booting from CONFIG if our settings say to turn it off
    boot_config = Config.get_general_config_enabled();

    // Disable status_wait if our settings say to turn it off
    status_wait_enabled = Config.get_general_status_wait_enabled();

    // Add our devices to the SIO bus
    for (int i = 0; i < MAX_DISK_DEVICES; i++)
        _sio_bus->addDevice(&_fnDisks[i].disk_dev, SIO_DEVICEID_DISK + i);

    for (int i = 0; i < MAX_NETWORK_DEVICES; i++)
        _sio_bus->addDevice(&sioNetDevs[i], SIO_DEVICEID_FN_NETWORK + i);

    _sio_bus->addDevice(&_cassetteDev, SIO_DEVICEID_CASSETTE);
    cassette()->set_buttons(Config.get_cassette_buttons());
    cassette()->set_pulldown(Config.get_cassette_pulldown());
}

sioDisk *sioFuji::bootdisk()
{
    return &_bootDisk;
}

void sioFuji::sio_base64_encode_input()
{
    uint16_t len = sio_get_aux();

    Debug_printf("FUJI: BASE64 ENCODE INPUT\n");

    if (!len)
    {
        Debug_printf("Invalid length. Aborting");
        sio_error();
        return;
    }

    std::vector<unsigned char> p(len);
    bus_to_peripheral(p.data(), len);
    base64.base64_buffer += std::string((const char *)p.data(), len);
    sio_complete();
}

void sioFuji::sio_base64_encode_compute()
{
    size_t out_len;

    Debug_printf("FUJI: BASE64 ENCODE COMPUTE\n");

    std::unique_ptr<char[]> p = Base64::encode(base64.base64_buffer.c_str(), base64.base64_buffer.size(), &out_len);
    if (!p)
    {
        Debug_printf("base64_encode compute failed\n");
        sio_error();
        return;
    }

    base64.base64_buffer.clear();
    base64.base64_buffer = std::string(p.get(), out_len);

    Debug_printf("Resulting BASE64 encoded data is: %u bytes\n", out_len);
    sio_complete();
}

void sioFuji::sio_base64_encode_length()
{
    Debug_printf("FUJI: BASE64 ENCODE LENGTH\n");

    size_t l = base64.base64_buffer.length();
    uint8_t response[4] = {
        (uint8_t)(l >>  0),
        (uint8_t)(l >>  8),
        (uint8_t)(l >>  16),
        (uint8_t)(l >>  24)
    };

    if (!l)
    {
        Debug_printf("BASE64 buffer is 0 bytes, sending error.\n");
        bus_to_computer(response, sizeof(response), true);
    }

    Debug_printf("base64 buffer length: %u bytes\n", l);

    bus_to_computer(response, sizeof(response), false);
}

void sioFuji::sio_base64_encode_output()
{
    Debug_printf("FUJI: BASE64 ENCODE OUTPUT\n");

    size_t len = sio_get_aux();

    if (!len)
    {
        Debug_printf("Refusing to send a zero byte buffer. Aborting\n");
        return;
    }
    else if (len > base64.base64_buffer.length())
    {
        Debug_printf("Requested %u bytes, but buffer is only %u bytes, aborting.\n", len, base64.base64_buffer.length());
        return;
    }
    else
    {
        Debug_printf("Requested %u bytes\n", len);
    }

    std::vector<unsigned char> p(len);
    std::memcpy(p.data(), base64.base64_buffer.data(), len);
    base64.base64_buffer.erase(0, len);
    base64.base64_buffer.shrink_to_fit();

    bus_to_computer(p.data(), len, false);
}

void sioFuji::sio_base64_decode_input()
{
    uint16_t len = sio_get_aux();

    Debug_printf("FUJI: BASE64 DECODE INPUT\n");

    if (!len)
    {
        Debug_printf("Invalid length. Aborting");
        sio_error();
        return;
    }

    std::vector<unsigned char> p(len);
    bus_to_peripheral(p.data(), len);
    base64.base64_buffer += std::string((const char *)p.data(), len);
    sio_complete();
}

void sioFuji::sio_base64_decode_compute()
{
    size_t out_len;

    Debug_printf("FUJI: BASE64 DECODE COMPUTE\n");

    std::unique_ptr<unsigned char[]> p = Base64::decode(base64.base64_buffer.c_str(), base64.base64_buffer.size(), &out_len);
    if (!p)
    {
        Debug_printf("base64_encode compute failed\n");
        sio_error();
        return;
    }

    base64.base64_buffer.clear();
    base64.base64_buffer = std::string((const char *)p.get(), out_len);

    Debug_printf("Resulting BASE64 encoded data is: %u bytes\n", out_len);
    sio_complete();
}

void sioFuji::sio_base64_decode_length()
{
    Debug_printf("FUJI: BASE64 DECODE LENGTH\n");

    size_t len = base64.base64_buffer.length();
    uint8_t response[4] = {
        (uint8_t)(len >>  0),
        (uint8_t)(len >>  8),
        (uint8_t)(len >>  16),
        (uint8_t)(len >>  24)
    };

    if (!len)
    {
        Debug_printf("BASE64 buffer is 0 bytes, sending error.\n");
        bus_to_computer(response, sizeof(response), true);
        return;
    }

    Debug_printf("base64 buffer length: %u bytes\n", len);

    bus_to_computer(response, sizeof(response), false);
}

void sioFuji::sio_base64_decode_output()
{
    Debug_printf("FUJI: BASE64 DECODE OUTPUT\n");

    size_t len = sio_get_aux();

    if (!len)
    {
        Debug_printf("Refusing to send a zero byte buffer. Aborting\n");
        sio_error();
        return;
    }
    else if (len > base64.base64_buffer.length())
    {
        Debug_printf("Requested %u bytes, but buffer is only %u bytes, aborting.\n", len, base64.base64_buffer.length());
        sio_error();
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
    bus_to_computer(p.data(), len, false);
}

void sioFuji::sio_hash_input()
{
    Debug_printf("FUJI: HASH INPUT\n");
    uint16_t len = sio_get_aux();
    if (!len)
    {
        Debug_printf("Invalid length. Aborting");
        sio_error();
        return;
    }

    std::vector<unsigned char> p(len);
    bus_to_peripheral(p.data(), len);
    hasher.add_data(p);
    sio_complete();
}

void sioFuji::sio_hash_compute(bool clear_data)
{
    Debug_printf("FUJI: HASH COMPUTE\n");
    algorithm = Hash::to_algorithm(sio_get_aux());
    hasher.compute(algorithm, clear_data);
    sio_complete();
}

void sioFuji::sio_hash_length()
{
    Debug_printf("FUJI: HASH LENGTH\n");
    uint16_t is_hex = sio_get_aux() == 1;
    uint8_t r = hasher.hash_length(algorithm, is_hex);
    bus_to_computer((uint8_t *)&r, 1, false);
}

void sioFuji::sio_hash_output()
{
    Debug_printf("FUJI: HASH OUTPUT\n");
    uint16_t is_hex = sio_get_aux() == 1;

    std::vector<uint8_t> hashed_data;
    if (is_hex) {
        std::string hex = hasher.output_hex();
        hashed_data.insert(hashed_data.end(), hex.begin(), hex.end());
    } else {
        hashed_data = hasher.output_binary();
    }
    bus_to_computer(hashed_data.data(), hashed_data.size(), false);
}

void sioFuji::sio_hash_clear()
{
    Debug_printf("FUJI: HASH CLEAR\n");
    hasher.clear();
    sio_complete();
}

void sioFuji::sio_process(uint32_t commanddata, uint8_t checksum)
{
    cmdFrame.commanddata = commanddata;
    cmdFrame.checksum = checksum;

    Debug_printf("sioFuji::sio_process() called, baud: %d\n", SIO.getBaudrate());

    switch (cmdFrame.comnd)
    {
    case FUJICMD_HSIO_INDEX:
        sio_ack();
        sio_high_speed();
        break;
    case FUJICMD_SET_HSIO_INDEX:
        sio_ack();
        sio_set_hsio_index();
        break;
    case FUJICMD_STATUS:
        sio_ack();
        sio_status();
        break;
    case FUJICMD_RESET:
        sio_ack();
        sio_reset_fujinet();
        break;
    case FUJICMD_SCAN_NETWORKS:
        sio_ack();
        sio_net_scan_networks();
        break;
    case FUJICMD_GET_SCAN_RESULT:
        sio_ack();
        sio_net_scan_result();
        break;
    case FUJICMD_SET_SSID:
        sio_late_ack();
        sio_net_set_ssid();
        break;
    case FUJICMD_GET_SSID:
        sio_ack();
        sio_net_get_ssid();
        break;
    case FUJICMD_GET_WIFISTATUS:
        sio_ack();
        sio_net_get_wifi_status();
        break;
    case FUJICMD_MOUNT_HOST:
        sio_ack();
        sio_mount_host();
        break;
    case FUJICMD_MOUNT_IMAGE:
        sio_ack();
        sio_disk_image_mount();
        break;
    case FUJICMD_OPEN_DIRECTORY:
        sio_late_ack();
        sio_open_directory();
        break;
    case FUJICMD_READ_DIR_ENTRY:
        sio_ack();
        sio_read_directory_entry();
        break;
    case FUJICMD_CLOSE_DIRECTORY:
        sio_ack();
        sio_close_directory();
        break;
    case FUJICMD_GET_DIRECTORY_POSITION:
        sio_ack();
        sio_get_directory_position();
        break;
    case FUJICMD_SET_DIRECTORY_POSITION:
        sio_ack();
        sio_set_directory_position();
        break;
    case FUJICMD_READ_HOST_SLOTS:
        sio_ack();
        sio_read_host_slots();
        break;
    case FUJICMD_WRITE_HOST_SLOTS:
        sio_late_ack();
        sio_write_host_slots();
        break;
    case FUJICMD_READ_DEVICE_SLOTS:
        sio_ack();
        sio_read_device_slots();
        break;
    case FUJICMD_WRITE_DEVICE_SLOTS:
        sio_late_ack();
        sio_write_device_slots();
        break;
    case FUJICMD_GET_WIFI_ENABLED:
        sio_ack();
        sio_net_get_wifi_enabled();
        break;
    case FUJICMD_UNMOUNT_IMAGE:
        sio_ack();
        sio_disk_image_umount();
        break;
    case FUJICMD_GET_ADAPTERCONFIG:
        sio_ack();
        sio_get_adapter_config();
        break;
    case FUJICMD_GET_ADAPTERCONFIG_EXTENDED:
        sio_ack();
        sio_get_adapter_config_extended();
        break;
    case FUJICMD_NEW_DISK:
        sio_late_ack();
        sio_new_disk();
        break;
    case FUJICMD_UNMOUNT_HOST:
        sio_ack();
        sio_unmount_host();
        break;
    case FUJICMD_SET_DEVICE_FULLPATH:
        sio_late_ack();
        sio_set_device_filename();
        break;
    case FUJICMD_SET_HOST_PREFIX:
        sio_late_ack();
        sio_set_host_prefix();
        break;
    case FUJICMD_GET_HOST_PREFIX:
        sio_ack();
        sio_get_host_prefix();
        break;
    case FUJICMD_SET_SIO_EXTERNAL_CLOCK:
        sio_ack();
        sio_set_sio_external_clock();
        break;
    case FUJICMD_WRITE_APPKEY:
        sio_late_ack();
        sio_write_app_key();
        break;
    case FUJICMD_READ_APPKEY:
        sio_ack();
        sio_read_app_key();
        break;
    case FUJICMD_OPEN_APPKEY:
        sio_late_ack();
        sio_open_app_key();
        break;
    case FUJICMD_CLOSE_APPKEY:
        sio_ack();
        sio_close_app_key();
        break;
    case FUJICMD_GET_DEVICE_FULLPATH:
        sio_ack();
        sio_get_device_filename();
        break;
    case FUJICMD_CONFIG_BOOT:
        sio_ack();
        sio_set_boot_config();
        break;
    case FUJICMD_COPY_FILE:
        sio_late_ack();
        sio_copy_file();
        break;
    case FUJICMD_MOUNT_ALL:
        sio_ack();
        mount_all();
        break;
    case FUJICMD_SET_BOOT_MODE:
        sio_ack();
        sio_set_boot_mode();
        break;
    case FUJICMD_ENABLE_UDPSTREAM:
        sio_late_ack();
        sio_enable_udpstream();
        break;
    case FUJICMD_BASE64_ENCODE_INPUT:
        sio_late_ack();
        sio_base64_encode_input();
        break;
    case FUJICMD_BASE64_ENCODE_COMPUTE:
        sio_ack();
        sio_base64_encode_compute();
        break;
    case FUJICMD_BASE64_ENCODE_LENGTH:
        sio_ack();
        sio_base64_encode_length();
        break;
    case FUJICMD_BASE64_ENCODE_OUTPUT:
        sio_ack();
        sio_base64_encode_output();
        break;
    case FUJICMD_BASE64_DECODE_INPUT:
        sio_late_ack();
        sio_base64_decode_input();
        break;
    case FUJICMD_BASE64_DECODE_COMPUTE:
        sio_ack();
        sio_base64_decode_compute();
        break;
    case FUJICMD_BASE64_DECODE_LENGTH:
        sio_ack();
        sio_base64_decode_length();
        break;
    case FUJICMD_BASE64_DECODE_OUTPUT:
        sio_ack();
        sio_base64_decode_output();
        break;
    case FUJICMD_HASH_INPUT:
        sio_late_ack();
        sio_hash_input();
        break;
    case FUJICMD_HASH_COMPUTE:
        sio_ack();
        sio_hash_compute(true);
        break;
    case FUJICMD_HASH_COMPUTE_NO_CLEAR:
        sio_ack();
        sio_hash_compute(false);
        break;
    case FUJICMD_HASH_LENGTH:
        sio_ack();
        sio_hash_length();
        break;
    case FUJICMD_HASH_OUTPUT:
        sio_ack();
        sio_hash_output();
        break;
    case FUJICMD_HASH_CLEAR:
        sio_ack();
        sio_hash_clear();
        break;
    default:
        sio_nak();
    }
}

int sioFuji::get_disk_id(int drive_slot)
{
    return _fnDisks[drive_slot].disk_dev.id();
}

std::string sioFuji::get_host_prefix(int host_slot)
{
    return _fnHosts[host_slot].get_prefix();
}

#endif /* BUILD_ATARI */
