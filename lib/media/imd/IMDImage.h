#ifndef IMD_IMAGE_H
#define IMD_IMAGE_H

// IMD (ImageDisk) disk image reader/writer.
//
// Format per IMD.TXT section 6, Dave Dunfield, placed in the public domain by
// its author. Implemented from the specification only; the ImageDisk program
// source is separately licensed and was not consulted.
//
// Depends on the C++ standard library only, so host tools such as MekkoGX can
// build it too. Nothing from FujiNet or the ESP32 may be included here.
//
// Deliberately not a MediaType: every platform declares its own incompatible
// MediaType, so each platform's adapter (e.g. rs232/diskTypeIMD) subclasses its
// own and forwards here.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <fstream>
#include <memory_resource>
#include <string>
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
    TooLarge,        // more than IMD_MAX_SECTORS sectors
};

const char *imd_status_str(IMDStatus s);

// What IMDImage<File> calls on its file. Another file type supplies the same
// four overloads, declared before IMDImage<File> is used.
IMDStatus imd_read_at(std::fstream &file, uint32_t off, void *dst, uint32_t len);
IMDStatus imd_write_at(std::fstream &file, uint32_t off, const void *src, uint32_t len);
uint32_t  imd_size(std::fstream &file);
IMDStatus imd_flush(std::fstream &file);

bool imd_looks_like_extension(const char *filename);

// Track recording mode (IMD.TXT 6.1). The rate is the controller transfer rate;
// FM carries half that as data. 6 and 9 are a libdsk extension for 1Mbps that we
// accept on read but never write.
uint16_t imd_mode_rate_kbps(uint8_t mode);
bool     imd_mode_is_mfm(uint8_t mode);
bool     imd_mode_is_valid(uint8_t mode);

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

template <typename File>
class IMDCursor;

template <typename File>
class IMDImage
{
public:
    // The sector tables come from memory, e.g. PSRAM on the ESP32.
    explicit IMDImage(std::pmr::memory_resource *memory = std::pmr::get_default_resource())
        : _sectors(memory), _tracks(memory) {}
    ~IMDImage();
    IMDImage(const IMDImage &) = delete;
    IMDImage &operator=(const IMDImage &) = delete;

    // Indexes the whole image; file must outlive the IMDImage, which never closes it.
    IMDStatus open(File *file, uint32_t size, bool writable);
    void      close();
    bool      is_open() const { return _file != nullptr; }

    uint32_t lba_count() const { return (uint32_t)_sectors.size(); }
    uint32_t track_count() const { return (uint32_t)_tracks.size(); }
    uint16_t sector_size(uint32_t lba) const;   // 0 if out of range
    uint16_t max_sector_size() const { return _max_sector_size; }
    bool     uniform_sector_size() const { return _uniform_size; }

    IMDStatus track_info(uint32_t track, IMDTrackInfo &out) const;
    IMDStatus sector_info(uint32_t lba, IMDSectorInfo &out) const;

    // Matches the track's *physical* cyl/head and the sector's physical ID.
    IMDStatus find_lba(uint8_t cyl, uint8_t head, uint8_t id, uint32_t &lba) const;

    // Data-error records still return Ok with their recovered contents; check
    // sector_info().had_error. out_len may be null.
    IMDStatus read_sector(uint32_t lba, uint8_t *buf, uint32_t buflen, uint16_t *out_len);

    // len must equal sector_size(lba). A compressed record takes a uniform buffer
    // in place; anything else first grows every compressed record in the image
    // into a full one, rewriting the file once. An unavailable record takes nothing.
    IMDStatus write_sector(uint32_t lba, const uint8_t *buf, uint16_t len);

    // Flat byte view over the sectors in LBA order, for fixed-block hosts.
    uint32_t  linear_size() const { return _linear_size; }
    IMDStatus read_linear(uint32_t byte_off, uint8_t *buf, uint32_t len);
    IMDStatus write_linear(uint32_t byte_off, const uint8_t *buf, uint32_t len);

