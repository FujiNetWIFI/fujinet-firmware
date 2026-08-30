#ifdef BUILD_RS232

#include "diskTypeDSK.h"

#include <stdlib.h>
#include <string.h>

#include "../../include/debug.h"

// --- The format table (firmware-baked geometry authority) -------------------
//
// Each format is a list of regions so one fmttype can describe a disk whose
// tracks aren't uniform. Four of the five initial formats are single-sided and
// uniform (one region). The fifth, Tarbell DD, is the mixed-count case: track 0
// has 26 x 128-byte sectors, tracks 1-76 have 51 x 128-byte sectors -- the
// sector SIZE is constant, the sector COUNT varies -- so it is a two-region
// entry that exercises the general offset walk in locate().

// --- uniform single-sided formats (one region each) ---
static const DSKRegion R_IBM_SD[]  = {{0,   76, 0xFF, 26,  128, 1}};
static const DSKRegion R_IBM_DD[]  = {{0,   76, 0xFF, 26,  256, 1}};
static const DSKRegion R_ALTAIR[]  = {{0,   76, 0xFF, 32,  137, 1}};
static const DSKRegion R_FDCPLUS[] = {{0, 2047, 0xFF, 32,  137, 1}};

// --- Tarbell Double Density: 26 sectors on track 0, 51 on tracks 1-76;
//     128-byte sectors throughout (varies sector COUNT, not size) ---
static const DSKRegion R_TARBELL_DD[] = {
    {0,  0, 0xFF, 26, 128, 1}, // track 0     : 26 x 128-byte sectors
    {1, 76, 0xFF, 51, 128, 1}, // tracks 1-76 : 51 x 128-byte sectors
};

// All five baked formats are single-sided, so head_major is moot (both side
// layouts reduce to the same offset) and is set false. It matters only for a
// double-sided custom format (set_geometry()).
const DSKFormat dsk_formats[FMT_COUNT] = {
    {"IBM 8\" SD", 77,   1, 1, R_IBM_SD,     false}, // = 256,256 bytes
    {"IBM 8\" DD", 77,   1, 1, R_IBM_DD,     false}, // = 512,512 bytes
    {"Altair 8\"", 77,   1, 1, R_ALTAIR,     false}, // = 337,568 bytes
    {"FDC+",       2048, 1, 1, R_FDCPLUS,    false}, // = 8,978,432 bytes
    {"Tarbell DD", 77,   1, 2, R_TARBELL_DD, false}, // = 499,456 bytes
};

// Return the region of format f whose track range contains t and whose
// head_mask includes h; nullptr if none (malformed table or bad address).
static const DSKRegion *region_for(const DSKFormat &f, uint16_t t, uint8_t h)
{
    for (uint8_t i = 0; i < f.num_regions; i++)
    {
        const DSKRegion &r = f.regions[i];
        if (t >= r.first_track && t <= r.last_track && (r.head_mask & (1 << h)))
            return &r;
    }
    return nullptr;
}

