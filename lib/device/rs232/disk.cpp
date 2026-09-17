#ifdef BUILD_RS232

#include "disk.h"
#include "fujiCommandID.h"

#include <cstring>
#include <string>

#include "../../include/debug.h"

#include "compat_string.h"
#include "fujiHost.h"
#include "fujiDevice.h"
#include "utils.h"

#define ROM_PUSH_STREAM_CFG 1
#define ROM_PUSH_STREAM_ROM 0

// push_stream: reads `f` from its current position in DISK_SECTORBUF_SIZE
// chunks and relays each one to the RP2040 as CMD::NET_WRITE frames on DBC
// stream `stream_id` (0 = ROM, 1 = a .cfg sibling -- the RP2040's
// dbc_inbound_handler() demuxes on this same id). Sent as PAYLOAD bytes,
// not params: FujiBusPacket::processArg(uint16_t) encodes bare integer
// arguments as wire params, but the RP2040's minimal fujibus.c client
// parses the descriptor chain only far enough to skip past it to find the
// payload -- it never surfaces decoded param values. The payload path is
// the one it actually exposes to callers (fb_reply_t.data/data_len), so
// that's what carries the OPEN header here.
//
// OPEN payload is the stream id followed by the stream's total size as 4
// little-endian bytes. The RP2040 uses the size to refuse a ROM too large
// for its cart.ROM[] before we drag the whole thing over TNFS, and to draw
// an exact progress bar; an older RP2040 build reads data[0] and ignores
// the rest, so this stays compatible in both directions.
//
// Always sends CMD::NET_CLOSE so the RP2040's stream state doesn't wedge; a
// failed transfer's CLOSE carries a 0x01 abort payload so partial data
// isn't booted.
static bool push_stream(fnFile *f, uint16_t stream_id, uint32_t expected_size)
{
    uint8_t buf[DISK_SECTORBUF_SIZE];

    struct { uint8_t id; u32le_t size; } open_hdr;
    static_assert(sizeof(open_hdr) == 5, "OPEN header must not be padded");
    open_hdr.id = (uint8_t)stream_id;
    open_hdr.size = expected_size;

    auto reply = SYSTEM_BUS.sendCommand(FUJI_DEVICEID::DBC, CMD::NET_OPEN,
                                        std::string((const char *)&open_hdr, sizeof(open_hdr)));
    if (!reply || reply->command() != CMD::FUJI_ACK)
    {
        Debug_printv("ROM push: failed to open DBC stream %u (%lu bytes)\n",
                     stream_id, (unsigned long)expected_size);
        return false;
    }

    bool ok = true;
    uint32_t sent = 0;
    size_t got;
    while ((got = fnio::fread(buf, 1, sizeof(buf), f)) > 0)
    {
        reply = SYSTEM_BUS.sendCommand(FUJI_DEVICEID::DBC, CMD::NET_WRITE,
                                       std::string((char *)buf, got));
        if (!reply || reply->command() != CMD::FUJI_ACK)
        {
            Debug_printv("ROM push: failed to send stream %u block\n", stream_id);
            ok = false;
            break;
        }
        sent += got;
    }

    // fread() can't distinguish EOF from error -- the byte count is the signal
    if (ok && sent != expected_size)
    {
        Debug_printv("ROM push: stream %u short transfer: %lu of %lu bytes\n",
                     stream_id, (unsigned long)sent, (unsigned long)expected_size);
        ok = false;
    }

    if (ok)
        reply = SYSTEM_BUS.sendCommand(FUJI_DEVICEID::DBC, CMD::NET_CLOSE);
    else
        reply = SYSTEM_BUS.sendCommand(FUJI_DEVICEID::DBC, CMD::NET_CLOSE,
                                       std::string(1, '\x01'));
    if (!reply || reply->command() != CMD::FUJI_ACK)
    {
        Debug_printv("ROM push: stream %u close failed/rejected\n", stream_id);
        ok = false;
    }
    return ok;
}