    // Bytes after the last good track, e.g. XMODEM packet padding.
    uint32_t    trailing_garbage() const { return _trailing; }
    const char *comment() const { return _comment.c_str(); }

    // A new image, each appended to file: the signature line stamped with `when`,
    // then its tracks.
    static IMDStatus write_header(File &file, time_t when);
    // One track of sectors 1..nsec filled with `fill`, stored as full records so
    // write_sector() can later take any data in place.
    static IMDStatus write_blank_track(File &file, uint8_t mode, uint8_t cyl, uint8_t head,
                                       uint8_t nsec, uint8_t size_code, uint8_t fill);

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

    std::pmr::vector<SectorRef> _sectors;
    std::pmr::vector<TrackRef>  _tracks;

    File       *_file = nullptr;
    uint32_t    _size = 0;
    bool        _writable = false;
    uint32_t    _trailing = 0;
    uint32_t    _linear_size = 0;
    uint16_t    _max_sector_size = 0;
    bool        _uniform_size = true;
    std::string _comment;

    // Sized once at open() so the linear path does not allocate per call
    std::vector<uint8_t> _scratch;

    IMDStatus _parse();
    IMDStatus _read_comment(uint32_t &data_start);
    // header_ok distinguishes a bad track header (likely transport padding)
    // from a failure inside an otherwise valid track (a real error).
    IMDStatus _parse_track(IMDCursor<File> &cur, bool &header_ok);
    bool      _tail_is_padding(uint32_t from);
    IMDStatus _locate_linear(uint32_t byte_off, uint32_t &lba, uint32_t &sec_off) const;
    IMDStatus _materialize(uint32_t lba, uint32_t sec_off, const uint8_t *src,
                           uint32_t n, uint8_t *out);
    IMDStatus _shift_up(uint32_t from, uint32_t end, uint32_t shift);
    IMDStatus _expand();
};

// Bounds-checked absolute-offset access, so the range checks live in one place.
template <typename File>
bool imd_read_bounded(File *file, uint32_t size, uint32_t off, void *dst, uint32_t len)
{
    if (len == 0)
        return true;
    if (off > size || len > size - off)
        return false;
    return imd_read_at(*file, off, dst, len) == IMDStatus::Ok;
}

template <typename File>
bool imd_write_bounded(File *file, uint32_t size, uint32_t off, const void *src, uint32_t len)
{
    if (len == 0)
        return true;
    if (off > size || len > size - off)
        return false;
    return imd_write_at(*file, off, src, len) == IMDStatus::Ok;
}

// Buffered forward reader. Indexing an 8" image walks ~2000 sector records; one
// read per record would make mounting crawl on SD/TNFS.
template <typename File>
class IMDCursor
{
public:
    IMDCursor(File *file, uint32_t size) : _file(file), _size(size) {}

    uint32_t pos() const { return _pos; }
    uint32_t remaining() const { return _pos < _size ? _size - _pos : 0; }
    bool     at_end() const { return _pos >= _size; }
    void     seek(uint32_t off) { _pos = off; }

    bool read(void *dst, uint32_t len)
    {
        uint8_t *out = static_cast<uint8_t *>(dst);
        while (len > 0)
        {
            if (!_fill())
                return false;
            uint32_t used = _pos - _buf_off;
            uint32_t avail = _buf_len - used;
            uint32_t n = len < avail ? len : avail;
            memcpy(out, _buf + used, n);
            out += n;
            _pos += n;
            len -= n;
        }
        return true;
    }

    bool byte(uint8_t &b) { return read(&b, 1); }

    bool skip(uint32_t len)
    {
        if (len > remaining())
            return false;
        _pos += len;
        return true;
    }

private:
    bool _fill()
    {
        if (_pos >= _size)
            return false;
        if (_buf_len > 0 && _pos >= _buf_off && _pos < _buf_off + _buf_len)
            return true;
        uint32_t n = _size - _pos;
        if (n > sizeof(_buf))
            n = sizeof(_buf);
        if (!imd_read_bounded(_file, _size, _pos, _buf, n))
        {
            _buf_len = 0;
            return false;
        }
        _buf_off = _pos;
        _buf_len = n;
        return true;
    }

