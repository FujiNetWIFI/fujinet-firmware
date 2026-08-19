#ifdef BUILD_RS232 // temporary

#include "diskTypeROM.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "../../include/debug.h"
#include "../../include/fujiCommandID.h"
#include "../../fuji/fujiDisk.h"
#include "../../fuji/fujiHost.h"

#include "bus.h"
#include "compat_string.h"
#include "fujiCommandID.h"

#define ROM_PUSH_STREAM_CFG 1
#define ROM_PUSH_STREAM_ROM 0

error_is_true MediaTypeROM::read(uint32_t sectornum, uint32_t *readcount)
{
    Debug_print("ROM READ not supported\r\n");
    RETURN_ERROR_AS_TRUE();
}

error_is_true MediaTypeROM::write(uint32_t sectornum, bool verify)
{
    Debug_print("ROM WRITE not supported\r\n");
    RETURN_ERROR_AS_TRUE();
}

error_is_true MediaTypeROM::format(uint32_t *responsesize)
{
    RETURN_ERROR_AS_TRUE();
}

void MediaTypeROM::status(uint8_t statusbuff[4])
{
    memset(statusbuff, 0, 4);
}

// push_stream: reads `f` from its current position in DISK_SECTORBUF_SIZE
// chunks and relays each one to the RP2040 as NETCMD_WRITE frames on DBC
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
// Always sends NETCMD_CLOSE so the RP2040's stream state doesn't wedge; a
// failed transfer's CLOSE carries a 0x01 abort payload so partial data
// isn't booted.
static bool push_stream(fnFile *f, uint16_t stream_id, uint32_t expected_size)
{
    uint8_t buf[DISK_SECTORBUF_SIZE];

    char open_payload[5] = {
        (char)(uint8_t)stream_id,
        (char)(uint8_t)(expected_size & 0xFF),
        (char)(uint8_t)((expected_size >> 8) & 0xFF),
        (char)(uint8_t)((expected_size >> 16) & 0xFF),
        (char)(uint8_t)((expected_size >> 24) & 0xFF),
    };
    auto reply = SYSTEM_BUS.sendCommand(FUJI_DEVICEID_DBC, NETCMD_OPEN,
                                        std::string(open_payload, sizeof(open_payload)));
    if (!reply || reply->command() != FUJICMD_ACK)
    {
        Debug_printv("MediaTypeROM: failed to open DBC stream %u (%lu bytes)\n",
                     stream_id, (unsigned long)expected_size);
        return false;
    }

    bool ok = true;
    uint32_t sent = 0;
    size_t got;
    while ((got = fnio::fread(buf, 1, sizeof(buf), f)) > 0)
    {
        reply = SYSTEM_BUS.sendCommand(FUJI_DEVICEID_DBC, NETCMD_WRITE,
                                       std::string((char *)buf, got));
        if (!reply || reply->command() != FUJICMD_ACK)
        {
            Debug_printv("MediaTypeROM: failed to send stream %u block\n", stream_id);
            ok = false;
            break;
        }
        sent += got;
    }

    // fread() can't distinguish EOF from error -- the byte count is the signal
    if (ok && sent != expected_size)
    {
        Debug_printv("MediaTypeROM: stream %u short transfer: %lu of %lu bytes\n",
                     stream_id, (unsigned long)sent, (unsigned long)expected_size);
        ok = false;
    }

    if (ok)
        reply = SYSTEM_BUS.sendCommand(FUJI_DEVICEID_DBC, NETCMD_CLOSE);
    else
        reply = SYSTEM_BUS.sendCommand(FUJI_DEVICEID_DBC, NETCMD_CLOSE,
                                       std::string(1, '\x01'));
    if (!reply || reply->command() != FUJICMD_ACK)
    {
        Debug_printv("MediaTypeROM: stream %u close failed/rejected\n", stream_id);
        ok = false;
    }
    return ok;
}

// mount()'s `filename` arrives already host-prefixed (fnfile_open rewrites
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

mediatype_t MediaTypeROM::mount(fnFile *f, uint32_t disksize, fujiHost *host, const char *filename)
{
    Debug_printv("MediaTypeROM MOUNT %s (%lu bytes)\n",
                 filename ? filename : "?", (unsigned long)disksize);

    _disk_fileh = f;
    _disk_image_size = disksize;
    _disktype = MEDIATYPE_ROM;

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

        if (cfg_found)
        {
            char resolved[MAX_FILENAME_LEN];
            strlcpy(resolved, cfgpath, sizeof(resolved));
            fnFile *cfgf = host->fnfile_open(cfgpath, resolved, sizeof(resolved), "rb");
            if (cfgf == nullptr)
            {
                Debug_printv("MediaTypeROM: .cfg sibling exists but failed to open: %s\n", cfgpath);
                return MEDIATYPE_UNKNOWN;
            }
            long cfgsize = host->file_size(cfgf);
            bool cfg_ok = cfgsize >= 0 &&
                          push_stream(cfgf, ROM_PUSH_STREAM_CFG, (uint32_t)cfgsize);
            fnio::fclose(cfgf);
            if (!cfg_ok)
            {
                Debug_printv("MediaTypeROM: .cfg push failed: %s\n", cfgpath);
                return MEDIATYPE_UNKNOWN;
            }
        }
    }

    fnio::fseek(f, 0, SEEK_SET);
    if (!push_stream(f, ROM_PUSH_STREAM_ROM, _disk_image_size))
    {
        Debug_printv("MediaTypeROM: ROM push failed\n");
        return MEDIATYPE_UNKNOWN;
    }

    return _disktype;
}

#endif // BUILD_RS232