// rs232Disk::mount()'s `filename` arrives already host-prefixed (fnfile_open rewrites
// disk.filename in place); fujiHost APIs prefix again, so strip it first.
static const char *strip_host_prefix(fujiHost *host, const char *filename)
{
    const char *pfx = host->get_prefix();
    if (pfx == nullptr || pfx[0] == '\0')
        return filename;

    size_t plen = strlen(pfx);
    if (strncmp(filename, pfx, plen) != 0)
        return filename; // not prefixed after all

    const char *p = filename + plen;
    // skip the separator util_concat_paths() inserted
    if (pfx[plen - 1] != '/' && pfx[plen - 1] != '\\' && (*p == '/' || *p == '\\'))
        p++;
    return p;
}

// A ROM image isn't served sector by sector the way a disk is: the whole file
// goes to the RP2040 at mount time, which presents it to the machine as
// cartridge ROM. Nothing reads it back through the media object afterwards.
static bool push_rom_streams(fnFile *f, uint32_t disksize, fujiHost *host, const char *filename)
{
    // Push the .cfg sibling first so the mapping is known before the ROM's
    // CLOSE boots. Missing sibling: fine. Existing sibling that fails to
    // open/push: fail the mount -- booting without it produces hangs.
    if (host != nullptr && filename != nullptr)
    {
        char cfgpath[MAX_FILENAME_LEN];
        strlcpy(cfgpath, strip_host_prefix(host, filename), sizeof(cfgpath));

        // replace the basename's extension only
        char *base = cfgpath;
        for (char *p = cfgpath; *p != '\0'; p++)
            if (*p == '/' || *p == '\\')
                base = p + 1;
        char *dot = strrchr(base, '.');
        if (dot != nullptr)
            strlcpy(dot, ".cfg", sizeof(cfgpath) - (dot - cfgpath));
        else
            strlcat(cfgpath, ".cfg", sizeof(cfgpath));

        bool cfg_found = host->file_exists(cfgpath);
        if (!cfg_found)
        {
            // case-sensitive hosts may carry the sibling as .CFG
            size_t len = strlen(cfgpath);
            memcpy(cfgpath + len - 4, ".CFG", 4);
            cfg_found = host->file_exists(cfgpath);
        }

        if (!cfg_found)
        {
            // Not an error -- a .bin with no memory map boots against the
            // emulator's size-guess table -- but for the titles that need one
            // the result is a wrong map, i.e. a game that boots to garbage
            // with nothing anywhere saying why. Say it here.
            Debug_printv("ROM push: no .cfg sibling for %s (tried \"%s\" in both "
                         "casings) -- booting with a default memory map\n",
                         filename, cfgpath);
        }
        else
        {
            char resolved[MAX_FILENAME_LEN];
            strlcpy(resolved, cfgpath, sizeof(resolved));
            fnFile *cfgf = host->fnfile_open(cfgpath, resolved, sizeof(resolved), "rb");
            if (cfgf == nullptr)
            {
                Debug_printv("ROM push: .cfg sibling exists but failed to open: %s\n", cfgpath);
                return false;
            }
            long cfgsize = host->file_size(cfgf);
            bool cfg_ok = cfgsize >= 0 &&
                          push_stream(cfgf, ROM_PUSH_STREAM_CFG, (uint32_t)cfgsize);
            fnio::fclose(cfgf);
            if (!cfg_ok)
            {
                Debug_printv("ROM push: .cfg push failed: %s\n", cfgpath);
                return false;
            }
        }
    }

    fnio::fseek(f, 0, SEEK_SET);
    if (!push_stream(f, ROM_PUSH_STREAM_ROM, disksize))
    {
        Debug_printv("ROM push: ROM push failed\n");
        return false;
    }

    return true;
}

rs232Disk::rs232Disk()
{
    device_active = false;
    _mount_time = 0;
}

// Read disk data and send to computer
void rs232Disk::rs232_read(uint32_t sector)
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);

    Debug_printf("disk READ %lu\n", sector);

    if (_disk == nullptr)
    {
        SYSTEM_BUS.transaction_error();
        return;
    }

    uint32_t readcount;

    bool err = _disk->read(sector, &readcount);

    // Send result to Atari
    SYSTEM_BUS.transaction_send(_disk->_disk_sectorbuff, readcount, err);
}