    File      *_file;
    uint32_t   _size;
    uint32_t   _pos = 0;
    uint32_t _buf_off = 0;
    uint32_t _buf_len = 0;
    uint8_t  _buf[512];
};

template <typename File>
IMDImage<File>::~IMDImage()
{
    close();
}

template <typename File>
IMDStatus IMDImage<File>::write_header(File &file, time_t when)
{
    char hdr[48];
    size_t len = strftime(hdr, sizeof(hdr), "IMD 1.18: %d/%m/%Y %H:%M:%S\r\n", localtime(&when));

    hdr[len++] = IMD_EOF_MARKER;
    return imd_write_at(file, imd_size(file), hdr, (uint32_t)len);
}

template <typename File>
IMDStatus IMDImage<File>::write_blank_track(File &file, uint8_t mode, uint8_t cyl, uint8_t head,
                                      uint8_t nsec, uint8_t size_code, uint8_t fill)
{
    std::vector<uint8_t> track = {mode, cyl, head, nsec, size_code};

    for (uint8_t id = 1; id <= nsec; id++)
        track.push_back(id);
    for (uint8_t i = 0; i < nsec; i++)
    {
        track.push_back(1); // a full sector of data follows
        track.insert(track.end(), 128u << size_code, fill);
    }
    return imd_write_at(file, imd_size(file), track.data(), (uint32_t)track.size());
}

template <typename File>
IMDStatus IMDImage<File>::open(File *file, uint32_t size, bool writable)
{
    close();
    if (file == nullptr)
        return IMDStatus::IoError;

    _file = file;
    _size = size;
    _writable = writable;

    IMDStatus st = _parse();
    if (st != IMDStatus::Ok)
        close();
    return st;
}

template <typename File>
void IMDImage<File>::close()
{
    _sectors.clear();
    _sectors.shrink_to_fit();
    _tracks.clear();
    _tracks.shrink_to_fit();
    _scratch.clear();
    _scratch.shrink_to_fit();
    _comment.clear();
    _file = nullptr;   // borrowed, never ours to close
    _size = 0;
    _writable = false;
    _trailing = 0;
    _linear_size = 0;
    _max_sector_size = 0;
    _uniform_size = true;
}

// Everything before the 0x1A is header+comment. The "IMD v.vv:" signature line
// is optional - SIMH omits it entirely - so only the terminator is required.
template <typename File>
IMDStatus IMDImage<File>::_read_comment(uint32_t &data_start)
{
    uint32_t total = _size;
    uint32_t limit = total < IMD_MAX_HEADER_SCAN ? total : IMD_MAX_HEADER_SCAN;
    uint8_t  buf[128];
    uint32_t off = 0;

    while (off < limit)
    {
        uint32_t n = limit - off;
        if (n > sizeof(buf))
            n = sizeof(buf);
        if (!imd_read_bounded(_file, _size, off, buf, n))
            return IMDStatus::IoError;

        for (uint32_t i = 0; i < n; i++)
        {
            if (buf[i] == IMD_EOF_MARKER)
            {
                data_start = off + i + 1;
                if (_comment.compare(0, 4, "IMD ") == 0)
                {
                    size_t nl = _comment.find('\n');
                    _comment.erase(0, nl == std::string::npos ? _comment.size() : nl + 1);
                }
                return IMDStatus::Ok;
            }
            if (_comment.size() < IMD_MAX_COMMENT)
                _comment.push_back((char)buf[i]);
        }
        off += n;
    }
    return IMDStatus::NotIMD;
}

