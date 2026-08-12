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
// dbc_inbound_handler() demuxes on this same id). Sent as one PAYLOAD byte,
// not a param: FujiBusPacket::processArg(uint16_t) encodes bare integer
// arguments as wire params, but the RP2040's minimal fujibus.c client
// parses the descriptor chain only far enough to skip past it to find the
// payload -- it never surfaces decoded param values. The payload path is
// the one it actually exposes to callers (fb_reply_t.data/data_len), so
// that's what carries the stream id here.
// Always sends NETCMD_CLOSE, even on failure, so the RP2040's per-stream
// state doesn't wedge for the next OPEN.
static bool push_stream(fnFile *f, uint16_t stream_id)
{
    uint8_t buf[DISK_SECTORBUF_SIZE];

    uint8_t open_payload = (uint8_t)stream_id;
    auto reply = SYSTEM_BUS.sendCommand(FUJI_DEVICEID_DBC, NETCMD_OPEN,
                                        std::string(1, (char)open_payload));
    if (!reply || reply->command() != FUJICMD_ACK)
    {
        Debug_printv("MediaTypeROM: failed to open DBC stream %u\n", stream_id);
        return false;
    }

    bool ok = true;
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
    }

    reply = SYSTEM_BUS.sendCommand(FUJI_DEVICEID_DBC, NETCMD_CLOSE);
    if (!reply || reply->command() != FUJICMD_ACK)
    {
        Debug_printv("MediaTypeROM: stream %u close failed/rejected\n", stream_id);
        ok = false;
    }
    return ok;
}

mediatype_t MediaTypeROM::mount(fnFile *f, uint32_t disksize, fujiHost *host, const char *filename)
{
    Debug_printv("MediaTypeROM MOUNT %s (%lu bytes)\n",
                 filename ? filename : "?", (unsigned long)disksize);

    _disk_fileh = f;
    _disk_image_size = disksize;
    _disktype = MEDIATYPE_ROM;

    // Push a same-named .cfg sibling first, if one exists, so the RP2040
    // knows the file's memory mapping before the ROM push's CLOSE frame
    // triggers the actual boot. Best-effort: a missing .cfg (the normal
    // case for a self-describing .rom, or a bare .bin the RP2040 will
    // fall back to guessing a mapping for) is not an error.
    if (host != nullptr && filename != nullptr)
    {
        char cfgpath[MAX_FILENAME_LEN];
        strlcpy(cfgpath, filename, sizeof(cfgpath));
        char *dot = strrchr(cfgpath, '.');
        if (dot != nullptr)
            strlcpy(dot, ".cfg", sizeof(cfgpath) - (dot - cfgpath));
        else
            strlcat(cfgpath, ".cfg", sizeof(cfgpath));

        if (host->file_exists(cfgpath))
        {
            char resolved[MAX_FILENAME_LEN];
            strlcpy(resolved, cfgpath, sizeof(resolved));
            fnFile *cfgf = host->fnfile_open(cfgpath, resolved, sizeof(resolved), "rb");
            if (cfgf != nullptr)
            {
                push_stream(cfgf, ROM_PUSH_STREAM_CFG);
                fnio::fclose(cfgf);
            }
            else
            {
                Debug_printv("MediaTypeROM: .cfg sibling exists but failed to open: %s\n", cfgpath);
            }
        }
    }

    fnio::fseek(f, 0, SEEK_SET);
    if (!push_stream(f, ROM_PUSH_STREAM_ROM))
    {
        Debug_printv("MediaTypeROM: ROM push failed\n");
        return MEDIATYPE_UNKNOWN;
    }

    return _disktype;
}

#endif // BUILD_RS232
