#ifdef BUILD_ATARI

#include "sioFuji.h"
#include "httpService.h"
#include "utils.h"
#include "base64.h"
#include "../../qrcode/qrmanager.h"
#include "compat_string.h"
#include "fuji_endian.h"

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

// Set SSID
void sioFuji::sio_net_set_ssid()
{
    SSIDConfig cfg;
    transaction_continue(true);
    if (!transaction_get(&cfg, sizeof(cfg))) {
        transaction_error();
        return;
    }

    fujicore_net_set_ssid_success(cfg.ssid, cfg.password, cmdFrame.aux1);
    transaction_complete();
}

// Set SIO baudrate
void sioFuji::sio_set_baudrate()
{

    int br = 0;

    switch(cmdFrame.aux1) {

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
            transaction_error();
            return;
    }

    // send complete with current baudrate
    transaction_complete();

    SYSTEM_BUS.flushOutput();
#ifndef ESP_PLATFORM
    fnSystem.delay_microseconds(2000);
#endif
    SYSTEM_BUS.setBaudrate(br);
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
#ifndef ESP_PLATFORM
    uint64_t poll_ts = fnSystem.millis();
#endif

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
    return _set_additional_direntry_details(f, dest, maxlen, 70, SIZE_32_LE,
                                            HAS_DIR_ENTRY_FLAGS_SEPARATE, HAS_DIR_ENTRY_TYPE);
}

//  Make new disk and shove into device slot
void sioFuji::sio_new_disk()
{
    transaction_continue(true);
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
    if (!transaction_get(&newDisk, sizeof(newDisk)))
    {
        Debug_print("sio_new_disk Bad checksum\n");
        transaction_error();
        return;
    }
    if (newDisk.deviceSlot >= MAX_DISK_DEVICES || newDisk.hostSlot >= MAX_HOSTS)
    {
        Debug_print("sio_new_disk Bad disk or host slot parameter\n");
        transaction_error();
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
        transaction_error();
        return;
    }

    disk.fileh = host.fnfile_open(disk.filename, disk.filename, sizeof(disk.filename), FILE_WRITE);
    if (disk.fileh == nullptr)
    {
        Debug_printf("sio_new_disk Couldn't open file for writing: \"%s\"\n", disk.filename);
        transaction_error();
        return;
    }

    bool ok = disk.disk_dev.write_blank(disk.fileh, newDisk.sectorSize, newDisk.numSectors);
    fnio::fclose(disk.fileh);

    if (ok == false)
    {
        Debug_print("sio_new_disk Data write failed\n");
        transaction_error();
        return;
    }

    Debug_print("sio_new_disk succeeded\n");
    transaction_complete();
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
        transaction_error();
        return;
    }

    SYSTEM_BUS.setHighSpeedIndex(index);

    // Go ahead and save it if AUX2 = 1
    if (cmdFrame.aux2 & 1)
    {
        Config.store_general_hsioindex(index);
        Config.save();
    }

    transaction_complete();
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
    cassette()->set_buttons(Config.get_cassette_buttons());
    cassette()->set_pulldown(Config.get_cassette_pulldown());

#ifdef UNUSED
#ifndef ESP_PLATFORM // required for FN-PC, causes RAM overflow on ESP32
    SYSTEM_BUS.addDevice(&_udpDev, FUJI_DEVICEID_MIDI);
#endif
#endif /* UNUSED */
}

void sioFuji::sio_qrcode_input()
{
    transaction_continue(true);

    uint16_t len = sio_get_aux();

    Debug_printf("FUJI: QRCODE INPUT (len: %d)\n", len);

    if (!len)
    {
        Debug_printf("Invalid length. Aborting");
        transaction_error();
        return;
    }

    std::vector<unsigned char> p(len);
    transaction_get(p.data(), len);
    _qrManager.data += std::string((const char *)p.data(), len);
    transaction_complete();
}