// Write disk data from computer
void rs232Disk::rs232_write(uint32_t sector, bool verify)
{
    //Debug_print("disk WRITE\n");

    SYSTEM_BUS.transaction_accept(TRANS_STATE::WILL_GET);

    if (_disk != nullptr)
    {
        uint16_t sectorSize = _disk->sector_size(sector);

        memset(_disk->_disk_sectorbuff, 0, DISK_SECTORBUF_SIZE);

        if (SYSTEM_BUS.transaction_get(_disk->_disk_sectorbuff, sectorSize))
        {
            if (_disk->write(sector, verify) == false)
            {
                SYSTEM_BUS.transaction_success();
                return;
            }
        }
    }

    SYSTEM_BUS.transaction_error();
}

// Status
void rs232Disk::rs232_status(FujiStatusReq reqType)
{
    Debug_print("disk STATUS\n");

    /* STATUS BYTES
        #0 - Drive status
            Bit 7 = 1: 26 sectors per track (1050/XF551 drive)
            Bit 6 = 1: Double sided disk (XF551 drive)
            Bit 5 = 1: Double density (XF551 drive)
            Bit 4 = 1: Motor running (always 0 on XF551)

            Bit 3 = 1: Failed due to write protected disk
            Bit 2 = 1: Unsuccessful PUT operation
            Bit 1 = 1: Receive error on last data frame (XF551)
            Bit 0 = 1: Receive error on last command frame (XF551)

        #1 - Floppy drive controller status (inverted from FDC)
            Bit 7 = 0: Drive not ready (1050 drive)
            Bit 6 = 0: Write protect error
            Bit 5 = 0: Deleted sector (sector marked as deleted in sector header)
            Bit 4 = 0: Record not found (missing sector)

            Bit 3 = 0: CRC error
            Bit 2 = 0: Lost data
            Bit 1 = 0: Data request pending
            Bit 0 = 0: Busy

        #2 - Format timeout
              810 drive: $E0 = 224 vertical blanks (4 mins NTSC)
            XF551 drive: $FE = 254 veritcal blanks (4.5 mins NTSC)

        #3 - Unused ($00)
    */
    // TODO: Why $DF for second byte?
    // TODO: Set bit 4 of drive status and bit 6 of FDC status on read-only disk
#define DRIVE_DEFAULT_TIMEOUT_810 0xE0
#define DRIVE_DEFAULT_TIMEOUT_XF551 0xFE

    uint8_t _status[4];
    _status[0] = 0x00;
    _status[1] = ~DISK_CTRL_STATUS_CLEAR; // Negation of default clear status
    _status[2] = DRIVE_DEFAULT_TIMEOUT_810;
    _status[3] = 0x00;

    if (_disk != nullptr)
        _disk->status(_status);

    Debug_printf("response: 0x%02x, 0x%02x, 0x%02x\n", _status[0], _status[1], _status[2]);

    SYSTEM_BUS.transaction_send(_status, sizeof(_status), false);
}

// Disk format
void rs232Disk::rs232_format()
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    Debug_print("disk FORMAT\n");

    if (_disk == nullptr)
    {
        SYSTEM_BUS.transaction_error();
        return;
    }

    uint32_t responsesize;
    bool err = _disk->format(&responsesize);

    // Send to computer
    SYSTEM_BUS.transaction_send(_disk->_disk_sectorbuff, responsesize, err);
}

// Read percom block
void rs232Disk::rs232_read_percom_block()
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    Debug_print("disk READ PERCOM BLOCK\n");

    if (_disk == nullptr)
    {
        SYSTEM_BUS.transaction_error();
        return;
    }

#ifdef VERBOSE_DISK
    _disk->dump_percom_block();
#endif
    SYSTEM_BUS.transaction_send((uint8_t *)&_disk->_percomBlock, sizeof(_disk->_percomBlock), false);
}