// True if everything from `from` to EOF is one repeated byte, the signature of
// transport padding (XMODEM rounds up to a packet boundary).
template <typename File>
bool IMDImage<File>::_tail_is_padding(uint32_t from)
{
    uint32_t total = _size;
    if (from >= total)
        return true;

    uint8_t  buf[128];
    uint8_t  first = 0;
    bool     have_first = false;
    uint32_t off = from;

    while (off < total)
    {
        uint32_t n = total - off;
        if (n > sizeof(buf))
            n = sizeof(buf);
        if (!imd_read_bounded(_file, _size, off, buf, n))
            return false;
        for (uint32_t i = 0; i < n; i++)
        {
            if (!have_first)
            {
                first = buf[i];
                have_first = true;
                continue;
            }
            if (buf[i] != first)
                return false;
        }
        off += n;
    }
    return true;
}

template <typename File>
IMDStatus IMDImage<File>::_parse()
{
    uint32_t data_start = 0;
    IMDStatus st = _read_comment(data_start);
    if (st != IMDStatus::Ok)
        return st;

    IMDCursor<File> cur(_file, _size);
    cur.seek(data_start);

    while (!cur.at_end())
    {
        uint32_t track_start = cur.pos();
        bool     header_ok = false;

        st = _parse_track(cur, header_ok);
        if (st == IMDStatus::Ok)
            continue;

        if (!header_ok && _tail_is_padding(track_start))
        {
            _trailing = _size - track_start;
            break;
        }
        return st;
    }

    if (_tracks.empty())
        return IMDStatus::NotIMD;

    for (size_t i = 0; i < _sectors.size(); i++)
    {
        uint16_t sz = _sectors[i].size;
        if (sz > _max_sector_size)
            _max_sector_size = sz;
        if (i > 0 && sz != _sectors[0].size)
            _uniform_size = false;
    }
    _scratch.assign(_max_sector_size, 0);
    return IMDStatus::Ok;
}

template <typename File>
IMDStatus IMDImage<File>::_parse_track(IMDCursor<File> &cur, bool &header_ok)
{
    header_ok = false;

    uint8_t hdr[5];
    if (!cur.read(hdr, sizeof(hdr)))
        return IMDStatus::Truncated;

    uint8_t  mode = hdr[0];
    uint8_t  cyl = hdr[1];
    uint8_t  head_raw = hdr[2];
    uint16_t nsec = hdr[3];
    uint8_t  size_code = hdr[4];

    if (!imd_mode_is_valid(mode) || nsec == 0)
        return IMDStatus::NotIMD;
    if (size_code > 6 && size_code != IMD_SIZE_CODE_VARIABLE)
        return IMDStatus::BadSizeCode;

    header_ok = true;

    uint8_t head = head_raw & IMD_HEAD_MASK;

    std::vector<uint8_t> smap(nsec), cmap, hmap;
    if (!cur.read(smap.data(), nsec))
        return IMDStatus::Truncated;

    // Cylinder map before head map, per IMD.TXT 6.5/6.6 (SIMH has these swapped)
    if (head_raw & IMD_HEAD_CYL_MAP)
    {
        cmap.resize(nsec);
        if (!cur.read(cmap.data(), nsec))
            return IMDStatus::Truncated;
    }
    if (head_raw & IMD_HEAD_HEAD_MAP)
    {
        hmap.resize(nsec);
        if (!cur.read(hmap.data(), nsec))
            return IMDStatus::Truncated;
    }

    std::vector<uint16_t> sizes;
    if (size_code == IMD_SIZE_CODE_VARIABLE)
    {
        std::vector<uint8_t> raw((size_t)nsec * 2);
        if (!cur.read(raw.data(), (uint32_t)nsec * 2))
            return IMDStatus::Truncated;
        sizes.resize(nsec);
        for (uint16_t i = 0; i < nsec; i++)
            sizes[i] = (uint16_t)(raw[i * 2] | (raw[i * 2 + 1] << 8));
    }

    if (_sectors.size() + nsec > IMD_MAX_SECTORS)
        return IMDStatus::TooLarge;

    TrackRef tr;
    tr.first_lba = (uint32_t)_sectors.size();
    tr.first_byte = _linear_size;
    tr.nsec = nsec;
    tr.mode = mode;
    tr.cyl = cyl;
    tr.head = head;
    tr.size_code = size_code;
    tr.map_flags = head_raw & (IMD_HEAD_CYL_MAP | IMD_HEAD_HEAD_MAP);

    size_t base = _sectors.size();

    for (uint16_t i = 0; i < nsec; i++)
    {
        uint8_t type;
        if (!cur.byte(type))
            return IMDStatus::Truncated;
        // Record length is derived from the type, so an unknown type is
        // unrecoverable - there is no way to find the next record.
        if (type > IMD_REC_MAX)
            return IMDStatus::BadRecordType;

        SectorRef s;
        s.size = (size_code == IMD_SIZE_CODE_VARIABLE) ? sizes[i] : (uint16_t)(128 << size_code);
        s.id = smap[i];
        s.id_cyl = cmap.empty() ? cyl : cmap[i];
        s.id_head = hmap.empty() ? head : hmap[i];
        s.rec_type = type;
        s.data_off = cur.pos();

        // Odd types carry a full sector, even types a single fill byte
        uint32_t payload = 0;
        if (type != IMD_REC_UNAVAILABLE)
            payload = (type & 1) ? s.size : 1;
        if (!cur.skip(payload))
            return IMDStatus::Truncated;

        _sectors.push_back(s);
        _linear_size += s.size;
    }

    // Sectors are stored in physical order; sorting by ID and indexing by
    // position deinterleaves the track and works for 0- and 1-based IDs alike.
    // Stable so duplicate IDs keep their on-disk order.
    std::stable_sort(_sectors.begin() + (long)base, _sectors.end(),
                     [](const SectorRef &a, const SectorRef &b) { return a.id < b.id; });

    _tracks.push_back(tr);
    return IMDStatus::Ok;
}