// Resolve (fmt, head, trk, sec) -> file offset + transfer size. In track_mode
// the result spans the whole track (sector is ignored); otherwise it is a
// single sector. Pure arithmetic; no file I/O. Returns false if the address is
// out of range for the format.
bool MediaTypeDSK::locate(uint8_t fmt, uint8_t head, uint16_t trk, uint16_t sec,
                             bool track_mode, uint32_t *offset, uint16_t *size)
{
    // Select the baked table entry, or the runtime custom slot (FMT_CUSTOM).
    const DSKFormat *fp;
    if (fmt == FMT_CUSTOM)
    {
        if (!_custom_valid)
            return false;
        fp = &_custom_format;
    }
    else if (fmt < FMT_COUNT)
        fp = &dsk_formats[fmt];
    else
        return false;

    const DSKFormat &f = *fp;
    if (trk >= f.num_tracks || head >= f.num_sides)
        return false;

    // Fast path: a uniform (single-region) format is a straight multiply.
    // head_major selects the side layout: all of head 0's tracks then head 1's,
    // vs. the two heads interleaved per cylinder. Single-sided formats reduce to
    // the same value.
    if (f.num_regions == 1)
    {
        const DSKRegion &r = f.regions[0];
        uint32_t track_index = f.head_major
            ? ((uint32_t)head * f.num_tracks + trk) // sequential sides
            : ((uint32_t)trk * f.num_sides + head); // interleaved
        uint32_t track_start = track_index * r.sectors;

        // Track mode: the whole track, starting at its first sector.
        if (track_mode)
        {
            *offset = track_start * r.sector_size;
            *size   = (uint16_t)(r.sectors * r.sector_size);
            return true;
        }

        if (sec < r.first_sector || sec >= r.first_sector + r.sectors)
            return false;
        *offset = (track_start + (sec - r.first_sector)) * r.sector_size;
        *size   = r.sector_size;
        return true;
    }

    // General path: sum each preceding (track, side) slot's OWN region bytes, in
    // the order the layout stores them, then add the sector offset within the
    // target slot. Because every slot contributes its own region's size, the two
    // sides of a cylinder may carry different geometry and the offsets still come
    // out right. head_major just changes the iteration order.
    uint32_t off = 0;
    if (f.head_major) // all of side 0, then side 1, ...
    {
        for (uint8_t h = 0; h < head; h++)
            for (uint16_t t = 0; t < f.num_tracks; t++)
            {
                const DSKRegion *r = region_for(f, t, h);
                if (r == nullptr)
                    return false;
                off += (uint32_t)r->sectors * r->sector_size;
            }
        for (uint16_t t = 0; t < trk; t++)
        {
            const DSKRegion *r = region_for(f, t, head);
            if (r == nullptr)
                return false;
            off += (uint32_t)r->sectors * r->sector_size;
        }
    }
    else // cylinder-major: both sides per cylinder
    {
        for (uint16_t t = 0; t < trk; t++)
            for (uint8_t h = 0; h < f.num_sides; h++)
            {
                const DSKRegion *r = region_for(f, t, h);
                if (r == nullptr)
                    return false;
                off += (uint32_t)r->sectors * r->sector_size;
            }
        for (uint8_t h = 0; h < head; h++)
        {
            const DSKRegion *r = region_for(f, trk, h);
            if (r == nullptr)
                return false;
            off += (uint32_t)r->sectors * r->sector_size;
        }
    }

    const DSKRegion *r = region_for(f, trk, head); // the target slot
    if (r == nullptr)
        return false;
    if (track_mode)
    {
        *offset = off;
        *size   = (uint16_t)(r->sectors * r->sector_size);
        return true;
    }
    if (sec < r->first_sector || sec >= r->first_sector + r->sectors)
        return false;
    *offset = off + (uint32_t)(sec - r->first_sector) * r->sector_size;
    *size   = r->sector_size;
    return true;
}

// Addressing hook: read head/track/sector/fmttype, resolve the byte offset and
// sector size, and stash them for read()/write()/sector_size() to consume.
uint32_t MediaTypeDSK::decode_sector(const uint32_t *params, unsigned count)
{
    // Without fmttype we cannot resolve geometry. A floppy host always sends
    // all four params; a short access is a malformed/legacy request and is
    // refused rather than guessed.
    if (count < 4)
    {
        _cur_head = 0;
        _cur_trk = 0;
        _cur_sec = 0;
        _cur_index = 0;
        _cur_mode        = 2; // unknown -- short/malformed access
        _cur_valid       = false;
        _cur_xfer_size = 0;
        return 0;
    }

    uint8_t  head    = (uint8_t)params[0];
    uint16_t trk     = (uint16_t)params[1];
    uint16_t sec     = (uint16_t)params[2];
    uint8_t  fmttype = (uint8_t)params[3];

    // fmttype carries the format index (bits 0-3, 0x0F = custom) and the access
    // mode (bit 6): single sector, or the whole track. Bits 4,5,7 are reserved.
    uint8_t index      = fmttype & FMT_INDEX_MASK;
    bool    track_mode = (fmttype & FMT_MODE_TRACK) != 0;

    // Stash the raw address so read()/write() can log the access compactly.
    _cur_head  = head;
    _cur_trk   = trk;
    _cur_sec   = sec;
    _cur_index = index;

    // Any reserved bit set is refused, never guessed -- the host must send a
    // clean fmttype.
    if (fmttype & FMT_RESERVED_MASK)
    {
        _cur_mode      = 2; // bad
        _cur_valid     = false;
        _cur_xfer_size = 0;
        return trk;
    }

    _cur_mode  = track_mode ? 1 : 0;
    _cur_valid = locate(index, head, trk, sec, track_mode, &_cur_offset, &_cur_xfer_size);
    if (!_cur_valid)
        _cur_xfer_size = 0;

    // Returned only for the device's logging/bounds; read()/write() use the stash.
    return trk;
}

