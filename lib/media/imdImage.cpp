// IMD (ImageDisk) disk image reader/writer. See imdImage.h.
//
// No BUILD_* guard: the ESP build globs lib/media/*.cpp, so this is compiled
// into every target and must stay free of platform dependencies.

#include "imdImage.h"

#include <algorithm>
#include <cstring>

#include "debug.h"

// Bounds-checked absolute-offset access, so the range checks live in one place.
static bool imd_read_at(fnFile *f, uint32_t size, uint32_t off, void *dst, uint32_t len)
{
    if (len == 0)
        return true;
    if (off > size || len > size - off)
        return false;
    if (fnio::fseek(f, (long)off, SEEK_SET) != 0)
        return false;
    return fnio::fread(dst, 1, len, f) == len;
}

static bool imd_write_at(fnFile *f, uint32_t size, uint32_t off, const void *src, uint32_t len)
{
    if (len == 0)
        return true;
    if (off > size || len > size - off)
        return false;
    if (fnio::fseek(f, (long)off, SEEK_SET) != 0)
        return false;
    return fnio::fwrite(src, 1, len, f) == len;
}

// Buffered forward reader. Indexing an 8" image walks ~2000 sector records; one
// read per record would make mounting crawl on SD/TNFS.
class IMDCursor
{
public:
    IMDCursor(fnFile *f, uint32_t size) : _f(f), _size(size) {}

    uint32_t pos() const { return _pos; }
    uint32_t remaining() const { return _pos < _size ? _size - _pos : 0; }
    bool     at_end() const { return _pos >= _size; }
    void     seek(uint32_t off) { _pos = off; }

    bool read(void *dst, uint32_t len)
    {
        uint8_t *out = (uint8_t *)dst;
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
        if (!imd_read_at(_f, _size, _pos, _buf, n))
        {
            _buf_len = 0;
            return false;
        }
        _buf_off = _pos;
        _buf_len = n;
        return true;
    }

    fnFile  *_f;
    uint32_t _size;
    uint32_t _pos = 0;
    uint32_t _buf_off = 0;
    uint32_t _buf_len = 0;
    uint8_t  _buf[512];
};

const char *imd_status_str(IMDStatus s)
{
    switch (s)
    {
    case IMDStatus::Ok:             return "Ok";
    case IMDStatus::NotIMD:         return "NotIMD";
    case IMDStatus::Truncated:      return "Truncated";
    case IMDStatus::BadRecordType:  return "BadRecordType";
    case IMDStatus::BadSizeCode:    return "BadSizeCode";
    case IMDStatus::NoSuchSector:   return "NoSuchSector";
    case IMDStatus::Unavailable:    return "Unavailable";
    case IMDStatus::BufferTooSmall: return "BufferTooSmall";
    case IMDStatus::WriteRefused:   return "WriteRefused";
    case IMDStatus::ReadOnly:       return "ReadOnly";
    case IMDStatus::IoError:        return "IoError";
    case IMDStatus::TooLarge:       return "TooLarge";
    }
    return "?";
}

uint16_t imd_mode_rate_kbps(uint8_t mode)
{
    switch (mode)
    {
    case 0: case 3: return 500;
    case 1: case 4: return 300;
    case 2: case 5: return 250;
    case 6: case 9: return 1000; // libdsk extension
    default:        return 0;
    }
}

bool imd_mode_is_mfm(uint8_t mode)
{
    return (mode >= 3 && mode <= 5) || mode == 9;
}

bool imd_mode_is_valid(uint8_t mode)
{
    return mode <= 6 || mode == 9;
}

IMDImage::~IMDImage()
{
    close();
}

bool IMDImage::looks_like_imd_extension(const char *filename)
{
    if (filename == nullptr)
        return false;
    size_t l = strlen(filename);
    return l > 4 && filename[l - 4] == '.' && strcasecmp(filename + l - 3, "IMD") == 0;
}

IMDStatus IMDImage::open(fnFile *f, uint32_t disksize, bool writable)
{
    close();
    if (f == nullptr)
        return IMDStatus::IoError;

    _f = f;
    _size = disksize;
    _writable = writable;

    IMDStatus st = _parse();
    if (st != IMDStatus::Ok)
        close();
    return st;
}

void IMDImage::close()
{
    _sectors.clear();
    _sectors.shrink_to_fit();
    _tracks.clear();
    _tracks.shrink_to_fit();
    _scratch.clear();
    _scratch.shrink_to_fit();
    _comment.clear();
    _f = nullptr;   // borrowed, never ours to close
    _size = 0;
    _writable = false;
    _trailing = 0;
    _linear_size = 0;
    _max_sector_size = 0;
    _uniform_size = true;
}

// Everything before the 0x1A is header+comment. The "IMD v.vv:" signature line
// is optional - SIMH omits it entirely - so only the terminator is required.
IMDStatus IMDImage::_read_comment(uint32_t &data_start)
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
        if (!imd_read_at(_f, _size, off, buf, n))
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
bool IMDImage::_tail_is_padding(uint32_t from)
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
        if (!imd_read_at(_f, _size, off, buf, n))
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

IMDStatus IMDImage::_parse()
{
    uint32_t data_start = 0;
    IMDStatus st = _read_comment(data_start);
    if (st != IMDStatus::Ok)
        return st;

    IMDCursor cur(_f, _size);
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
            Debug_printf("IMD: ignoring %lu trailing bytes\n", (unsigned long)_trailing);
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

    Debug_printf("IMD: %u tracks, %u sectors, %lu linear bytes\n",
                 (unsigned)_tracks.size(), (unsigned)_sectors.size(),
                 (unsigned long)_linear_size);
    return IMDStatus::Ok;
}

