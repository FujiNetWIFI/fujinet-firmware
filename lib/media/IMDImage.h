#ifndef IMD_IMAGE_H
#define IMD_IMAGE_H

// IMD (ImageDisk) disk image reader, writer and builder.
//
// Format per IMD.TXT section 6, Dave Dunfield, placed in the public domain by
// its author. Implemented from the specification only; the ImageDisk program
// source is separately licensed and was not consulted.
//
// Standard C++ only, so a host tool can build it as well as the firmware. It works
// on the file handle it is given: a std::fstream, a std::FILE, or anything with
// fnFile's seek(), tell(), read(), write() and flush().
//
// Deliberately not a MediaType: every platform declares its own incompatible
// MediaType, so each platform's adapter (e.g. rs232/diskTypeIMD) wraps this.

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <istream>
#include <memory_resource>
#include <string>
#include <type_traits>
#include <vector>

// ASCII SUB, terminates the header/comment block
#define IMD_EOF_MARKER 0x1A

// Bounds the per-mount sector table; also fits platforms that address sectors
// with uint16_t
#define IMD_MAX_SECTORS 65535

// Largest size code (6) is 8192 bytes; the 0xFF table can name anything
#define IMD_MAX_SIZE_CODE_BYTES 8192

// Give up looking for the comment terminator past this point
#define IMD_MAX_HEADER_SCAN 65536

// Comment is kept for display only, so cap what we retain
#define IMD_MAX_COMMENT 256

// Selects the per-sector size table instead of a single size code
#define IMD_SIZE_CODE_VARIABLE 0xFF

// Track head byte: map flags in the top two bits, the head in the low six
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

enum class IMDStatus : uint8_t
{
    Ok = 0,
    NotIMD,          // no comment terminator, or no parsable track at all
    Truncated,       // ran out of bytes mid-structure
    BadRecordType,   // sector record type > 0x08; length is type-derived so no resync
    BadSizeCode,     // size code 7..0xFE
    NoSuchSector,
    Unavailable,     // record type 0x00: the sector was never readable
    BufferTooSmall,
    WriteRefused,    // an unavailable record, or not the sector's length
    ReadOnly,
    IoError,
    TooLarge,        // more than IMD_MAX_SECTORS sectors, or past 4 GiB
};

struct IMDSectorInfo
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

struct IMDTrackInfo
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

// One sector for writeTrack(), in physical order. A null data writes it as never
// readable.
struct IMDSectorSpec
{
    uint8_t        sector_id;
    uint16_t       size;
    const uint8_t *data;
};

class IMDImage
{
public:
    // file must outlive the IMDImage, which never closes it. memory holds the sector
    // and track tables.
    template <typename File>
    explicit IMDImage(File *file,
                      std::pmr::memory_resource *memory = std::pmr::get_default_resource())
        : _file(_wrap(file)), _sectors(memory), _tracks(memory)
    {
    }
    IMDImage(const IMDImage &) = delete;
    IMDImage &operator=(const IMDImage &) = delete;

    // Indexes the whole image
    IMDStatus open(bool writable);
    void   close();
    bool   isOpen() const { return _open; }

    uint32_t lbaCount() const { return (uint32_t)_sectors.size(); }
    uint32_t trackCount() const { return (uint32_t)_tracks.size(); }
    uint16_t sectorSize(uint32_t lba) const;   // 0 when out of range
    uint16_t maxSectorSize() const { return _max_sector_size; }
    bool     uniformSectorSize() const { return _uniform_size; }
    uint32_t trailingGarbage() const { return _trailing; } // e.g. XMODEM padding
    const char *comment() const { return _comment.c_str(); }

    IMDStatus trackInfo(uint32_t track, IMDTrackInfo &info) const;
    IMDStatus sectorInfo(uint32_t lba, IMDSectorInfo &info) const;
    // Matches the track's physical cylinder and head
    IMDStatus findLBA(uint8_t cyl, uint8_t head, uint8_t sector_id, uint32_t &lba) const;

    // A data-error record returns Ok with its recovered contents; check had_error.
    // out_len may be null.
    IMDStatus readSector(uint32_t lba, uint8_t *buf, uint32_t buf_len, uint16_t *out_len);

    // len must equal sectorSize(lba). A compressed record takes a uniform buffer in
    // place; anything else first grows every compressed record in the image into a
    // full one, rewriting the file once. An unavailable record takes nothing.
    IMDStatus writeSector(uint32_t lba, const uint8_t *buf, uint16_t len);

    // Flat byte view over the sectors in LBA order, for fixed-block hosts
    uint32_t  linearSize() const { return _linear_size; }
    IMDStatus readLinear(uint32_t byte_off, uint8_t *buf, uint32_t len);
    IMDStatus writeLinear(uint32_t byte_off, const uint8_t *buf, uint32_t len);

    // A new image in an empty file: the signature line stamped with `when`, an optional
    // comment, then one writeTrack() per track, appended in order.
    IMDStatus writeHeader(time_t when, const char *comment = nullptr);
    // Sizes that are all 128 << n use a size code; anything else gets the size table.
    IMDStatus writeTrack(uint8_t mode, uint8_t cyl, uint8_t head,
                         const std::vector<IMDSectorSpec> &sectors);
    // Sectors 1..nsec filled with `fill`, as full records so writes stay in place
    IMDStatus writeBlankTrack(uint8_t mode, uint8_t cyl, uint8_t head, uint8_t nsec,
                              uint8_t size_code, uint8_t fill);

    static const char *statusString(IMDStatus status);

