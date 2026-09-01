#ifdef BUILD_COCO

#include "decbLayout.h"

#include <stdio.h>
#include <string.h>

static const char *ext_for_type(uint8_t t)
{
    switch (t) {
    case 0: return "BAS";
    case 1: return "DAT";
    case 2: return "BIN";
    case 3: return "TXT";
    default: return "DAT";
    }
}

// Characters that may appear in a filename. The test is whether the user can
// type the name back at a CoCo keyboard, so this excludes DECB's own
// delimiters (. : / , "), space, wildcards, and 0x5B-0x5F, which display as
// arrows and graphics glyphs rather than the ASCII they stand for.
static bool decb_name_char(unsigned char c)
{
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
        return true;
    switch (c)
    {
    case '-': case '#': case '$': case '%': case '&': case '\'':
    case '(': case ')': case '+': case ';': case '<': case '=':
    case '>': case '@': case '!':
        return true;
    default:
        return false;
    }
}

// Disk BASIC names are 8 characters, space padded, starting with a letter.
// Tape names frequently are not.
static void sanitize_name(const char *in, int seq, char *out)
{
    int j = 0;
    for (int i = 0; in[i] && j < 8; i++)
    {
        unsigned char c = (unsigned char)in[i];
        if (c >= 'a' && c <= 'z')
            c -= 32;
        if (decb_name_char(c))
            out[j++] = (char)c;
        // Dropped rather than substituted: a stand-in the user cannot type
        // is worse than a shorter name.
    }
    out[j] = 0;

    // DECB expects a filename to begin with a letter.
    if (j == 0 || out[0] < 'A' || out[0] > 'Z')
        snprintf(out, 9, "TAPE%02d", seq % 100);
}

// Tapes may repeat a name; Disk BASIC would resolve every LOAD to the first
// match, so later ones take a suffix.
static void dedup_name(char *name, const DecbFile *files, int n)
{
    for (int attempt = 0; attempt < 100; attempt++)
    {
        bool clash = false;
        for (int i = 0; i < n; i++)
            if (!strcmp(files[i].name, name)) { clash = true; break; }
        if (!clash)
            return;

        char base[16];
        snprintf(base, sizeof base, "%s", name);
        int blen = (int)strlen(base);
        if (blen > 6) blen = 6;
        snprintf(name, 9, "%.*s%02d", blen, base, attempt + 1);
    }
}

// Length of a tokenized BASIC program, which is not always the payload length.
// A cassette save runs from the start of the program to the start of
// variables, so it often trails RAM past the program's end; a disk file must
// not, or the start of variables lands beyond the program and RUN fails.
//
// Lines are [next address][line number][tokens][00], ending with a zero next
// address. Anything that does not parse falls back to the full payload.
static uint32_t basic_program_length(CasIndex *idx, int ci, uint32_t payload)
{
    uint32_t off = 0;
    int lines = 0;

    while (off + 2 <= payload)
    {
        uint8_t hdr[2];
        if (idx->read_file(ci, off, hdr, 2) != 2)
            return payload;
        if (hdr[0] == 0 && hdr[1] == 0)
            return off + 2;                 // end of program marker

        if (off + 4 > payload)
            return payload;
        off += 4;                           // next-address and line number

        bool found = false;
        while (off < payload && !found)
        {
            uint8_t chunk[64];
            size_t want = payload - off;
            if (want > sizeof chunk)
                want = sizeof chunk;
            size_t got = idx->read_file(ci, off, chunk, want);
            if (got == 0)
                return payload;
            for (size_t i = 0; i < got; i++)
            {
                if (chunk[i] == 0)
                {
                    off += (uint32_t)i + 1;
                    found = true;
                    break;
                }
            }
            if (!found)
                off += (uint32_t)got;
        }
        if (!found)
            return payload;

        if (++lines > 8192)                 // runaway chain
            return payload;
    }
    return payload;
}

uint32_t DecbLayout::granule_to_lsn(uint8_t g)
{
    uint32_t track = g / 2;
    if (g >= 34)
        track++;                       // skip the directory track
    uint32_t half = g & 1;
    return track * DECB_SECTORS_TRACK + half * DECB_GRAN_SECTORS;
}

bool DecbLayout::lsn_to_granule(uint32_t lsn, uint8_t *g, uint32_t *offset)
{
    uint32_t track = lsn / DECB_SECTORS_TRACK;
    uint32_t sidx = lsn % DECB_SECTORS_TRACK;
    if (track == DECB_DIR_TRACK || track >= DECB_TRACKS)
        return false;

    uint32_t gtrack = (track < DECB_DIR_TRACK) ? track : track - 1;
    *g = (uint8_t)(gtrack * 2 + (sidx >= DECB_GRAN_SECTORS ? 1 : 0));
    *offset = (sidx % DECB_GRAN_SECTORS) * DECB_SECTOR_SIZE;
    return true;
}