void sioFuji::sio_qrcode_encode()
{
    uint16_t aux = sio_get_aux();
    uint8_t version = aux & 0b01111111;
    uint8_t ecc_mode = ((aux >> 8) & 0b00000011);
    bool shorten = (aux >> 12) & 0b00000001;

    Debug_printf("FUJI: QRCODE ENCODE\n");
    Debug_printf("QR Version: %d, ECC: %d, Shorten: %s\n", version, ecc_mode, shorten ? "Y" : "N");

    std::string url = _qrManager.data;

    if (shorten) {
        url = fnHTTPD.shorten_url(url);
    }

    _qrManager.version(version);
    _qrManager.ecc((qr_ecc_t)ecc_mode);
    _qrManager.output_mode = QR_OUTPUT_MODE_ATASCII;
    _qrManager.encode();

    _qrManager.data.clear();

    if (!_qrManager.code.size())
    {
        Debug_printf("QR code encoding failed\n");
        transaction_error();
        return;
    }

    Debug_printf("Resulting QR code is: %u modules\n", _qrManager.code.size());
    transaction_complete();
}

void sioFuji::sio_qrcode_length()
{
    Debug_printf("FUJI: QRCODE LENGTH\n");
    uint8_t output_mode = sio_get_aux();
    Debug_printf("Output mode: %i\n", output_mode);

    size_t len = _qrManager.size();

    // A bit gross to have a side effect from length command, but not enough aux bytes
    // to specify version, ecc, *and* output mode for the encode command. Also can't
    // just wait for output command, because output mode determines buffer length,
    if (len && (output_mode != _qrManager.output_mode)) {
        _qrManager.output_mode = (ouput_mode_t)output_mode;
        _qrManager.encode();
        len = _qrManager.code.size();
    }

    uint8_t response[4] = {
        (uint8_t)(len >> 0),
        (uint8_t)(len >> 8),
        (uint8_t)(len >> 16),
        (uint8_t)(len >> 24)
    };

    if (!len)
    {
        Debug_printf("QR code buffer is 0 bytes, sending error.\n");
        bus_to_computer(response, sizeof(response), true);
    }

    Debug_printf("QR code buffer length: %u bytes\n", len);

    bus_to_computer(response, sizeof(response), false);
}

void sioFuji::sio_qrcode_output()
{
    Debug_printf("FUJI: QRCODE OUTPUT\n");

    size_t len = sio_get_aux();

    if (!len)
    {
        Debug_printf("Refusing to send a zero byte buffer. Aborting\n");
        return;
    }
    else if (len > _qrManager.size())
    {
        Debug_printf("Requested %u bytes, but buffer is only %u bytes, aborting.\n", len, _qrManager.code.size());
        return;
    }
    else
    {
        Debug_printf("Requested %u bytes\n", len);
    }

    bus_to_computer(&_qrManager.code[0], len, false);

    _qrManager.code.clear();
    _qrManager.code.shrink_to_fit();
}

void sioFuji::sio_base64_encode_input()
{
    transaction_continue(true);

    uint16_t len = sio_get_aux();

    Debug_printf("FUJI: BASE64 ENCODE INPUT\n");

    if (!len)
    {
        Debug_printf("Invalid length. Aborting");
        transaction_error();
        return;
    }

    std::vector<unsigned char> p(len);
    transaction_get(p.data(), len);
    base64.base64_buffer += std::string((const char *)p.data(), len);
    transaction_complete();
}

void sioFuji::sio_base64_encode_compute()
{
    size_t out_len;

    Debug_printf("FUJI: BASE64 ENCODE COMPUTE\n");

    std::unique_ptr<char[]> p = Base64::encode(base64.base64_buffer.c_str(), base64.base64_buffer.size(), &out_len);
    if (!p)
    {
        Debug_printf("base64_encode compute failed\n");
        transaction_error();
        return;
    }

    base64.base64_buffer.clear();
    base64.base64_buffer = std::string(p.get(), out_len);

    Debug_printf("Resulting BASE64 encoded data is: %u bytes\n", out_len);
    transaction_complete();
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
        transaction_put(response, sizeof(response), true);
    }

    Debug_printf("base64 buffer length: %u bytes\n", l);

    transaction_put(response, sizeof(response), false);
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

    transaction_put(p.data(), len, false);
}