template <typename File>
uint16_t IMDImage<File>::sector_size(uint32_t lba) const
{
    if (lba >= _sectors.size())
        return 0;
    return _sectors[lba].size;
}

template <typename File>
IMDStatus IMDImage<File>::track_info(uint32_t track, IMDTrackInfo &out) const
{
    if (track >= _tracks.size())
        return IMDStatus::NoSuchSector;
    const TrackRef &t = _tracks[track];
    out.first_lba = t.first_lba;
    out.nsec = t.nsec;
    out.mode = t.mode;
    out.cyl = t.cyl;
    out.head = t.head;
    out.size_code = t.size_code;
    out.has_cyl_map = (t.map_flags & IMD_HEAD_CYL_MAP) != 0;
    out.has_head_map = (t.map_flags & IMD_HEAD_HEAD_MAP) != 0;
    return IMDStatus::Ok;
}

template <typename File>
IMDStatus IMDImage<File>::sector_info(uint32_t lba, IMDSectorInfo &out) const
{
    if (lba >= _sectors.size())
        return IMDStatus::NoSuchSector;
    const SectorRef &s = _sectors[lba];
    out.size = s.size;
    out.id = s.id;
    out.cyl = s.id_cyl;
    out.head = s.id_head;
    out.rec_type = s.rec_type;
    out.unavailable = (s.rec_type == IMD_REC_UNAVAILABLE);

    uint8_t f = out.unavailable ? 0 : (uint8_t)(s.rec_type - 1);
    out.compressed = (f & IMD_REC_COMPRESSED) != 0;
    out.deleted = (f & IMD_REC_DELETED) != 0;
    out.had_error = (f & IMD_REC_ERROR) != 0;
    return IMDStatus::Ok;
}

template <typename File>
IMDStatus IMDImage<File>::find_lba(uint8_t cyl, uint8_t head, uint8_t id, uint32_t &lba) const
{
    for (size_t t = 0; t < _tracks.size(); t++)
    {
        const TrackRef &tr = _tracks[t];
        if (tr.cyl != cyl || tr.head != head)
            continue;
        for (uint16_t i = 0; i < tr.nsec; i++)
        {
            if (_sectors[tr.first_lba + i].id == id)
            {
                lba = tr.first_lba + i;
                return IMDStatus::Ok;
            }
        }
    }
    return IMDStatus::NoSuchSector;
}

