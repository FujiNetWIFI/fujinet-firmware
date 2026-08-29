#ifndef IMD_IMAGE_H
#define IMD_IMAGE_H

// IMD (ImageDisk) disk image reader/writer.
//
// Format per IMD.TXT section 6, Dave Dunfield, placed in the public domain by
// its author. Implemented from the specification only; the ImageDisk program
// source is separately licensed and was not consulted.
//
// Platform independent on purpose: this lives at the root of lib/media and must
// never include a platform mediaType.h, so any target can adapt it. The ESP
// build globs lib/media/*.cpp, so this compiles into every target.

#include <cstdint>
#include <string>
#include <vector>

#ifdef ESP_PLATFORM
#include "PSRAMAllocator.h"
#endif

// ASCII SUB, terminates the header/comment block
#define IMD_EOF_MARKER 0x1A

// Platform MediaType classes address sectors with uint16_t
#define IMD_MAX_SECTORS 65535

// Largest size code (6) is 8192 bytes; the 0xFF table can name anything
#define IMD_MAX_SIZE_CODE_BYTES 8192

// Give up looking for the comment terminator past this point
#define IMD_MAX_HEADER_SCAN 65536

// Comment is kept for display only, so cap what we retain
#define IMD_MAX_COMMENT 256

// Selects the per-sector size table instead of a single size code
#define IMD_SIZE_CODE_VARIABLE 0xFF

// Track head byte: flags in the top two bits, head in bit 0
#define IMD_HEAD_CYL_MAP  0x80
#define IMD_HEAD_HEAD_MAP 0x40
#define IMD_HEAD_MASK     0x3F

// Sector data record types (IMD.TXT 6.7). For 0x01..0x08, (type-1) is a bit
// field: 1 = compressed, 2 = deleted address mark, 4 = data error. Odd types
// carry a full sector; even types carry a single fill byte.
#define IMD_REC_UNAVAILABLE 0x00
#define IMD_REC_MAX         0x08
#define IMD_REC_COMPRESSED  0x01
#define IMD_REC_DELETED     0x02
#define IMD_REC_ERROR       0x04

enum class ImdStatus : uint8_t
{
    Ok = 0,
    NotImd,          // no comment terminator, or no parsable track at all
    Truncated,       // ran out of bytes mid-structure
    BadRecordType,   // sector record type > 0x08; length is type-derived so no resync
    BadSizeCode,     // size code 7..0xFE
    NoSuchSector,
    Unavailable,     // record type 0x00: the sector was never readable
    BufferTooSmall,
    WriteRefused,    // would change the record's on-disk length
    ReadOnly,
    IoError,
    TooLarge,        // more than IMD_MAX_SECTORS sectors
};

const char *imd_status_str(ImdStatus s);

// Track recording mode (IMD.TXT 6.1). The rate is the controller transfer rate;
// FM carries half that as data. 6 and 9 are a libdsk extension for 1Mbps that we
// accept on read but never write.
uint16_t imd_mode_rate_kbps(uint8_t mode);
bool     imd_mode_is_mfm(uint8_t mode);
bool     imd_mode_is_valid(uint8_t mode);

struct ImdSectorInfo
{
    uint16_t size;
    uint8_t  id;          // physical ID from the sector numbering map
    uint8_t  cyl;         // logical cylinder ID: cylinder map, else the track's
    uint8_t  head;        // logical head ID: head map, else the track's
    uint8_t  rec_type;    // raw record type byte
    bool     deleted;
    bool     had_error;
    bool     compressed;
    bool     unavailable;
};

struct ImdTrackInfo
{
    uint32_t first_lba;
    uint16_t nsec;
    uint8_t  mode;
    uint8_t  cyl;         // physical
    uint8_t  head;        // physical
    uint8_t  size_code;
    bool     has_cyl_map;
    bool     has_head_map;
};

// Byte source. Deliberately not fnFile*: fnFile is FileHandler on some targets
// and std::FILE on others, which would make any caller (including tests)
// target-specific. ImdFnFileSource below is the fnFile binding.
class ImdSource
{
public:
    virtual ~ImdSource() = default;
    virtual bool     read_at(uint32_t off, void *dst, uint32_t len) = 0;
    virtual bool     write_at(uint32_t off, const void *src, uint32_t len) { (void)off; (void)src; (void)len; return false; }
    virtual uint32_t size() = 0;
    virtual bool     flush() { return true; }
    virtual bool     writable() const { return false; }
};