// Write percom block
void rs232Disk::rs232_write_percom_block()
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::WILL_GET);
    Debug_print("disk WRITE PERCOM BLOCK\n");

    if (_disk == nullptr)
    {
        SYSTEM_BUS.transaction_error();
        return;
    }

    SYSTEM_BUS.transaction_get((uint8_t *)&_disk->_percomBlock, sizeof(_disk->_percomBlock));
#ifdef VERBOSE_DISK
    _disk->dump_percom_block();
#endif
    SYSTEM_BUS.transaction_success();
}

/* Mount Disk
   We determine the type of image based on the filename extenrs232n.
   If the disk_type value passed is not MEDIATYPE_UNKNOWN then that's used instead.
   If filename has no extenrs232n or is NULL and disk_type is MEDIATYPE_UNKOWN,
   then we assume it's MEDIATYPE_ATR.
   Return value is MEDIATYPE_UNKNOWN in case of failure.
*/
mediatype_t rs232Disk::mount(fnFile *f, const char *filename, uint32_t disksize,
                             disk_access_flags_t access_mode, mediatype_t disk_type,
                             fujiHost *host)
{
    // TAPE or CASSETTE: use this function to send file info to cassette device
    //  MediaType::discover_mediatype(filename) can detect CAS and WAV files
    Debug_print("disk MOUNT\n");

    // Destroy any existing MediaType
    if (_disk != nullptr)
    {
        delete _disk;
        _disk = nullptr;
    }

    // Determine MediaType based on filename extenrs232n
    if (disk_type == MEDIATYPE_UNKNOWN && filename != nullptr)
        disk_type = MediaType::discover_mediatype(filename);

    // Now mount based on MediaType
    switch (disk_type)
    {
    case MEDIATYPE_ROM:
        device_active = true;
        _mount_time = time(NULL);
        _disk = new MediaTypeROM();
        _disk->mount(f, disksize);
        return push_rom_streams(f, disksize, host, filename) ? MEDIATYPE_ROM : MEDIATYPE_UNKNOWN;
    case MEDIATYPE_IMG:
    case MEDIATYPE_UNKNOWN:
    default:
        device_active = true;
        _mount_time = time(NULL);
        _disk = new MediaTypeImg();
        return _disk->mount(f, disksize);
    }
}

mediatype_t rs232Disk::mount_disk_media(fnFile *f, const char *filename, uint32_t disksize,
                                         mediatype_t disk_type)
{
    return mount(f, filename, disksize, DISK_ACCESS_MODE_READ, disk_type);
}

// Destructor
rs232Disk::~rs232Disk()
{
    if (_disk != nullptr)
    {
        delete _disk;
        _disk = nullptr;
    }
}

// Unmount disk file
void rs232Disk::unmount()
{
    Debug_print("disk UNMOUNT\n");

    if (_disk != nullptr)
    {
        _disk->unmount();
        device_active = false;
        _mount_time = 0;
    }
}

// Create blank disk
success_is_true rs232Disk::write_blank(fnFile *f, uint16_t sectorSize, uint16_t numSectors)
{
    Debug_print("disk CREATE NEW IMAGE\n");

    return MediaTypeImg::create(f, sectorSize, numSectors);
}

// Process command
void rs232Disk::rs232_process(const FujiBusPacket &packet)
{
    Debug_print("disk rs232_process()\n");

    switch (packet.command())
    {
    case CMD::DISK_READ:
        rs232_read(packet.param(0));
        return;
    case CMD::DISK_PUT:
        rs232_write(packet.param(0), false);
        return;
    case CMD::DISK_STATUS:
    case CMD::DISK_WRITE:
        rs232_write(packet.param(0), true);
        return;
    case CMD::DISK_FORMAT:
    case CMD::DISK_FORMAT_MEDIUM:
        rs232_format();
        return;
    case CMD::DISK_PERCOM_READ:
        rs232_read_percom_block();
        return;
    case CMD::DISK_PERCOM_WRITE:
        rs232_write_percom_block();
        return;
    default:
        break;
    }

    SYSTEM_BUS.transaction_error();
}

#endif /* BUILD_RS232 */