template <typename File>
IMDStatus IMDImage<File>::read_sector(uint32_t lba, uint8_t *buf, uint32_t buflen, uint16_t *out_len)
{
    if (out_len != nullptr)
        *out_len = 0;
    if (!is_open())
        return IMDStatus::IoError;
    if (lba >= _sectors.size())
        return IMDStatus::NoSuchSector;

    const SectorRef &s = _sectors[lba];
    // Never fabricate contents for a sector that was never readable
    if (s.rec_type == IMD_REC_UNAVAILABLE)
        return IMDStatus::Unavailable;
    if (buflen < s.size)
        return IMDStatus::BufferTooSmall;

    if (s.size > 0)
    {
        if (s.rec_type & 1)
        {
            if (!imd_read_bounded(_file, _size, s.data_off, buf, s.size))
                return IMDStatus::IoError;
        }
        else
        {
            uint8_t fill;
            if (!imd_read_bounded(_file, _size, s.data_off, &fill, 1))
                return IMDStatus::IoError;
            memset(buf, fill, s.size);
        }
    }

    if (out_len != nullptr)
        *out_len = s.size;
    return IMDStatus::Ok;
}

template <typename File>
IMDStatus IMDImage<File>::write_sector(uint32_t lba, const uint8_t *buf, uint16_t len)
{
    if (!is_open())
        return IMDStatus::IoError;
    if (!_writable)
        return IMDStatus::ReadOnly;
    if (lba >= _sectors.size())
        return IMDStatus::NoSuchSector;

    SectorRef &s = _sectors[lba];
    if (s.rec_type == IMD_REC_UNAVAILABLE)
        return IMDStatus::WriteRefused;
    if (len != s.size)
        return IMDStatus::WriteRefused;

    bool    deleted = ((s.rec_type - 1) & IMD_REC_DELETED) != 0;
    uint8_t new_type;

    if (s.rec_type & 1)
    {
        if (s.size > 0 && !imd_write_bounded(_file, _size, s.data_off, buf, s.size))
            return IMDStatus::IoError;
        new_type = (uint8_t)(1 + (deleted ? IMD_REC_DELETED : 0));
    }
    else
    {
        // A compressed record is one fill byte on disk, so only a uniform
        // buffer fits in place
        uint8_t fill = (s.size > 0) ? buf[0] : 0;
        for (uint16_t i = 1; i < s.size; i++)
            if (buf[i] != fill)
            {
                IMDStatus status = _expand();
                if (status != IMDStatus::Ok)
                    return status;
                return write_sector(lba, buf, len);
            }
        if (!imd_write_bounded(_file, _size, s.data_off, &fill, 1))
            return IMDStatus::IoError;
        new_type = (uint8_t)(2 + (deleted ? IMD_REC_DELETED : 0));
    }

    // Fresh data clears any recorded error, but keeps the deleted address mark
    if (new_type != s.rec_type)
    {
        if (!imd_write_bounded(_file, _size, s.data_off - 1, &new_type, 1))
            return IMDStatus::IoError;
        s.rec_type = new_type;
    }

    return imd_flush(*_file);
}