bool DecbLayout::build(CasIndex *index, const char *volume_name)
{
    _idx = index;
    _nfiles = 0;
    _grans_used = 0;
    _dropped_full = 0;
    _dropped_empty = 0;
    memset(_volume, 0, sizeof _volume);
    if (volume_name)
        snprintf(_volume, sizeof _volume, "%s", volume_name);

    for (int i = 0; i < _idx->file_count(); i++)
    {
        const CasFileEntry &cf = _idx->file(i);

        if (cf.payload == 0)
        {
            _dropped_empty++;
            continue;
        }

        DecbFile f;
        memset(&f, 0, sizeof f);
        f.cas_index = i;
        f.ftype = cf.ftype > 3 ? 1 : cf.ftype;   // Disk BASIC knows 0..3 only
        f.ascii = cf.ascii;
        f.load = cf.load;
        f.exec = cf.exec;

        // An already-segmented payload is passed through; wrapping it twice
        // produces a file that will not load.
        f.wrap_ml = (cf.ftype == 2 && !cf.seg_ok);
        f.body_len = cf.payload;
        f.pre_len = 0;
        f.post_len = 0;

        if (f.wrap_ml)
        {
            // $00 len16 load16 <data> ... $FF $0000 exec16
            f.pre[0] = 0x00;
            f.pre[1] = (uint8_t)(f.body_len >> 8);
            f.pre[2] = (uint8_t)(f.body_len & 0xff);
            f.pre[3] = (uint8_t)(f.load >> 8);
            f.pre[4] = (uint8_t)(f.load & 0xff);
            f.pre_len = 5;

            f.post[0] = 0xff;
            f.post[1] = 0x00;
            f.post[2] = 0x00;
            f.post[3] = (uint8_t)(f.exec >> 8);
            f.post[4] = (uint8_t)(f.exec & 0xff);
            f.post_len = 5;
        }
        else if (f.ftype == 0 && !f.ascii)
        {
            // On disk a tokenized BASIC program carries a 3 byte header of
            // $FF and the program length; cassette files do not. Without it
            // Disk BASIC takes the program's first three bytes as the header,
            // so the program LISTs but RUN starts three bytes in.
            f.body_len = basic_program_length(_idx, i, cf.payload);

            f.pre[0] = 0xff;
            f.pre[1] = (uint8_t)(f.body_len >> 8);
            f.pre[2] = (uint8_t)(f.body_len & 0xff);
            f.pre_len = 3;
        }

        f.disk_size = f.pre_len + f.body_len + f.post_len;

        uint32_t grans = (f.disk_size + DECB_GRAN_SIZE - 1) / DECB_GRAN_SIZE;
        if (grans == 0)
            grans = 1;

        if (_nfiles >= DECB_MAX_DIRENTS ||
            _grans_used + grans > DECB_MAX_GRANULES)
        {
            _dropped_full++;
            continue;
        }

        snprintf(f.ext, sizeof f.ext, "%s", ext_for_type(f.ftype));
        sanitize_name(cf.name, i + 1, f.name);
        dedup_name(f.name, _files, _nfiles);

        f.first_granule = (uint8_t)_grans_used;
        f.granule_count = (uint8_t)grans;

        uint32_t tail = f.disk_size - (grans - 1) * DECB_GRAN_SIZE;
        uint32_t secs = (tail + DECB_SECTOR_SIZE - 1) / DECB_SECTOR_SIZE;
        if (secs == 0)
            secs = 1;
        f.sectors_last_gran = (uint8_t)secs;
        f.last_sector_size = (uint16_t)(tail - (secs - 1) * DECB_SECTOR_SIZE);

        _grans_used += grans;
        _files[_nfiles++] = f;
    }

    return _nfiles > 0;
}

int DecbLayout::file_for_granule(uint8_t g) const
{
    for (int i = 0; i < _nfiles; i++)
        if (g >= _files[i].first_granule &&
            g < _files[i].first_granule + _files[i].granule_count)
            return i;
    return -1;
}

void DecbLayout::build_gat(uint8_t *buf)
{
    // dskini zeros the sector then marks granules 0..67 free, leaving the
    // bytes past 67 zero.
    memset(buf, 0x00, DECB_SECTOR_SIZE);
    memset(buf, DECB_GRAN_FREE, DECB_MAX_GRANULES);

    for (int i = 0; i < _nfiles; i++)
    {
        const DecbFile &f = _files[i];
        for (uint8_t n = 0; n < f.granule_count; n++)
        {
            uint8_t g = f.first_granule + n;
            if (n + 1 < f.granule_count)
                buf[g] = (uint8_t)(g + 1);                       // chain onward
            else
                buf[g] = (uint8_t)(DECB_GRAN_LAST | f.sectors_last_gran);
        }
    }
}