void sioFuji::sio_random_number()
{
    int r = rand();
    transaction_put(&r,sizeof(int),true);
}

void sioFuji::sio_base64_decode_input()
{
    transaction_continue(true);

    uint16_t len = sio_get_aux();

    Debug_printf("FUJI: BASE64 DECODE INPUT\n");

    if (!len)
    {
        Debug_printf("Invalid length. Aborting");
        transaction_error();
        return;
    }

    std::vector<unsigned char> p(len);
    transaction_get(p.data(), len);
    base64.base64_buffer += std::string((const char *)p.data(), len);
    transaction_complete();
}

void sioFuji::sio_base64_decode_compute()
{
    size_t out_len;

    Debug_printf("FUJI: BASE64 DECODE COMPUTE\n");

    std::unique_ptr<unsigned char[]> p = Base64::decode(base64.base64_buffer.c_str(), base64.base64_buffer.size(), &out_len);
    if (!p)
    {
        Debug_printf("base64_encode compute failed\n");
        transaction_error();
        return;
    }

    base64.base64_buffer.clear();
    base64.base64_buffer = std::string((const char *)p.get(), out_len);

    Debug_printf("Resulting BASE64 encoded data is: %u bytes\n", out_len);
    transaction_complete();
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
        transaction_put(response, sizeof(response), true);
        return;
    }

    Debug_printf("base64 buffer length: %u bytes\n", len);

    transaction_put(response, sizeof(response), false);
}