template <typename File>
IMDStatus IMDImage<File>::_locate_linear(uint32_t byte_off, uint32_t &lba, uint32_t &sec_off) const
{
    if (byte_off >= _linear_size || _tracks.empty())
        return IMDStatus::NoSuchSector;

    size_t lo = 0, hi = _tracks.size() - 1;
    while (lo < hi)
    {
        size_t mid = (lo + hi + 1) / 2;
        if (_tracks[mid].first_byte <= byte_off)
            lo = mid;
        else
            hi = mid - 1;
    }

    const TrackRef &t = _tracks[lo];
    uint32_t rel = byte_off - t.first_byte;

    if (t.size_code != IMD_SIZE_CODE_VARIABLE)
    {
        uint32_t ss = (uint32_t)(128 << t.size_code);
        lba = t.first_lba + rel / ss;
        sec_off = rel % ss;
        return lba < _sectors.size() ? IMDStatus::Ok : IMDStatus::NoSuchSector;
    }

    // Variable-size track: nsec is at most 255, so a scan is cheap enough
    for (uint16_t i = 0; i < t.nsec; i++)
    {
        uint32_t sz = _sectors[t.first_lba + i].size;
        if (rel < sz)
        {
            lba = t.first_lba + i;
            sec_off = rel;
            return IMDStatus::Ok;
        }
        rel -= sz;
    }
    return IMDStatus::NoSuchSector;
}

template <typename File>
IMDStatus IMDImage<File>::read_linear(uint32_t byte_off, uint8_t *buf, uint32_t len)
{
    if (!is_open())
        return IMDStatus::IoError;
    if (len == 0)
        return IMDStatus::Ok;
    if (byte_off > _linear_size || len > _linear_size - byte_off)
        return IMDStatus::NoSuchSector;

    uint32_t done = 0;

    while (done < len)
    {
        uint32_t lba, sec_off;
        if (_locate_linear(byte_off + done, lba, sec_off) != IMDStatus::Ok)
            return IMDStatus::NoSuchSector;

        uint16_t  ss = _sectors[lba].size;
        uint16_t  got = 0;
        IMDStatus st = read_sector(lba, _scratch.data(), ss, &got);
        if (st != IMDStatus::Ok)
            return st;

        uint32_t n = ss - sec_off;
        if (n > len - done)
            n = len - done;
        memcpy(buf + done, _scratch.data() + sec_off, n);
        done += n;
    }
    return IMDStatus::Ok;
}

template <typename File>
IMDStatus IMDImage<File>::_materialize(uint32_t lba, uint32_t sec_off, const uint8_t *src,
                                 uint32_t n, uint8_t *out)
{
    uint16_t ss = _sectors[lba].size;
    if (sec_off == 0 && n == ss)
    {
        memcpy(out, src, n);
        return IMDStatus::Ok;
    }

    // Partial overwrite needs the rest of the sector to decide what it becomes
    uint16_t  got = 0;
    IMDStatus st = read_sector(lba, out, ss, &got);
    if (st != IMDStatus::Ok)
        return st;
    memcpy(out + sec_off, src, n);
    return IMDStatus::Ok;
}

template <typename File>
IMDStatus IMDImage<File>::write_linear(uint32_t byte_off, const uint8_t *buf, uint32_t len)
{
    if (!is_open())
        return IMDStatus::IoError;
    if (!_writable)
        return IMDStatus::ReadOnly;
    if (len == 0)
        return IMDStatus::Ok;
    if (byte_off > _linear_size || len > _linear_size - byte_off)
        return IMDStatus::NoSuchSector;

    // Pass 0 validates every touched record so a refusal leaves the image intact
    for (int pass = 0; pass < 2; pass++)
    {
        uint32_t done = 0;
        while (done < len)
        {
            uint32_t lba, sec_off;
            if (_locate_linear(byte_off + done, lba, sec_off) != IMDStatus::Ok)
                return IMDStatus::NoSuchSector;

            uint16_t sec_size = _sectors[lba].size;
            uint32_t chunk = sec_size - sec_off;
            if (chunk > len - done)
                chunk = len - done;

            if (_sectors[lba].rec_type == IMD_REC_UNAVAILABLE)
                return IMDStatus::WriteRefused;

            if (pass == 1)
            {
                IMDStatus status = _materialize(lba, sec_off, buf + done, chunk, _scratch.data());
                if (status != IMDStatus::Ok)
                    return status;
                status = write_sector(lba, _scratch.data(), sec_size);
                if (status != IMDStatus::Ok)
                    return status;
            }
            done += chunk;
        }
    }
    return IMDStatus::Ok;
}