IMDStatus IMDImage::_parse_track(IMDCursor &cur, bool &header_ok)
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
    if (head > 1)
    {
        Debug_printf("IMD: cyl %u head %u is out of spec\n", (unsigned)cyl, (unsigned)head);
    }

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

uint16_t IMDImage::sector_size(uint32_t lba) const
{
    if (lba >= _sectors.size())
        return 0;
    return _sectors[lba].size;
}

bool IMDImage::track_info(uint32_t track, IMDTrackInfo &out) const
{
    if (track >= _tracks.size())
        return false;
    const TrackRef &t = _tracks[track];
    out.first_lba = t.first_lba;
    out.nsec = t.nsec;
    out.mode = t.mode;
    out.cyl = t.cyl;
    out.head = t.head;
    out.size_code = t.size_code;
    out.has_cyl_map = (t.map_flags & IMD_HEAD_CYL_MAP) != 0;
    out.has_head_map = (t.map_flags & IMD_HEAD_HEAD_MAP) != 0;
    return true;
}

bool IMDImage::sector_info(uint32_t lba, IMDSectorInfo &out) const
{
    if (lba >= _sectors.size())
        return false;
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
    return true;
}

bool IMDImage::find_lba(uint8_t cyl, uint8_t head, uint8_t id, uint32_t &lba) const
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
                return true;
            }
        }
    }
    return false;
}

IMDStatus IMDImage::read_sector(uint32_t lba, uint8_t *buf, uint32_t buflen, uint16_t *out_len)
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
            if (!imd_read_at(_f, _size, s.data_off, buf, s.size))
                return IMDStatus::IoError;
        }
        else
        {
            uint8_t fill;
            if (!imd_read_at(_f, _size, s.data_off, &fill, 1))
                return IMDStatus::IoError;
            memset(buf, fill, s.size);
        }
    }

    if (out_len != nullptr)
        *out_len = s.size;
    return IMDStatus::Ok;
}

IMDStatus IMDImage::write_sector(uint32_t lba, const uint8_t *buf, uint16_t len)
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
        if (s.size > 0 && !imd_write_at(_f, _size, s.data_off, buf, s.size))
            return IMDStatus::IoError;
        new_type = (uint8_t)(1 + (deleted ? IMD_REC_DELETED : 0));
    }
    else
    {
        // A compressed record is one byte on disk, so it can only take a
        // uniform buffer; anything else would change the record's length.
        uint8_t fill = (s.size > 0) ? buf[0] : 0;
        for (uint16_t i = 1; i < s.size; i++)
            if (buf[i] != fill)
                return IMDStatus::WriteRefused;
        if (!imd_write_at(_f, _size, s.data_off, &fill, 1))
            return IMDStatus::IoError;
        new_type = (uint8_t)(2 + (deleted ? IMD_REC_DELETED : 0));
    }

    // Fresh data clears any recorded error, but keeps the deleted address mark
    if (new_type != s.rec_type)
    {
        if (!imd_write_at(_f, _size, s.data_off - 1, &new_type, 1))
            return IMDStatus::IoError;
        s.rec_type = new_type;
    }

    fnio::fflush(_f);
    return IMDStatus::Ok;
}

bool IMDImage::_locate_linear(uint32_t byte_off, uint32_t &lba, uint32_t &sec_off) const
{
    if (byte_off >= _linear_size || _tracks.empty())
        return false;

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
        return lba < _sectors.size();
    }

    // Variable-size track: nsec is at most 255, so a scan is cheap enough
    for (uint16_t i = 0; i < t.nsec; i++)
    {
        uint32_t sz = _sectors[t.first_lba + i].size;
        if (rel < sz)
        {
            lba = t.first_lba + i;
            sec_off = rel;
            return true;
        }
        rel -= sz;
    }
    return false;
}

IMDStatus IMDImage::read_linear(uint32_t byte_off, uint8_t *buf, uint32_t len)
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
        if (!_locate_linear(byte_off + done, lba, sec_off))
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

IMDStatus IMDImage::_materialize(uint32_t lba, uint32_t sec_off, const uint8_t *src,
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

IMDStatus IMDImage::write_linear(uint32_t byte_off, const uint8_t *buf, uint32_t len)
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
            if (!_locate_linear(byte_off + done, lba, sec_off))
                return IMDStatus::NoSuchSector;

            uint16_t ss = _sectors[lba].size;
            uint32_t n = ss - sec_off;
            if (n > len - done)
                n = len - done;

            if (_sectors[lba].rec_type == IMD_REC_UNAVAILABLE)
                return IMDStatus::WriteRefused;

            IMDStatus st = _materialize(lba, sec_off, buf + done, n, _scratch.data());
            if (st != IMDStatus::Ok)
                return st;

            if (pass == 0)
            {
                if ((_sectors[lba].rec_type & 1) == 0)
                {
                    for (uint16_t i = 1; i < ss; i++)
                        if (_scratch[i] != _scratch[0])
                            return IMDStatus::WriteRefused;
                }
            }
            else
            {
                st = write_sector(lba, _scratch.data(), ss);
                if (st != IMDStatus::Ok)
                    return st;
            }
            done += n;
        }
    }
    return IMDStatus::Ok;
}