    // Track recording mode (IMD.TXT 6.1). The rate is the controller transfer rate;
    // FM carries half that as data. 6 and 9 are a libdsk extension for 1Mbps.
    static uint16_t modeRateKBPS(uint8_t mode);
    static bool     modeIsMFM(uint8_t mode);
    static bool     modeIsValid(uint8_t mode);

    static bool looksLikeExtension(const char *filename);

private:
    // The file handle as a pointer and five calls, so nothing past _wrap() needs its type
    struct Handle
    {
        void *file;
        bool     (*seek_fn)(void *file, uint32_t off);
        bool     (*read_fn)(void *file, void *dst, uint32_t len);
        bool     (*write_fn)(void *file, const void *src, uint32_t len);
        bool     (*flush_fn)(void *file);
        uint32_t (*end_fn)(void *file);

        bool     seek(uint32_t off) const { return seek_fn(file, off); }
        bool     read(void *dst, uint32_t len) const { return read_fn(file, dst, len); }
        bool     write(const void *src, uint32_t len) const
        {
            return write_fn(file, src, len);
        }
        bool     flush() const { return flush_fn(file); }
        uint32_t end() const { return end_fn(file); }
    };

    template <typename File> struct Calls;

    template <typename File> static Handle _wrap(File *file)
    {
        return {file, Calls<File>::seek, Calls<File>::read, Calls<File>::write,
                Calls<File>::flush, Calls<File>::end};
    }

    // Kept small: one per sector for the life of the mount
    struct SectorRef
    {
        uint32_t data_off;   // the record type byte is at data_off - 1
        uint16_t size;
        uint8_t  sector_id;
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

    class Cursor;

    Handle      _file;
    bool        _open = false;
    bool        _writable = false;
    uint32_t    _size = 0;
    uint32_t    _trailing = 0;
    uint32_t    _linear_size = 0;
    uint16_t    _max_sector_size = 0;
    bool        _uniform_size = true;
    std::string _comment;
    std::vector<uint8_t> _scratch;   // one sector, sized at open() for the linear path

    std::pmr::vector<SectorRef> _sectors;
    std::pmr::vector<TrackRef>  _tracks;

    bool   _readAt(uint32_t off, void *dst, uint32_t len);
    bool   _writeAt(uint32_t off, const void *src, uint32_t len);
    bool   _append(const void *src, uint32_t len);

    IMDStatus _readComment(uint32_t &data_start);
    bool   _tailIsPadding(uint32_t from);
    // header_ok tells a bad track header (likely padding) from a failure inside a
    // valid track (a real error)
    IMDStatus _parseTrack(Cursor &cursor, bool &header_ok);
    IMDStatus _parse();
    IMDStatus _shiftUp(uint32_t from, uint32_t end, uint32_t shift);
    IMDStatus _expand();
    IMDStatus _locateLinear(uint32_t byte_off, uint32_t &lba, uint32_t &sec_off) const;
    IMDStatus _materialize(uint32_t lba, uint32_t sec_off, const uint8_t *src, uint32_t len,
                           uint8_t *out);
};

// The one place that knows the handle's type: a std::fstream, a std::FILE, or
// anything else with fnFile's methods
template <typename File> struct IMDImage::Calls
{
    static bool seek(void *handle, uint32_t off)
    {
        File *file = static_cast<File *>(handle);
        if constexpr (std::is_base_of_v<std::ios_base, File>)
        {
            file->clear();
            file->seekg(off);
            file->seekp(off);
            return !file->fail();
        }
        else if constexpr (std::is_same_v<File, std::FILE>)
            return std::fseek(file, (long)off, SEEK_SET) == 0;
        else
            return file->seek((long)off, SEEK_SET) == 0;
    }

    static bool read(void *handle, void *dst, uint32_t len)
    {
        File *file = static_cast<File *>(handle);
        if constexpr (std::is_base_of_v<std::ios_base, File>)
        {
            file->read(static_cast<char *>(dst), len);
            return (uint32_t)file->gcount() == len;
        }
        else if constexpr (std::is_same_v<File, std::FILE>)
            return std::fread(dst, 1, len, file) == len;
        else
            return file->read(dst, 1, len) == len;
    }

    static bool write(void *handle, const void *src, uint32_t len)
    {
        File *file = static_cast<File *>(handle);
        if constexpr (std::is_base_of_v<std::ios_base, File>)
        {
            file->write(static_cast<const char *>(src), len);
            return !file->fail();
        }
        else if constexpr (std::is_same_v<File, std::FILE>)
            return std::fwrite(src, 1, len, file) == len;
        else
            return file->write(src, 1, len) == len;
    }

    static bool flush(void *handle)
    {
        File *file = static_cast<File *>(handle);
        if constexpr (std::is_base_of_v<std::ios_base, File>)
            return !file->flush().fail();
        else if constexpr (std::is_same_v<File, std::FILE>)
            return std::fflush(file) == 0;
        else
            return file->flush() == 0;
    }

    static uint32_t end(void *handle)
    {
        File *file = static_cast<File *>(handle);
        long  end;
        if constexpr (std::is_base_of_v<std::ios_base, File>)
        {
            file->clear();
            file->seekg(0, std::ios::end);
            end = (long)file->tellg();
        }
        else if constexpr (std::is_same_v<File, std::FILE>)
            end = std::fseek(file, 0, SEEK_END) == 0 ? std::ftell(file) : -1;
        else
            end = file->seek(0, SEEK_END) == 0 ? file->tell() : -1;
        return end < 0 ? 0 : (uint32_t)end;
    }
};

#endif // IMD_IMAGE_H