// Moves [from, end) up by shift, last chunk first so nothing is overwritten
// before it has been read.
template <typename File>
IMDStatus IMDImage<File>::_shift_up(uint32_t from, uint32_t end, uint32_t shift)
{
    uint8_t buf[512];

    if (shift == 0)
        return IMDStatus::Ok;
    while (end > from)
    {
        uint32_t chunk = end - from < sizeof(buf) ? end - from : (uint32_t)sizeof(buf);
        end -= chunk;
        if (imd_read_at(*_file, end, buf, chunk) != IMDStatus::Ok
            || imd_write_at(*_file, end + shift, buf, chunk) != IMDStatus::Ok)
            return IMDStatus::IoError;
    }
    return IMDStatus::Ok;
}

// Records are packed with no offsets, so growing one moves everything after
// it. Growing them all in one pass, from the end of the file back, costs one
// rewrite instead of one per sector, and every later write is in place.
template <typename File>
IMDStatus IMDImage<File>::_expand()
{
    std::vector<uint32_t> grow;   // LBAs of the compressed records, in file order
    for (uint32_t lba = 0; lba < _sectors.size(); lba++)
        if (_sectors[lba].rec_type != IMD_REC_UNAVAILABLE && (_sectors[lba].rec_type & 1) == 0
            && _sectors[lba].size > 1)
            grow.push_back(lba);
    std::sort(grow.begin(), grow.end(), [this](uint32_t lhs, uint32_t rhs) {
        return _sectors[lhs].data_off < _sectors[rhs].data_off;
    });

    // before[idx]: how far record grow[idx] and everything up to the next one move
    std::vector<uint32_t> before(grow.size());
    uint32_t total = 0;
    for (size_t idx = 0; idx < grow.size(); idx++)
    {
        before[idx] = total;
        total += _sectors[grow[idx]].size - 1u;
    }
    if (total == 0)
        return IMDStatus::Ok;
    if (_size > UINT32_MAX - total)
        return IMDStatus::TooLarge;

    uint8_t  buf[512];
    uint32_t end = _size;
    for (size_t idx = grow.size(); idx-- > 0;)
    {
        const SectorRef &sector = _sectors[grow[idx]];
        uint32_t shift = before[idx] + sector.size - 1u;
        uint8_t  fill;

        if (_shift_up(sector.data_off + 1, end, shift) != IMDStatus::Ok
            || imd_read_at(*_file, sector.data_off, &fill, 1) != IMDStatus::Ok)
            return IMDStatus::IoError;
        memset(buf, fill, sizeof(buf));
        for (uint32_t off = 0; off < sector.size; off += sizeof(buf))
        {
            uint32_t chunk = sector.size - off < sizeof(buf) ? sector.size - off
                                                              : (uint32_t)sizeof(buf);
            if (imd_write_at(*_file, sector.data_off + before[idx] + off, buf, chunk)
                != IMDStatus::Ok)
                return IMDStatus::IoError;
        }
        end = sector.data_off;
    }

    // Every record moves by the growth of the compressed records ahead of it
    std::vector<uint32_t> old_off(grow.size());
    for (size_t idx = 0; idx < grow.size(); idx++)
        old_off[idx] = _sectors[grow[idx]].data_off;
    for (SectorRef &sector : _sectors)
    {
        size_t idx = std::lower_bound(old_off.begin(), old_off.end(), sector.data_off)
                     - old_off.begin();
        sector.data_off += idx < grow.size() ? before[idx] : total;
    }
    _size += total;

    for (uint32_t lba : grow)
    {
        SectorRef &sector = _sectors[lba];
        uint8_t    full = (uint8_t)(sector.rec_type - 1);
        if (imd_write_at(*_file, sector.data_off - 1, &full, 1) != IMDStatus::Ok)
            return IMDStatus::IoError;
        sector.rec_type = full;
    }
    return imd_flush(*_file);
}

#endif // IMD_IMAGE_H