void DecbLayout::build_dir_sector(int which, uint8_t *buf)
{
    memset(buf, 0xff, DECB_SECTOR_SIZE);   // 0xFF in byte 0 ends the directory

    for (int slot = 0; slot < DECB_DIRENTS_SECTOR; slot++)
    {
        int idx = which * DECB_DIRENTS_SECTOR + slot;
        if (idx >= _nfiles)
            return;

        uint8_t *e = buf + slot * DECB_DIRENT_SIZE;
        const DecbFile &f = _files[idx];

        memset(e, ' ', 11);
        size_t nl = strlen(f.name);
        memcpy(e, f.name, nl > 8 ? 8 : nl);
        size_t el = strlen(f.ext);
        memcpy(e + 8, f.ext, el > 3 ? 3 : el);

        e[11] = f.ftype;
        e[12] = f.ascii ? 0xff : 0x00;
        e[13] = f.first_granule;
        e[14] = (uint8_t)(f.last_sector_size >> 8);
        e[15] = (uint8_t)(f.last_sector_size & 0xff);
        memset(e + 16, 0x00, DECB_DIRENT_SIZE - 16);
    }
}

/*
 * Serve bytes of a file as they appear on disk. For a wrapped machine-language
 * file the disk image is a 5-byte preamble, the tape payload, then a 5-byte
 * postamble, so a sector can straddle any two of those three regions.
 */
size_t DecbLayout::read_disk_bytes(const DecbFile &f, uint32_t offset,
                                   uint8_t *buf, size_t len)
{
    if (offset >= f.disk_size)
        return 0;
    if (offset + len > f.disk_size)
        len = f.disk_size - offset;

    size_t done = 0;
    while (done < len)
    {
        uint32_t p = offset + (uint32_t)done;
        size_t want = len - done;

        if (p < f.pre_len)
        {
            size_t n = f.pre_len - p;
            if (n > want) n = want;
            memcpy(buf + done, f.pre + p, n);
            done += n;
        }
        else if (p < (uint32_t)f.pre_len + f.body_len)
        {
            uint32_t in_body = p - f.pre_len;
            size_t n = f.body_len - in_body;
            if (n > want) n = want;
            size_t got = _idx->read_file(f.cas_index, in_body, buf + done, n);
            done += got;
            if (got < n)
                break;
        }
        else
        {
            uint32_t in_post = p - f.pre_len - f.body_len;
            if (in_post >= f.post_len)
                break;
            size_t n = f.post_len - in_post;
            if (n > want) n = want;
            memcpy(buf + done, f.post + in_post, n);
            done += n;
        }
    }
    return done;
}

bool DecbLayout::read_sector(uint32_t lsn, uint8_t *buf)
{
    if (lsn >= DECB_TOTAL_SECTORS)
        return false;

    if (lsn == DECB_LSN_GAT)
    {
        build_gat(buf);
        return true;
    }
    if (lsn >= DECB_LSN_DIR_FIRST && lsn < DECB_LSN_DIR_FIRST + DECB_DIR_SECTORS)
    {
        build_dir_sector((int)(lsn - DECB_LSN_DIR_FIRST), buf);
        return true;
    }
    if (lsn == DECB_LSN_TRACK17)
    {
        memset(buf, 0x00, DECB_SECTOR_SIZE);   // dskini writes this one zeroed
        return true;
    }
    if (lsn == DECB_LSN_DISKNAME)
    {
        memset(buf, 0xff, DECB_SECTOR_SIZE);
        if (_volume[0])
            memcpy(buf, _volume, strlen(_volume) + 1);  // dskini strcpy's it
        return true;
    }
    if (lsn / DECB_SECTORS_TRACK == DECB_DIR_TRACK)
    {
        memset(buf, 0xff, DECB_SECTOR_SIZE);   // rest of the directory track
        return true;
    }

    uint8_t g;
    uint32_t off_in_gran;
    if (!lsn_to_granule(lsn, &g, &off_in_gran))
        return false;

    memset(buf, 0xff, DECB_SECTOR_SIZE);       // unallocated space reads as $FF

    int fi = file_for_granule(g);
    if (fi < 0)
        return true;

    const DecbFile &f = _files[fi];
    uint32_t offset = (uint32_t)(g - f.first_granule) * DECB_GRAN_SIZE + off_in_gran;
    if (offset >= f.disk_size)
        return true;                            // slack past end of file

    size_t got = read_disk_bytes(f, offset, buf, DECB_SECTOR_SIZE);
    if (got < DECB_SECTOR_SIZE)
        memset(buf + got, 0x00, DECB_SECTOR_SIZE - got);  // pad the tail sector
    return true;
}

#endif // BUILD_COCO