// One compact line describing the current access, e.g.
//   DSK R fmt=3(FDC+) SEC 0/5/1 137 @55A0     (head/track/sector, size, hex off)
//   DSK W fmt=0(IBM 8" SD) TRK 0/5/1 3328 @4100
//   DSK R fmt=3(FDC+) SEC 0/2048/1 BAD        (address failed the geometry check)
void MediaTypeDSK::log_access(char rw)
{
    const char *mode = (_cur_mode == 0) ? "SEC" : (_cur_mode == 1) ? "TRK" : "BADMODE";
    const char *name = (_cur_index == FMT_CUSTOM) ? "custom"
                       : (_cur_index < FMT_COUNT)  ? dsk_formats[_cur_index].name
                                                   : "?";

    if (_cur_valid)
        Debug_printf("DSK %c fmt=%u(%s) %s %u/%u/%u %u @%lX\r\n", rw, _cur_index, name,
                     mode, _cur_head, _cur_trk, _cur_sec, _cur_xfer_size,
                     (unsigned long)_cur_offset);
    else
        Debug_printf("DSK %c fmt=%u(%s) %s %u/%u/%u BAD\r\n", rw, _cur_index, name, mode,
                     _cur_head, _cur_trk, _cur_sec);
}

uint16_t MediaTypeDSK::sector_size(uint32_t /*sectornum*/)
{
    // The transfer size stashed by decode_sector(): one sector, or -- in track
    // mode -- the whole track. The device uses it to size the host transfer.
    return _cur_xfer_size;
}

// Returns TRUE if an error condition occurred
error_is_true MediaTypeDSK::read(uint32_t sectornum, uint32_t *readcount)
{
    log_access('R');

    *readcount = 0;

    // A bad fmt/track/sector from the host errors here rather than seeking out
    // of range -- tighter than IMG's linear bounds check. (The "BAD" suffix in
    // the log line above already flags it.)
    if (!_cur_valid)
        RETURN_ERROR_AS_TRUE();

    memset(_buff, 0, _cur_xfer_size);

    bool err = fnio::fseek(_disk_fileh, _cur_offset, SEEK_SET) != 0;

    if (err == false)
        err = fnio::fread(_buff, 1, _cur_xfer_size, _disk_fileh) != _cur_xfer_size;

    *readcount = _cur_xfer_size;

    RETURN_ERROR_IF(err);
}

// Returns TRUE if an error condition occurred
error_is_true MediaTypeDSK::write(uint32_t sectornum, bool verify)
{
    log_access('W');

    // The geometry check is the ONLY bound -- there is deliberately no
    // actual-file-size check. A write to any in-range address is allowed even
    // when its offset is past the current end of file: the seek-past-EOF + write
    // extends the image. That is by design -- it is how a blank 0-byte image is
    // formatted (write each valid track and the file grows to the format's
    // maximum size). locate() already caps the address to that maximum
    // (num_tracks x sectors x sector_size), so a valid write can never extend
    // beyond the format's full size.
    if (!_cur_valid)
        RETURN_ERROR_AS_TRUE(); // "BAD" already logged by log_access('W')

    int e = fnio::fseek(_disk_fileh, _cur_offset, SEEK_SET);
    if (e != 0)
    {
        Debug_printf("::write seek error %d\r\n", e);
        RETURN_ERROR_AS_TRUE();
    }

    // Writes sector CONTENTS only -- never image layout. The dump stays
    // bit-for-bit re-writable to physical media (the raw-dump invariant).
    e = fnio::fwrite(_buff, 1, _cur_xfer_size, _disk_fileh);
    if (e != _cur_xfer_size)
    {
        Debug_printf("::write error %d, %d\r\n", e, errno);
        RETURN_ERROR_AS_TRUE();
    }

    // Since we might get reset at any moment, go ahead and sync the file. A
    // successful flush (0) is the norm and not worth a line per write; only a
    // non-zero result -- an actual sync failure -- is logged.
    int ret = fnio::fflush(_disk_fileh);
    if (ret != 0)
        Debug_printf("DSK::write fflush error:%d\r\n", ret);

    RETURN_SUCCESS_AS_FALSE();
}

// MediaTypeDSK has no geometry to report before the first CHS access (the
// host names its format per command), so this is a minimal clear/ready reply.
// A CHS boot path never issues STATUS/PERCOM.
void MediaTypeDSK::status(uint8_t statusbuff[4])
{
    statusbuff[0] = DISK_DRIVE_STATUS_CLEAR;
    statusbuff[1] = ~_disk_controller_status; // Negate the controller status
}

// Interim stub: real formatting needs a WD179x-style Write-Track path that
// parses a whole raw track per fmttype into sector offsets -- its own project.
// Until then FORMAT is refused rather than fabricating structural bytes into
// the raw dump.
error_is_true MediaTypeDSK::format(uint32_t *responsesize)
{
    Debug_print("DSK FORMAT not implemented\r\n");
    *responsesize = 0;
    RETURN_ERROR_AS_TRUE();
}

