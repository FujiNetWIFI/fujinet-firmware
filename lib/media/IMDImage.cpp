// IMD (ImageDisk) image: everything that doesn't need the file handle's type. See
// IMDImage.h.

#include "IMDImage.h"

#include <algorithm>
#include <cctype>
#include <cstring>

const char *IMDImage::statusString(IMDStatus status)
{
    switch (status)
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

uint16_t IMDImage::modeRateKBPS(uint8_t mode)
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

bool IMDImage::modeIsMFM(uint8_t mode)
{
    return (mode >= 3 && mode <= 5) || mode == 9;
}

bool IMDImage::modeIsValid(uint8_t mode)
{
    return mode <= 6 || mode == 9;
}

bool IMDImage::looksLikeExtension(const char *filename)
{
    if (filename == nullptr)
        return false;
    size_t len = strlen(filename);
    if (len <= 4 || filename[len - 4] != '.')
        return false;
    for (size_t idx = 0; idx < 3; idx++)
        if (std::toupper((unsigned char)filename[len - 3 + idx]) != "IMD"[idx])
            return false;
    return true;
}

// ---------------------------------------------------------------------------
// File access

bool IMDImage::_readAt(uint32_t off, void *dst, uint32_t len)
{
    if (len == 0)
        return true;
    if (off > _size || len > _size - off)
        return false;
    return _file.seek(off) && _file.read(dst, len);
}

bool IMDImage::_writeAt(uint32_t off, const void *src, uint32_t len)
{
    if (len == 0)
        return true;
    return _file.seek(off) && _file.write(src, len);
}

bool IMDImage::_append(const void *src, uint32_t len)
{
    return _file.file != nullptr && _file.seek(_file.end()) && _file.write(src, len);
}

// ---------------------------------------------------------------------------
// Parsing

// Buffered forward reader: indexing an 8" disk walks about 2000 records, and one
// read per record would make a mount over the network crawl.
class IMDImage::Cursor
{
public:
    explicit Cursor(IMDImage &image) : _image(image) {}

    uint32_t pos() const { return _pos; }
    bool     atEnd() const { return _pos >= _image._size; }
    void     seek(uint32_t off) { _pos = off; }

    bool read(void *dst, uint32_t len)
    {
        uint8_t *out = static_cast<uint8_t *>(dst);
        while (len > 0)
        {
            if (!_fill())
                return false;
            uint32_t used = _pos - _buf_off;
            uint32_t chunk = std::min(len, _buf_len - used);
            memcpy(out, _buf + used, chunk);
            out += chunk;
            _pos += chunk;
            len -= chunk;
        }
        return true;
    }

    bool skip(uint32_t len)
    {
        if (len > _image._size - std::min(_pos, _image._size))
            return false;
        _pos += len;
        return true;
    }

private:
    bool _fill()
    {
        if (_pos >= _image._size)
            return false;
        if (_buf_len > 0 && _pos >= _buf_off && _pos < _buf_off + _buf_len)
            return true;
        uint32_t chunk = std::min<uint32_t>(_image._size - _pos, sizeof(_buf));
        if (!_image._readAt(_pos, _buf, chunk))
        {
            _buf_len = 0;
            return false;
        }
        _buf_off = _pos;
        _buf_len = chunk;
        return true;
    }

    IMDImage   &_image;
    uint32_t _pos = 0;
    uint32_t _buf_off = 0;
    uint32_t _buf_len = 0;
    uint8_t  _buf[512];
};

IMDStatus IMDImage::open(bool writable)
{
    close();
    if (_file.file == nullptr)
        return IMDStatus::IoError;

    _size = _file.end();
    _writable = writable;
    IMDStatus status = _parse();
    if (status != IMDStatus::Ok)
    {
        close();
        return status;
    }
    _open = true;
    return IMDStatus::Ok;
}

void IMDImage::close()
{
    _sectors.clear();
    _sectors.shrink_to_fit();
    _tracks.clear();
    _tracks.shrink_to_fit();
    _comment.clear();
    _scratch.clear();
    _scratch.shrink_to_fit();
    _open = false;
    _writable = false;
    _size = 0;
    _trailing = 0;
    _linear_size = 0;
    _max_sector_size = 0;
    _uniform_size = true;
}

IMDStatus IMDImage::probe()
{
    if (_file.file == nullptr)
        return IMDStatus::IoError;
    IMDStatus status = _probeHeader();
    return _file.seek(0) ? status : IMDStatus::IoError;
}

IMDStatus IMDImage::_probeHeader()
{
    uint32_t size = _file.end();
    uint32_t limit = std::min<uint32_t>(size, IMD_MAX_HEADER_SCAN);
    uint8_t  buf[128];

    for (uint32_t off = 0; off < limit;)
    {
        uint32_t chunk = std::min<uint32_t>(limit - off, sizeof(buf));
        if (!_file.seek(off) || !_file.read(buf, chunk))
            return IMDStatus::IoError;
        if (off == 0 && (chunk < 4 || memcmp(buf, "IMD ", 4) != 0))
            return IMDStatus::NotIMD;

        for (uint32_t idx = 0; idx < chunk; idx++)
        {
            if (buf[idx] != IMD_EOF_MARKER)
                continue;
            uint8_t  header[5];
            uint32_t track = off + idx + 1;
            if (size - track < sizeof(header))
                return IMDStatus::NotIMD;
            if (!_file.seek(track) || !_file.read(header, sizeof(header)))
                return IMDStatus::IoError;
            bool valid = modeIsValid(header[0]) && (header[2] & IMD_HEAD_MASK) <= 1
                         && header[3] != 0
                         && (header[4] <= 6 || header[4] == IMD_SIZE_CODE_VARIABLE);
            return valid ? IMDStatus::Ok : IMDStatus::NotIMD;
        }
        off += chunk;
    }
    return IMDStatus::NotIMD;
}

// Everything before IMD_EOF_MARKER is the signature and comment. The "IMD v.vv:" line
// is optional (SIMH omits it), so only the terminator is required.
IMDStatus IMDImage::_readComment(uint32_t &data_start)
{
    uint32_t limit = std::min<uint32_t>(_size, IMD_MAX_HEADER_SCAN);
    uint8_t  buf[128];

    for (uint32_t off = 0; off < limit;)
    {
        uint32_t chunk = std::min<uint32_t>(limit - off, sizeof(buf));
        if (!_readAt(off, buf, chunk))
            return IMDStatus::IoError;
        for (uint32_t idx = 0; idx < chunk; idx++)
        {
            if (buf[idx] == IMD_EOF_MARKER)
            {
                data_start = off + idx + 1;
                if (_comment.compare(0, 4, "IMD ") == 0)
                {
                    size_t newline = _comment.find('\n');
                    _comment.erase(0, newline == std::string::npos ? _comment.size()
                                                                   : newline + 1);
                }
                return IMDStatus::Ok;
            }
            if (_comment.size() < IMD_MAX_COMMENT)
                _comment.push_back((char)buf[idx]);
        }
        off += chunk;
    }
    return IMDStatus::NotIMD;
}

// True if everything from `from` to the end is one repeated byte: transport padding
bool IMDImage::_tailIsPadding(uint32_t from)
{
    uint8_t buf[128];
    uint8_t first = 0;
    bool    have_first = false;

    for (uint32_t off = from; off < _size;)
    {
        uint32_t chunk = std::min<uint32_t>(_size - off, sizeof(buf));
        if (!_readAt(off, buf, chunk))
            return false;
        for (uint32_t idx = 0; idx < chunk; idx++)
        {
            if (!have_first)
            {
                first = buf[idx];
                have_first = true;
            }
            else if (buf[idx] != first)
                return false;
        }
        off += chunk;
    }
    return true;
}

IMDStatus IMDImage::_parse()
{
    uint32_t data_start = 0;
    IMDStatus   status = _readComment(data_start);
    if (status != IMDStatus::Ok)
        return status;

    Cursor cursor(*this);
    cursor.seek(data_start);
    while (!cursor.atEnd())
    {
        uint32_t track_start = cursor.pos();
        bool     header_ok = false;

        status = _parseTrack(cursor, header_ok);
        if (status == IMDStatus::Ok)
            continue;
        if (!header_ok && _tailIsPadding(track_start))
        {
            _trailing = _size - track_start;
            break;
        }
        return status;
    }

    if (_tracks.empty())
        return IMDStatus::NotIMD;

    for (const SectorRef &sector : _sectors)
    {
        _max_sector_size = std::max(_max_sector_size, sector.size);
        if (sector.size != _sectors[0].size)
            _uniform_size = false;
    }
    _scratch.assign(_max_sector_size, 0);
    return IMDStatus::Ok;
}

IMDStatus IMDImage::_parseTrack(Cursor &cursor, bool &header_ok)
{
    header_ok = false;

    uint8_t header[5];
    if (!cursor.read(header, sizeof(header)))
        return IMDStatus::Truncated;

    uint8_t  mode = header[0];
    uint8_t  cyl = header[1];
    uint8_t  head_byte = header[2];
    uint16_t nsec = header[3];
    uint8_t  size_code = header[4];

    if (!modeIsValid(mode) || nsec == 0)
        return IMDStatus::NotIMD;
    if (size_code > 6 && size_code != IMD_SIZE_CODE_VARIABLE)
        return IMDStatus::BadSizeCode;
    header_ok = true;

    uint8_t head = head_byte & IMD_HEAD_MASK;

    std::vector<uint8_t> id_map(nsec), cyl_map, head_map;
    if (!cursor.read(id_map.data(), nsec))
        return IMDStatus::Truncated;
    // Cylinder map before head map, per IMD.TXT 6.5/6.6 (SIMH has them swapped)
    if (head_byte & IMD_HEAD_CYL_MAP)
    {
        cyl_map.resize(nsec);
        if (!cursor.read(cyl_map.data(), nsec))
            return IMDStatus::Truncated;
    }
    if (head_byte & IMD_HEAD_HEAD_MAP)
    {
        head_map.resize(nsec);
        if (!cursor.read(head_map.data(), nsec))
            return IMDStatus::Truncated;
    }

    std::vector<uint16_t> sizes;
    if (size_code == IMD_SIZE_CODE_VARIABLE)
    {
        std::vector<uint8_t> raw((size_t)nsec * 2);
        if (!cursor.read(raw.data(), (uint32_t)raw.size()))
            return IMDStatus::Truncated;
        sizes.resize(nsec);
        for (uint16_t idx = 0; idx < nsec; idx++)
            sizes[idx] = (uint16_t)(raw[idx * 2] | (raw[idx * 2 + 1] << 8));
    }

    if (_sectors.size() + nsec > IMD_MAX_SECTORS)
        return IMDStatus::TooLarge;

    TrackRef track;
    track.first_lba = (uint32_t)_sectors.size();
    track.first_byte = _linear_size;
    track.nsec = nsec;
    track.mode = mode;
    track.cyl = cyl;
    track.head = head;
    track.size_code = size_code;
    track.map_flags = head_byte & (IMD_HEAD_CYL_MAP | IMD_HEAD_HEAD_MAP);

    for (uint16_t idx = 0; idx < nsec; idx++)
    {
        uint8_t rec_type;
        if (!cursor.read(&rec_type, 1))
            return IMDStatus::Truncated;
        // A record's length comes from its type, so an unknown type loses the place
        if (rec_type > IMD_REC_MAX)
            return IMDStatus::BadRecordType;

        SectorRef sector;
        sector.size = size_code == IMD_SIZE_CODE_VARIABLE ? sizes[idx]
                                                          : (uint16_t)(128 << size_code);
        sector.sector_id = id_map[idx];
        sector.id_cyl = cyl_map.empty() ? cyl : cyl_map[idx];
        sector.id_head = head_map.empty() ? head : head_map[idx];
        sector.rec_type = rec_type;
        sector.data_off = cursor.pos();

        uint32_t payload = 0;
        if (rec_type != IMD_REC_UNAVAILABLE)
            payload = (rec_type & 1) ? sector.size : 1;
        if (!cursor.skip(payload))
            return IMDStatus::Truncated;
        _sectors.push_back(sector);
        _linear_size += sector.size;
    }

    // Stored in physical order; sorting by ID deinterleaves the track for 0- and
    // 1-based IDs alike. Stable so duplicate IDs keep their order.
    std::stable_sort(_sectors.begin() + track.first_lba, _sectors.end(),
                     [](const SectorRef &lhs, const SectorRef &rhs) {
                         return lhs.sector_id < rhs.sector_id;
                     });
    _tracks.push_back(track);
    return IMDStatus::Ok;
}

// ---------------------------------------------------------------------------
// Queries

uint16_t IMDImage::sectorSize(uint32_t lba) const
{
    return lba < _sectors.size() ? _sectors[lba].size : 0;
}

IMDStatus IMDImage::trackInfo(uint32_t track, IMDTrackInfo &info) const
{
    if (track >= _tracks.size())
        return IMDStatus::NoSuchSector;
    const TrackRef &ref = _tracks[track];
    info.first_lba = ref.first_lba;
    info.nsec = ref.nsec;
    info.mode = ref.mode;
    info.cyl = ref.cyl;
    info.head = ref.head;
    info.size_code = ref.size_code;
    info.has_cyl_map = (ref.map_flags & IMD_HEAD_CYL_MAP) != 0;
    info.has_head_map = (ref.map_flags & IMD_HEAD_HEAD_MAP) != 0;
    return IMDStatus::Ok;
}

IMDStatus IMDImage::sectorInfo(uint32_t lba, IMDSectorInfo &info) const
{
    if (lba >= _sectors.size())
        return IMDStatus::NoSuchSector;
    const SectorRef &ref = _sectors[lba];
    info.size = ref.size;
    info.id = ref.sector_id;
    info.cyl = ref.id_cyl;
    info.head = ref.id_head;
    info.rec_type = ref.rec_type;
    info.unavailable = ref.rec_type == IMD_REC_UNAVAILABLE;

    uint8_t flags = info.unavailable ? 0 : (uint8_t)(ref.rec_type - 1);
    info.compressed = (flags & IMD_REC_COMPRESSED) != 0;
    info.deleted = (flags & IMD_REC_DELETED) != 0;
    info.had_error = (flags & IMD_REC_ERROR) != 0;
    return IMDStatus::Ok;
}

IMDStatus IMDImage::findLBA(uint8_t cyl, uint8_t head, uint8_t sector_id,
                            uint32_t &lba) const
{
    for (const TrackRef &track : _tracks)
    {
        if (track.cyl != cyl || track.head != head)
            continue;
        for (uint16_t idx = 0; idx < track.nsec; idx++)
        {
            if (_sectors[track.first_lba + idx].sector_id == sector_id)
            {
                lba = track.first_lba + idx;
                return IMDStatus::Ok;
            }
        }
    }
    return IMDStatus::NoSuchSector;
}

// ---------------------------------------------------------------------------
// Sector I/O

IMDStatus IMDImage::readSector(uint32_t lba, uint8_t *buf, uint32_t buf_len,
                               uint16_t *out_len)
{
    if (out_len != nullptr)
        *out_len = 0;
    if (!_open)
        return IMDStatus::IoError;
    if (lba >= _sectors.size())
        return IMDStatus::NoSuchSector;

    const SectorRef &sector = _sectors[lba];
    // Never invent contents for a sector that was never readable
    if (sector.rec_type == IMD_REC_UNAVAILABLE)
        return IMDStatus::Unavailable;
    if (buf_len < sector.size)
        return IMDStatus::BufferTooSmall;

    if (sector.rec_type & 1)
    {
        if (!_readAt(sector.data_off, buf, sector.size))
            return IMDStatus::IoError;
    }
    else
    {
        uint8_t fill;
        if (!_readAt(sector.data_off, &fill, 1))
            return IMDStatus::IoError;
        memset(buf, fill, sector.size);
    }

    if (out_len != nullptr)
        *out_len = sector.size;
    return IMDStatus::Ok;
}

IMDStatus IMDImage::writeSector(uint32_t lba, const uint8_t *buf, uint16_t len)
{
    if (!_open)
        return IMDStatus::IoError;
    if (!_writable)
        return IMDStatus::ReadOnly;
    if (lba >= _sectors.size())
        return IMDStatus::NoSuchSector;

    SectorRef &sector = _sectors[lba];
    if (sector.rec_type == IMD_REC_UNAVAILABLE || len != sector.size)
        return IMDStatus::WriteRefused;

    bool deleted = ((sector.rec_type - 1) & IMD_REC_DELETED) != 0;
    uint8_t new_type;

    if (sector.rec_type & 1)
    {
        if (!_writeAt(sector.data_off, buf, sector.size))
            return IMDStatus::IoError;
        new_type = (uint8_t)(1 + (deleted ? IMD_REC_DELETED : 0));
    }
    else
    {
        // A compressed record is one fill byte, so only a uniform buffer fits in place
        uint8_t fill = len > 0 ? buf[0] : 0;
        for (uint16_t idx = 1; idx < len; idx++)
        {
            if (buf[idx] != fill)
            {
                IMDStatus status = _expand();
                if (status != IMDStatus::Ok)
                    return status;
                return writeSector(lba, buf, len);
            }
        }
        if (!_writeAt(sector.data_off, &fill, 1))
            return IMDStatus::IoError;
        new_type = (uint8_t)(2 + (deleted ? IMD_REC_DELETED : 0));
    }

    // Fresh data clears a recorded error but keeps the deleted address mark
    if (new_type != sector.rec_type)
    {
        if (!_writeAt(sector.data_off - 1, &new_type, 1))
            return IMDStatus::IoError;
        sector.rec_type = new_type;
    }
    return _file.flush() ? IMDStatus::Ok : IMDStatus::IoError;
}

// Moves [from, end) up by shift, last chunk first so nothing is overwritten before
// it has been read
IMDStatus IMDImage::_shiftUp(uint32_t from, uint32_t end, uint32_t shift)
{
    uint8_t buf[512];

    if (shift == 0)
        return IMDStatus::Ok;
    while (end > from)
    {
        uint32_t chunk = std::min<uint32_t>(end - from, sizeof(buf));
        end -= chunk;
        if (!_readAt(end, buf, chunk) || !_writeAt(end + shift, buf, chunk))
            return IMDStatus::IoError;
    }
    return IMDStatus::Ok;
}

// Records are packed with no offsets, so growing one moves everything after it.
// Growing them all in one pass, from the end back, costs one rewrite instead of one
// per sector, and every later write is in place.
IMDStatus IMDImage::_expand()
{
    std::vector<uint32_t> grow;   // LBAs of the compressed records, in file order
    for (uint32_t lba = 0; lba < _sectors.size(); lba++)
    {
        const SectorRef &sector = _sectors[lba];
        if (sector.rec_type != IMD_REC_UNAVAILABLE && (sector.rec_type & 1) == 0
            && sector.size > 1)
            grow.push_back(lba);
    }
    std::sort(grow.begin(), grow.end(), [this](uint32_t lhs, uint32_t rhs) {
        return _sectors[lhs].data_off < _sectors[rhs].data_off;
    });

    // before[idx]: how far record grow[idx], and everything up to the next one, moves
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
        uint8_t fill;

        if (_shiftUp(sector.data_off + 1, end, before[idx] + sector.size - 1u) != IMDStatus::Ok
            || !_readAt(sector.data_off, &fill, 1))
            return IMDStatus::IoError;
        memset(buf, fill, sizeof(buf));
        for (uint32_t off = 0; off < sector.size; off += sizeof(buf))
        {
            uint32_t chunk = std::min<uint32_t>(sector.size - off, sizeof(buf));
            if (!_writeAt(sector.data_off + before[idx] + off, buf, chunk))
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
        if (!_writeAt(sector.data_off - 1, &full, 1))
            return IMDStatus::IoError;
        sector.rec_type = full;
    }
    return _file.flush() ? IMDStatus::Ok : IMDStatus::IoError;
}

// ---------------------------------------------------------------------------
// Building a new image

IMDStatus IMDImage::writeHeader(time_t when, const char *comment)
{
    // A signature after existing tracks would stop the image parsing
    if (_file.file == nullptr || _file.end() != 0)
        return IMDStatus::WriteRefused;

    char header[48];
    size_t len = strftime(header, sizeof(header), "IMD 1.18: %d/%m/%Y %H:%M:%S\r\n",
                          localtime(&when));
    std::vector<uint8_t> out(header, header + len);
    if (comment != nullptr)
        out.insert(out.end(), comment, comment + strlen(comment));
    out.push_back(IMD_EOF_MARKER);
    if (!_append(out.data(), (uint32_t)out.size()))
        return IMDStatus::IoError;
    return IMDStatus::Ok;
}

IMDStatus IMDImage::writeTrack(uint8_t mode, uint8_t cyl, uint8_t head,
                               const std::vector<IMDSectorSpec> &sectors)
{
    if (sectors.empty() || sectors.size() > 255 || !modeIsValid(mode))
        return IMDStatus::WriteRefused;

    uint8_t size_code = IMD_SIZE_CODE_VARIABLE;
    for (uint8_t code = 0; code <= 6; code++)
    {
        if (std::all_of(sectors.begin(), sectors.end(),
                        [code](const IMDSectorSpec &spec) {
                            return spec.size == (128u << code);
                        }))
            size_code = code;
    }

    std::vector<uint8_t> out = {mode, cyl, (uint8_t)(head & IMD_HEAD_MASK),
                                (uint8_t)sectors.size(),
                                size_code};
    for (const IMDSectorSpec &spec : sectors)
        out.push_back(spec.sector_id);
    if (size_code == IMD_SIZE_CODE_VARIABLE)
    {
        for (const IMDSectorSpec &spec : sectors)
        {
            out.push_back((uint8_t)(spec.size & 0xFF));
            out.push_back((uint8_t)(spec.size >> 8));
        }
    }
    for (const IMDSectorSpec &spec : sectors)
    {
        if (spec.data == nullptr)
        {
            out.push_back(IMD_REC_UNAVAILABLE);
            continue;
        }
        out.push_back(1);   // a full sector of data follows
        out.insert(out.end(), spec.data, spec.data + spec.size);
    }
    if (!_append(out.data(), (uint32_t)out.size()))
        return IMDStatus::IoError;
    return IMDStatus::Ok;
}

IMDStatus IMDImage::writeBlankTrack(uint8_t mode, uint8_t cyl, uint8_t head, uint8_t nsec,
                                    uint8_t size_code, uint8_t fill)
{
    if (size_code > 6)
        return IMDStatus::BadSizeCode;
    std::vector<uint8_t> data(128u << size_code, fill);
    std::vector<IMDSectorSpec> sectors;
    for (uint8_t sector_id = 1; sector_id <= nsec; sector_id++)
        sectors.push_back({sector_id, (uint16_t)data.size(), data.data()});
    return writeTrack(mode, cyl, head, sectors);
}

// ---------------------------------------------------------------------------
// Linear byte view

IMDStatus IMDImage::_locateLinear(uint32_t byte_off, uint32_t &lba,
                                  uint32_t &sec_off) const
{
    if (byte_off >= _linear_size || _tracks.empty())
        return IMDStatus::NoSuchSector;

    // The last track starting at or before byte_off
    auto after = std::upper_bound(_tracks.begin(), _tracks.end(), byte_off,
                                  [](uint32_t off, const TrackRef &track) {
                                      return off < track.first_byte;
                                  });
    const TrackRef &track = *(after - 1);
    uint32_t rel = byte_off - track.first_byte;

    if (track.size_code != IMD_SIZE_CODE_VARIABLE)
    {
        uint32_t sec_size = 128u << track.size_code;
        lba = track.first_lba + rel / sec_size;
        sec_off = rel % sec_size;
        return lba < _sectors.size() ? IMDStatus::Ok : IMDStatus::NoSuchSector;
    }

    // A variable-size track has at most 255 sectors, so a scan is cheap
    for (uint16_t idx = 0; idx < track.nsec; idx++)
    {
        uint32_t sec_size = _sectors[track.first_lba + idx].size;
        if (rel < sec_size)
        {
            lba = track.first_lba + idx;
            sec_off = rel;
            return IMDStatus::Ok;
        }
        rel -= sec_size;
    }
    return IMDStatus::NoSuchSector;
}

IMDStatus IMDImage::readLinear(uint32_t byte_off, uint8_t *buf, uint32_t len)
{
    if (!_open)
        return IMDStatus::IoError;
    if (byte_off > _linear_size || len > _linear_size - byte_off)
        return IMDStatus::NoSuchSector;

    for (uint32_t done = 0; done < len;)
    {
        uint32_t lba, sec_off;
        if (_locateLinear(byte_off + done, lba, sec_off) != IMDStatus::Ok)
            return IMDStatus::NoSuchSector;
        uint16_t  sec_size = _sectors[lba].size;
        IMDStatus status = readSector(lba, _scratch.data(), sec_size, nullptr);
        if (status != IMDStatus::Ok)
            return status;
        uint32_t chunk = std::min(sec_size - sec_off, len - done);
        memcpy(buf + done, _scratch.data() + sec_off, chunk);
        done += chunk;
    }
    return IMDStatus::Ok;
}

// The whole sector a partial write lands in, with the new bytes in place
IMDStatus IMDImage::_materialize(uint32_t lba, uint32_t sec_off,
                                 const uint8_t *src, uint32_t len, uint8_t *out)
{
    uint16_t sec_size = _sectors[lba].size;
    if (sec_off != 0 || len != sec_size)
    {
        IMDStatus status = readSector(lba, out, sec_size, nullptr);
        if (status != IMDStatus::Ok)
            return status;
    }
    memcpy(out + sec_off, src, len);
    return IMDStatus::Ok;
}

IMDStatus IMDImage::writeLinear(uint32_t byte_off, const uint8_t *buf,
                                uint32_t len)
{
    if (!_open)
        return IMDStatus::IoError;
    if (!_writable)
        return IMDStatus::ReadOnly;
    if (byte_off > _linear_size || len > _linear_size - byte_off)
        return IMDStatus::NoSuchSector;

    // Pass 0 checks every touched record, so a refusal leaves the image untouched
    for (int pass = 0; pass < 2; pass++)
    {
        for (uint32_t done = 0; done < len;)
        {
            uint32_t lba, sec_off;
            if (_locateLinear(byte_off + done, lba, sec_off) != IMDStatus::Ok)
                return IMDStatus::NoSuchSector;
            uint16_t sec_size = _sectors[lba].size;
            uint32_t chunk = std::min(sec_size - sec_off, len - done);

            if (_sectors[lba].rec_type == IMD_REC_UNAVAILABLE)
                return IMDStatus::WriteRefused;
            if (pass == 1)
            {
                IMDStatus status = _materialize(lba, sec_off, buf + done, chunk,
                                                _scratch.data());
                if (status != IMDStatus::Ok)
                    return status;
                status = writeSector(lba, _scratch.data(), sec_size);
                if (status != IMDStatus::Ok)
                    return status;
            }
            done += chunk;
        }
    }
    return IMDStatus::Ok;
}
