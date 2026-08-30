#ifndef _MEDIATYPE_DSK_RS232
#define _MEDIATYPE_DSK_RS232

#include "diskType.h" // pulls in MediaType

// The staging buffer must hold the largest single transfer. In track mode
// (see fmttype below) that is a whole track, not just the largest sector: the
// largest baked sector is IBM 8" DD (256 B) but the largest baked track is an
// IBM 8" DD track (26 x 256 = 6656 B), so the track bound dominates. This buffer
// is private to MediaTypeDSK so MediaTypeImg / MediaTypeROM keep their
// 512-byte base buffer (DISK_SECTORBUF_SIZE). A custom format (FMT_CUSTOM) may
// declare a larger sector or track; ensure_buffer() grows the staging buffer
// onto the heap when that happens (see below and diskTypeDSK.cpp).
#define DSK_MAX_SECTOR_SIZE 256                     // IBM 8" DD single sector
#define DSK_MAX_TRACK_SIZE  6656                    // IBM 8" DD track: 26 x 256
#define DSK_BUFFER_SIZE     DSK_MAX_TRACK_SIZE   // holds a sector OR a track

// Custom-format (FMT_CUSTOM) bounds. A custom sector is capped at 1024 B; a
// custom format may have up to 8 regions (Tarbell needs 2; per-side special
// tracks fit comfortably).
#define DSK_MAX_CUSTOM_SECTOR  1024
#define DSK_MAX_CUSTOM_REGIONS 8

// A region describes a run of tracks whose geometry is uniform. A format that
// is uniform end-to-end is a single region; a mixed-density disk (e.g. Tarbell
// DD, whose track 0 differs from tracks 1-76) is several.
struct DSKRegion
{
    uint16_t first_track;  // inclusive
    uint16_t last_track;   // inclusive
    uint8_t  head_mask;    // sides this applies to: bit0=side0, bit1=side1; 0xFF = all
    uint8_t  sectors;      // sectors per track (per side) in this region
    uint16_t sector_size;  // bytes
    uint8_t  first_sector; // sector-number of the first sector on a track
};

struct DSKFormat
{
    const char         *name;
    uint16_t            num_tracks;
    uint8_t             num_sides;
    uint8_t             num_regions;
    const DSKRegion *regions;
    bool                head_major; // false = interleaved (T0H0,T0H1,T1H0,...)
                                    // true  = sequential  (all H0 tracks, then all H1)
};

// fmttype values sent by the host; index into dsk_formats[] (0x0F = custom).
enum dsk_fmt_t
{
    FMT_IBM_SD = 0, // IBM 8" SD  : 1 side, 77 trk, 26 spt, 128B
    FMT_IBM_DD,     // IBM 8" DD  : 1 side, 77 trk, 26 spt, 256B
    FMT_ALTAIR,     // Altair 8"  : 1 side, 77 trk, 32 spt, 137B
    FMT_FDCPLUS,    // FDC+       : 1 side, 2048 trk, 32 spt, 137B (Altair geom, more trk)
    FMT_TARBELL_DD, // Tarbell DD : 1 side, trk0 26x128 + trk1-76 51x128
    FMT_COUNT       // baked-format count; indices 5-14 reserved, 15 (FMT_CUSTOM) is custom
};

// param(3), "fmttype", packs three fields: bits 0-3 index dsk_formats[]
// (0x0F selects a runtime custom format, set via DISKCMD_SET_GEOMETRY), bit 6
// selects the access mode (single sector vs. whole track), and bits 4,5,7 are
// reserved. The host ORs the mode into the format index on every read/write;
// any reserved bit set is refused (never silently treated as a sector access).
#define FMT_INDEX_MASK    0x0F // low 4 bits: format index (16 formats)
#define FMT_CUSTOM        0x0F //   index 15: geometry set via DISKCMD_SET_GEOMETRY
#define FMT_MODE_TRACK    0x40 // bit 6: whole-track access (clear = single sector)
#define FMT_RESERVED_MASK 0xB0 // bits 4,5,7 -- must be zero

extern const DSKFormat dsk_formats[FMT_COUNT];