class ImdCursor;

class ImdImage
{
public:
    ImdImage() = default;
    ~ImdImage();
    ImdImage(const ImdImage &) = delete;
    ImdImage &operator=(const ImdImage &) = delete;

    // Indexes the whole image; src must outlive the ImdImage.
    ImdStatus open(ImdSource *src, bool writable);
    void      close();
    bool      is_open() const { return _src != nullptr; }

    uint32_t lba_count() const { return (uint32_t)_sectors.size(); }
    uint32_t track_count() const { return (uint32_t)_tracks.size(); }
    uint16_t sector_size(uint32_t lba) const;   // 0 if out of range
    uint16_t max_sector_size() const { return _max_sector_size; }
    bool     uniform_sector_size() const { return _uniform_size; }

    bool track_info(uint32_t track, ImdTrackInfo &out) const;
    bool sector_info(uint32_t lba, ImdSectorInfo &out) const;

    // Matches the track's *physical* cyl/head and the sector's physical ID.
    bool find_lba(uint8_t cyl, uint8_t head, uint8_t id, uint32_t &lba) const;

    // Data-error records still return Ok with their recovered contents; check
    // sector_info().had_error. out_len may be null.
    ImdStatus read_sector(uint32_t lba, uint8_t *buf, uint32_t buflen, uint16_t *out_len);

    // len must equal sector_size(lba). Writes are in place only: a compressed
    // record can only absorb a uniform buffer, and an unavailable record cannot
    // absorb anything. Expand the image first (IMDU /E) if that is a problem.
    ImdStatus write_sector(uint32_t lba, const uint8_t *buf, uint16_t len);

    // Flat byte view over the sectors in LBA order, for fixed-block hosts.
    uint32_t  linear_size() const { return _linear_size; }
    ImdStatus read_linear(uint32_t byte_off, uint8_t *buf, uint32_t len);
    ImdStatus write_linear(uint32_t byte_off, const uint8_t *buf, uint32_t len);

    // Bytes after the last good track, e.g. XMODEM packet padding.
    uint32_t    trailing_garbage() const { return _trailing; }
    const char *comment() const { return _comment.c_str(); }

    static bool looks_like_imd_extension(const char *filename);

private:
    // Kept small: one of these exists per sector for the life of the mount.
    struct SectorRef
    {
        uint32_t data_off;   // payload offset; the type byte is at data_off-1
        uint16_t size;
        uint8_t  id;
        uint8_t  id_cyl;
        uint8_t  id_head;
        uint8_t  rec_type;
    };

    struct TrackRef
    {
        uint32_t first_lba;
        uint32_t first_byte;  // offset into the linear view
        uint16_t nsec;
        uint8_t  mode;
        uint8_t  cyl;
        uint8_t  head;
        uint8_t  size_code;
        uint8_t  map_flags;   // IMD_HEAD_CYL_MAP / IMD_HEAD_HEAD_MAP as stored
    };

#ifdef ESP_PLATFORM
    std::vector<SectorRef, PSRAMAllocator<SectorRef>> _sectors;
    std::vector<TrackRef, PSRAMAllocator<TrackRef>> _tracks;
#else
    std::vector<SectorRef> _sectors;
    std::vector<TrackRef> _tracks;
#endif

    ImdSource  *_src = nullptr;
    bool        _writable = false;
    uint32_t    _trailing = 0;
    uint32_t    _linear_size = 0;
    uint16_t    _max_sector_size = 0;
    bool        _uniform_size = true;
    std::string _comment;

    // Sized once at open() so the linear path does not allocate per call
    std::vector<uint8_t> _scratch;

    ImdStatus _parse();
    ImdStatus _read_comment(uint32_t &data_start);
    // header_ok distinguishes a bad track header (likely transport padding)
    // from a failure inside an otherwise valid track (a real error).
    ImdStatus _parse_track(ImdCursor &cur, bool &header_ok);
    bool      _tail_is_padding(uint32_t from);
    bool      _locate_linear(uint32_t byte_off, uint32_t &lba, uint32_t &sec_off) const;
    ImdStatus _materialize(uint32_t lba, uint32_t sec_off, const uint8_t *src,
                           uint32_t n, uint8_t *out);
};

#endif // IMD_IMAGE_H