void sioFuji::sio_base64_decode_output()
{
    Debug_printf("FUJI: BASE64 DECODE OUTPUT\n");

    size_t len = sio_get_aux();

    if (!len)
    {
        Debug_printf("Refusing to send a zero byte buffer. Aborting\n");
        transaction_error();
        return;
    }
    else if (len > base64.base64_buffer.length())
    {
        Debug_printf("Requested %u bytes, but buffer is only %u bytes, aborting.\n", len, base64.base64_buffer.length());
        transaction_error();
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
    transaction_put(p.data(), len, false);
}

void sioFuji::sio_hash_input()
{
    transaction_continue(true);

    Debug_printf("FUJI: HASH INPUT\n");
    uint16_t len = sio_get_aux();
    if (!len)
    {
        Debug_printf("Invalid length. Aborting");
        transaction_error();
        return;
    }

    std::vector<unsigned char> p(len);
    transaction_get(p.data(), len);
    hasher.add_data(p);
    transaction_complete();
}

void sioFuji::sio_hash_compute(bool clear_data)
{
    Debug_printf("FUJI: HASH COMPUTE\n");
    algorithm = Hash::to_algorithm(sio_get_aux());
    hasher.compute(algorithm, clear_data);
    transaction_complete();
}

void sioFuji::sio_hash_length()
{
    Debug_printf("FUJI: HASH LENGTH\n");
    uint16_t is_hex = sio_get_aux() == 1;
    uint8_t r = hasher.hash_length(algorithm, is_hex);
    transaction_put(&r, 1, false);
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
    transaction_put(hashed_data.data(), hashed_data.size(), false);
}

void sioFuji::sio_hash_clear()
{
    Debug_printf("FUJI: HASH CLEAR\n");
    hasher.clear();
    transaction_complete();
}

void sioFuji::sio_process(uint32_t commanddata, uint8_t checksum)
{
    cmdFrame.commanddata = commanddata;
    cmdFrame.checksum = checksum;

    Debug_printf("sioFuji::fujicmd_process() called, baud: %d\n", SYSTEM_BUS.getBaudrate());

    switch (cmdFrame.comnd)
    {
    case FUJICMD_HSIO_INDEX:
        sio_high_speed();
        break;
    case FUJICMD_SET_HSIO_INDEX:
        sio_set_hsio_index();
        break;
    case FUJICMD_STATUS:
        sio_status();
        break;
    case FUJICMD_RESET:
        fujicmd_reset();
        break;
    case FUJICMD_SCAN_NETWORKS:
        fujicmd_net_scan_networks();
        break;
    case FUJICMD_GET_SCAN_RESULT:
        fujicmd_net_scan_result(cmdFrame.aux1);
        break;
    case FUJICMD_SET_SSID:
        sio_net_set_ssid();
        break;
    case FUJICMD_GET_SSID:
        fujicmd_net_get_ssid();
        break;
    case FUJICMD_GET_WIFISTATUS:
        fujicmd_net_get_wifi_status();
        break;
    case FUJICMD_MOUNT_HOST:
        fujicmd_mount_host_success(cmdFrame.aux1);
        break;
    case FUJICMD_MOUNT_IMAGE:
        fujicmd_mount_disk_image_success(cmdFrame.aux1, (disk_access_flags_t) cmdFrame.aux2);
        break;
    case FUJICMD_OPEN_DIRECTORY:
        fujicmd_open_directory_success(cmdFrame.aux1);
        break;
    case FUJICMD_READ_DIR_ENTRY:
        fujicmd_read_directory_entry(cmdFrame.aux1, cmdFrame.aux2);
        break;
    case FUJICMD_CLOSE_DIRECTORY:
        fujicmd_close_directory();
        break;
    case FUJICMD_GET_DIRECTORY_POSITION:
        fujicmd_get_directory_position();
        break;
    case FUJICMD_SET_DIRECTORY_POSITION:
        fujicmd_set_directory_position(le16toh(cmdFrame.aux12));
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
    case FUJICMD_GET_WIFI_ENABLED:
        fujicmd_net_get_wifi_enabled();
        break;
    case FUJICMD_SET_BAUDRATE:
        sio_set_baudrate();
        break;
    case FUJICMD_UNMOUNT_IMAGE:
        fujicmd_unmount_disk_image_success(cmdFrame.aux1);
        break;
    case FUJICMD_GET_ADAPTERCONFIG:
        // THIS IS STILL NEEDED FOR BACKWARDS COMPATIBILITY WITH
        // FUJINET-LIB THAT SENDS 0xE8 FOR ADAPTER_CONFIG_EXTENDED
        // WITH 0x01 IN THE AUX1 BYTE
        if (cmdFrame.aux1 == 1)
            fujicmd_get_adapter_config_extended();
        else
            fujicmd_get_adapter_config();
        break;
    case FUJICMD_GET_ADAPTERCONFIG_EXTENDED:
        fujicmd_get_adapter_config_extended();
        break;
    case FUJICMD_NEW_DISK:
        sio_new_disk();
        break;
    case FUJICMD_UNMOUNT_HOST:
        fujicmd_unmount_host_success(cmdFrame.aux1);
        break;
    case FUJICMD_SET_DEVICE_FULLPATH:
        fujicmd_set_device_filename_success(cmdFrame.aux1, cmdFrame.aux2 >> 4,
                                            (disk_access_flags_t) (cmdFrame.aux2 & 0x0F));
        break;
    case FUJICMD_SET_HOST_PREFIX:
        fujicmd_set_host_prefix(cmdFrame.aux1);
        break;
    case FUJICMD_GET_HOST_PREFIX:
        fujicmd_get_host_prefix(cmdFrame.aux1);
        break;
    case FUJICMD_SET_SIO_EXTERNAL_CLOCK:
        fujicmd_set_sio_external_clock(le16toh(cmdFrame.aux12));
        break;
    case FUJICMD_WRITE_APPKEY:
        fujicmd_write_app_key(le16toh(cmdFrame.aux12),
                              get_value_or_default(mode_to_keysize, _current_appkey.mode, 64));
        break;
    case FUJICMD_READ_APPKEY:
        fujicmd_read_app_key();
        break;
    case FUJICMD_OPEN_APPKEY:
        fujicmd_open_app_key();
        break;
    case FUJICMD_CLOSE_APPKEY:
        fujicmd_close_app_key();
        break;
    case FUJICMD_GET_DEVICE_FULLPATH:
        fujicmd_get_device_filename(cmdFrame.aux1);
        break;
    case FUJICMD_CONFIG_BOOT:
        fujicmd_set_boot_config(cmdFrame.aux1);
        break;
    case FUJICMD_COPY_FILE:
        sio_copy_file();
        break;
    case FUJICMD_MOUNT_ALL:
        fujicmd_mount_all_success();
        break;
    case FUJICMD_SET_BOOT_MODE:
        fujicmd_set_boot_mode(cmdFrame.aux1, MEDIATYPE_UNKNOWN, &bootdisk);
        break;
    case FUJICMD_ENABLE_UDPSTREAM:
        fujicmd_enable_udpstream(le16toh(cmdFrame.aux12));
        break;
    case FUJICMD_QRCODE_INPUT:
        sio_qrcode_input();
        break;
    case FUJICMD_QRCODE_ENCODE:
        sio_qrcode_encode();
        break;
    case FUJICMD_QRCODE_LENGTH:
        sio_qrcode_length();
        break;
    case FUJICMD_QRCODE_OUTPUT:
        sio_qrcode_output();
        break;
    case FUJICMD_BASE64_ENCODE_INPUT:
        sio_base64_encode_input();
        break;
    case FUJICMD_BASE64_ENCODE_COMPUTE:
        sio_base64_encode_compute();
        break;
    case FUJICMD_BASE64_ENCODE_LENGTH:
        sio_base64_encode_length();
        break;
    case FUJICMD_BASE64_ENCODE_OUTPUT:
        sio_base64_encode_output();
        break;
    case FUJICMD_BASE64_DECODE_INPUT:
        sio_base64_decode_input();
        break;
    case FUJICMD_BASE64_DECODE_COMPUTE:
        sio_base64_decode_compute();
        break;
    case FUJICMD_BASE64_DECODE_LENGTH:
        sio_base64_decode_length();
        break;
    case FUJICMD_BASE64_DECODE_OUTPUT:
        sio_base64_decode_output();
        break;
    case FUJICMD_HASH_INPUT:
        sio_hash_input();
        break;
    case FUJICMD_HASH_COMPUTE:
        sio_hash_compute(true);
        break;
    case FUJICMD_HASH_COMPUTE_NO_CLEAR:
        sio_hash_compute(false);
        break;
    case FUJICMD_HASH_LENGTH:
        sio_hash_length();
        break;
    case FUJICMD_HASH_OUTPUT:
        sio_hash_output();
        break;
    case FUJICMD_HASH_CLEAR:
        sio_hash_clear();
        break;
    case FUJICMD_RANDOM_NUMBER:
        sio_random_number();
        break;
    case FUJICMD_GENERATE_GUID:
        fujicmd_generate_guid();
        break;
    default:
        transaction_error();
    }
}

bool sioFuji::fujicore_mount_disk_image_success(uint8_t deviceSlot,
                                                disk_access_flags_t access_mode)
{
    if (!fujiDevice::fujicore_mount_disk_image_success(deviceSlot, access_mode))
        return false;
    status_wait_count = 0;
    return true;
}

// Atari expects this field as a 32-bit little-endian value.
// fujiDevice::fujicmd_net_scan_networks only writes a single byte, so
// we override it here to pad/encode the same computed value as LE32.
void sioFuji::fujicmd_net_scan_networks()
{
    transaction_continue(false);
    Debug_println("Fuji cmd: SCAN NETWORKS");

    char ret[4] = {0};
    fujicore_net_scan_networks();
    ret[0] = _countScannedSSIDs;
    transaction_put((uint8_t *)ret, 4, false);
}

std::optional<std::vector<uint8_t>> sioFuji::fujicore_read_app_key()
{
    auto result = fujiDevice::fujicore_read_app_key();

    if (result)
    {
        uint16_t len = htole16(result->size());
        result->resize(MAX_APPKEY_LEN, 0);
        const uint8_t *len_bytes = reinterpret_cast<const uint8_t*>(&len);
        result->insert(result->begin(), len_bytes, len_bytes + sizeof(len));
    }

    return result;
}

#endif /* BUILD_ATARI */