class MediaTypeDSK : public MediaType
{
private:
    // Default staging buffer, sized to the largest baked transfer (a whole track
    // in track mode). _buff/_buff_size point here by default and are repointed at
    // a heap block when a custom geometry needs more (§5.8). Private to this type
    // so MediaTypeImg/MediaTypeROM keep their 512-byte base buffer.
    uint8_t  _dsk_buff[DSK_BUFFER_SIZE]; // this type's own staging buffer
    uint8_t *_buff      = _dsk_buff;        // active staging buffer (may be heap)
    uint32_t _buff_size = DSK_BUFFER_SIZE;
    uint8_t *_heap_buff = nullptr;             // owned overflow buffer, freed on dtor

    uint32_t _cur_offset    = 0;     // byte offset of the addressed sector/track
    uint16_t _cur_xfer_size = 0;     // bytes to transfer: one sector, or a whole track
    bool     _cur_valid     = false; // false if the last address was out of range

    // Raw address fields from the last decode_sector(), kept only so read()/
    // write() can emit one compact debug line describing the access.
    uint8_t  _cur_head  = 0;
    uint16_t _cur_trk   = 0;
    uint16_t _cur_sec   = 0;
    uint8_t  _cur_index = 0;         // format index (fmttype & FMT_INDEX_MASK)
    uint8_t  _cur_mode  = 0;         // 0 = sector, 1 = track, 2 = unknown/bad

    // Runtime custom format (FMT_CUSTOM), built by set_geometry() from
    // host-supplied regions. Baked formats live in dsk_formats[]; this is the
    // index-15 slot.
    DSKRegion _custom_regions[DSK_MAX_CUSTOM_REGIONS];
    uint8_t      _custom_region_count = 0;
    DSKFormat _custom_format {};
    bool         _custom_valid = false;

    // Emit one line: "DSK <rw> fmt=<i>(<name>) <SEC|TRK> h/t/s <size> @<hexoff>"
    // (or "... BAD" when the address failed the geometry check). rw is 'R'/'W'.
    void log_access(char rw);

    // Resolve (fmt, head, trk, sec) -> file offset + transfer size. track_mode
    // selects a whole track (sector is then ignored) rather than a single
    // sector. Pure arithmetic over dsk_formats[] (or the custom slot); no
    // file I/O. Returns false if the address is out of range for the format.
    bool locate(uint8_t fmt, uint8_t head, uint16_t trk, uint16_t sec,
                bool track_mode, uint32_t *offset, uint16_t *size);

    // Grow the active staging buffer to hold at least `bytes` (keeps the static
    // _dsk_buff for anything that fits; heap-allocs only when a custom
    // geometry is larger). Returns false if the allocation failed.
    bool ensure_buffer(uint32_t bytes);

public:
    uint32_t decode_sector(const uint32_t *params, unsigned count) override;
    void     set_geometry(const uint32_t *params, unsigned count) override; // FMT_CUSTOM (§5.8)

    // Staging buffer overrides -- hand the device this type's active buffer.
    uint8_t  *sector_buffer()      override { return _buff; }
    uint32_t  sector_buffer_size() override { return _buff_size; }

    mediatype_t   mount(fnFile *f, uint32_t disksize, fujiHost *host = nullptr,
                        const char *filename = nullptr) override;
    error_is_true read(uint32_t sectornum, uint32_t *readcount) override;
    error_is_true write(uint32_t sectornum, bool verify) override;
    uint16_t      sector_size(uint32_t sectornum) override;
    void          status(uint8_t statusbuff[4]) override;
    error_is_true format(uint32_t *responsesize) override;
    ~MediaTypeDSK() override; // frees _heap_buff

    // Inspection accessors for the last decode_sector() result. Const, no side
    // effects; used by the unit tests to assert the resolved byte offset and
    // validity without reaching into private state. sector_size() already
    // exposes the resolved size.
    uint32_t cur_offset() const { return _cur_offset; }
    bool     cur_valid()  const { return _cur_valid; }
};

#endif // _MEDIATYPE_DSK_RS232