// Open the file and record its size; no header parse, no sidecar, no geometry
// inference. fmttype supplies geometry per access. host/filename are ignored
// (they exist only for MediaTypeROM's sibling-.cfg lookup).
mediatype_t MediaTypeDSK::mount(fnFile *f, uint32_t disksize, fujiHost *, const char *)
{
    Debug_print("DSK MOUNT\r\n");

    _disk_fileh      = f;
    _disk_image_size = disksize;
    _disktype        = MEDIATYPE_DSK;

    return _disktype;
}

// Grow the active staging buffer to hold at least `bytes`. Keeps the static
// _dsk_buff for anything that fits (the common case pays nothing) and
// repoints _buff at a right-sized heap block only when a custom geometry needs
// more. Returns false if the allocation failed (the old buffer is kept).
bool MediaTypeDSK::ensure_buffer(uint32_t bytes)
{
    if (bytes <= DSK_BUFFER_SIZE)
    {
        _buff      = _dsk_buff;
        _buff_size = DSK_BUFFER_SIZE;
        return true;
    }
    if (bytes > _buff_size || _heap_buff == nullptr)
    {
        uint8_t *p = (uint8_t *)realloc(_heap_buff, bytes);
        if (p == nullptr)
            return false; // keep the old buffer; the custom set fails cleanly
        _heap_buff = p;
    }
    _buff      = _heap_buff;
    _buff_size = bytes;
    return true;
}

// Declare a runtime custom format (FMT_CUSTOM) from a host-supplied region. One
// region per call; the `append` flag (params[7] bit1) accumulates several calls
// into a multi-region format. A fresh (non-append) call resets the builder.
// Params default so the uniform single-sided case is just 5 params.
void MediaTypeDSK::set_geometry(const uint32_t *params, unsigned count)
{
    if (count < 5)
    {
        _custom_valid = false;
        return;
    }

    uint16_t first_track = (uint16_t)params[0];
    uint16_t last_track  = (uint16_t)params[1];
    uint8_t  spt         = (uint8_t)params[2];
    uint16_t sec_size    = (uint16_t)params[3];
    uint8_t  first_sec   = (uint8_t)params[4];
    uint8_t  head_mask   = (count >= 6) ? (uint8_t)params[5] : 0xFF;
    uint8_t  num_sides   = (count >= 7) ? (uint8_t)params[6] : 1;
    uint8_t  flags       = (count >= 8) ? (uint8_t)params[7] : 0;
    bool     head_major  = (flags & 0x01) != 0;
    bool     append      = (flags & 0x02) != 0;

    // Fresh call: reset the region builder before adding region 0.
    if (!append)
    {
        _custom_region_count = 0;
        _custom_valid        = false;
    }

    if (!spt || !sec_size || sec_size > DSK_MAX_CUSTOM_SECTOR ||
        last_track < first_track || !num_sides ||
        _custom_region_count >= DSK_MAX_CUSTOM_REGIONS)
    {
        _custom_valid = false;
        return;
    }

    _custom_regions[_custom_region_count++] =
        {first_track, last_track, head_mask, spt, sec_size, first_sec};

    // Derive the format header and size the staging buffer for the largest track
    // over all regions declared so far.
    uint16_t max_track  = 0;
    uint32_t max_tbytes = 0;
    for (uint8_t i = 0; i < _custom_region_count; i++)
    {
        const DSKRegion &r = _custom_regions[i];
        if (r.last_track > max_track)
            max_track = r.last_track;
        uint32_t tb = (uint32_t)r.sectors * r.sector_size;
        if (tb > max_tbytes)
            max_tbytes = tb;
    }
    _custom_format = {"custom", (uint16_t)(max_track + 1), num_sides,
                      _custom_region_count, _custom_regions, head_major};
    _custom_valid  = ensure_buffer(max_tbytes);

    Debug_printf("DSK SET_GEOMETRY %s trk %u-%u %ux%u first=%u hm=%02X sides=%u "
                 "flags=%02X regions=%u -> %s\r\n",
                 append ? "append" : "fresh", first_track, last_track, spt, sec_size,
                 first_sec, head_mask, num_sides, flags, _custom_region_count,
                 _custom_valid ? "OK" : "BAD");
}

MediaTypeDSK::~MediaTypeDSK()
{
    if (_heap_buff != nullptr)
    {
        free(_heap_buff);
        _heap_buff = nullptr;
    }
}

#endif /* BUILD_RS232 */
