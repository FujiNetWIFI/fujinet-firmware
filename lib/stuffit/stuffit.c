/*
 * stuffit.c - archive identification, entry enumeration and per-method
 * decompression dispatch for both StuffIt archive formats.
 *
 * Ported from XADStuffItParser.m (classic "SIT!" format) and
 * XADStuffIt5Parser.m (StuffIt 5 "StuffIt (c)1997-" format), The
 * Unarchiver (https://github.com/MacPaw/XADMaster), LGPL v2.1+. Field
 * layouts, magic bytes, the folder start/end marker scheme, the
 * StuffIt5 "growing for loop" entry walk and its offset->directory
 * table are all taken directly from those two files.
 */
#include "stuffit.h"
#include "sit_internal.h"
#include "sit_crc16.h"

#include <string.h>
#include <stdlib.h>

/* ------------------------------------------------------------------- */
/* libc-backed convenience allocator                                    */
/* ------------------------------------------------------------------- */
static void *sit_libc_alloc(size_t n, void *ctx) { (void)ctx; return malloc(n); }
static void sit_libc_free(void *p, void *ctx) { (void)ctx; free(p); }
const sit_allocator sit_libc_allocator = { sit_libc_alloc, sit_libc_free, NULL };

/* ------------------------------------------------------------------- */
/* error strings                                                        */
/* ------------------------------------------------------------------- */
const char *sit_strerror(int code)
{
    switch (code) {
        case SIT_OK:           return "OK";
        case SIT_E_IO:          return "I/O error";
        case SIT_E_FORMAT:      return "not a recognized StuffIt archive";
        case SIT_E_CRC:         return "CRC-16 mismatch";
        case SIT_E_UNSUPPORTED: return "unsupported compression method";
        case SIT_E_ENCRYPTED:   return "entry is encrypted";
        case SIT_E_NOMEM:       return "out of memory";
        case SIT_E_CORRUPT:     return "corrupt compressed data";
        case SIT_E_INVAL:       return "invalid argument";
        case SIT_E_EOF:         return "end of archive";
        case SIT_E_LIMIT:       return "internal table limit exceeded";
        default:                return "unknown error";
    }
}

const char *sit_method_name(uint8_t method)
{
    switch (method) {
        case 0:  return "None";
        case 1:  return "RLE";
        case 2:  return "LZW";
        case 3:  return "Huffman";
        case 13: return "LZ+Huffman";
        case 15: return "Arsenic";
        default: return "?";
    }
}

const char *sit_format_name(sit_format fmt)
{
    switch (fmt) {
        case SIT_FMT_CLASSIC: return "SIT!";
        case SIT_FMT_SIT5:    return "StuffIt 5";
        default:              return "?";
    }
}

sit_format sit_get_format(const sit_archive *ar)
{
    return (ar != NULL) ? ar->format : SIT_FMT_UNKNOWN;
}

/* ------------------------------------------------------------------- */
/* small big-endian FILE* readers - used only for header/metadata       */
/* parsing (not fork decompression, which always goes through sit_io). */
/* ------------------------------------------------------------------- */
static int f_u8(FILE *f, uint8_t *v)
{
    int c = fgetc(f);
    if (c < 0) return SIT_E_IO;
    *v = (uint8_t)c;
    return SIT_OK;
}

static int f_u16(FILE *f, uint16_t *v)
{
    uint8_t b[2];
    if (fread(b, 1, 2, f) != 2) return SIT_E_IO;
    *v = (uint16_t)((b[0] << 8) | b[1]);
    return SIT_OK;
}

static int f_u32(FILE *f, uint32_t *v)
{
    uint8_t b[4];
    if (fread(b, 1, 4, f) != 4) return SIT_E_IO;
    *v = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
    return SIT_OK;
}

static int f_skip(FILE *f, long n)
{
    if (n <= 0) return SIT_OK;
    if (fseek(f, n, SEEK_CUR) != 0) return SIT_E_IO;
    return SIT_OK;
}

static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

#define TRY(x) do { int _e = (x); if (_e) return _e; } while (0)

/* ------------------------------------------------------------------- */
/* format identification + sit_open                                     */
/* ------------------------------------------------------------------- */

/* StuffIt 5 magic: "StuffIt (c)1997-" + 4 wildcard bytes +
 * " Aladdin Systems, Inc., http://www.aladdinsys.com/StuffIt/\r\n"
 * (XADStuffIt5Parser.m recognizeFileWithHandle:firstBytes:name:). */
static const unsigned char sit5_template[] =
    "StuffIt (c)1997-\xFF\xFF\xFF\xFF Aladdin Systems, Inc., "
    "http://www.aladdinsys.com/StuffIt/\r\n";

static int looks_like_sit5(const uint8_t *hdr, size_t n)
{
    size_t len = sizeof(sit5_template) - 1;
    if (n < len) return 0;
    for (size_t i = 0; i < len; i++) {
        if (sit5_template[i] != 0xFF && hdr[i] != sit5_template[i]) return 0;
    }
    return 1;
}

/* Classic magic: bytes[10..13]=="rLau" and bytes[0..3] one of
 * "SIT!" / "STin" / "STi[0-9]" / "ST[0-9][0-9]"
 * (XADStuffItParser.m recognizeFileWithHandle:firstBytes:name:). */
static int looks_like_classic(const uint8_t *hdr, size_t n)
{
    if (n < 14) return 0;
    if (!(hdr[10] == 'r' && hdr[11] == 'L' && hdr[12] == 'a' && hdr[13] == 'u')) return 0;

    if (hdr[0] == 'S' && hdr[1] == 'I' && hdr[2] == 'T' && hdr[3] == '!') return 1;
    if (hdr[0] == 'S' && hdr[1] == 'T') {
        if (hdr[2] == 'i' && (hdr[3] == 'n' || (hdr[3] >= '0' && hdr[3] <= '9'))) return 1;
        if (hdr[2] >= '0' && hdr[2] <= '9' && hdr[3] >= '0' && hdr[3] <= '9') return 1;
    }
    return 0;
}

static int open_classic(sit_archive *ar, long base)
{
    FILE *f = ar->f;
    if (fseek(f, base, SEEK_SET) != 0) return SIT_E_IO;

    uint8_t ah[22];
    if (fread(ah, 1, sizeof(ah), f) != sizeof(ah)) return SIT_E_FORMAT;

    ar->format = SIT_FMT_CLASSIC;
    ar->base = base;
    ar->totalsize = (long)be32(ah + 6);
    ar->nextpos = base + 22;
    ar->u.classic.depth = 0;
    return SIT_OK;
}

/* SIT5_ARCHIVEFLAGS_* from XADStuffIt5Parser.m */
#define SIT5_AF_14BYTES 0x10
#define SIT5_AF_COMMENT 0x20
#define SIT5_AF_META    0x40
#define SIT5_AF_CRYPTED 0x80

static int open_sit5(sit_archive *ar, long base)
{
    FILE *f = ar->f;
    if (fseek(f, base, SEEK_SET) != 0) return SIT_E_IO;

    TRY(f_skip(f, 82));
    uint8_t version, aflags;
    TRY(f_u8(f, &version));
    TRY(f_u8(f, &aflags));
    if (version != 5) return SIT_E_FORMAT;

    uint32_t ignore32;
    uint16_t numfiles, ignore16;
    uint32_t firstoffs;
    TRY(f_u32(f, &ignore32)); /* total archive size, unused */
    TRY(f_u32(f, &ignore32)); /* unknown */
    TRY(f_u16(f, &numfiles));
    TRY(f_u32(f, &firstoffs));
    TRY(f_u16(f, &ignore16)); /* header crc, unused */

    if (aflags & SIT5_AF_14BYTES) TRY(f_skip(f, 14));

    if (aflags & SIT5_AF_CRYPTED) {
        uint8_t hashsize;
        TRY(f_u8(f, &hashsize));
        if (hashsize != 5) return SIT_E_FORMAT;
        TRY(f_skip(f, hashsize)); /* archive password hash, decryption out of scope */
    }

    if (aflags & SIT5_AF_META) {
        uint16_t length_n;
        TRY(f_u16(f, &length_n));
        TRY(f_skip(f, (long)length_n * 20));
    }

    if (aflags & SIT5_AF_COMMENT) {
        uint16_t commentsize, length_b;
        TRY(f_u16(f, &commentsize));
        TRY(f_u16(f, &length_b));
        if (commentsize) TRY(f_skip(f, commentsize));
        TRY(f_skip(f, length_b));
    }

    ar->format = SIT_FMT_SIT5;
    ar->base = base;
    ar->nextpos = base + (long)firstoffs;
    ar->u.sit5.base = base;
    ar->u.sit5.numentries = numfiles;
    ar->u.sit5.index = 0;
    ar->u.sit5.ndirs = 0;
    return SIT_OK;
}

int sit_open(sit_archive *ar, FILE *f, const sit_allocator *a)
{
    if (!ar || !f) return SIT_E_INVAL;

    memset(ar, 0, sizeof(*ar));
    ar->f = f;
    ar->alloc = a ? *a : sit_libc_allocator;
    ar->format = SIT_FMT_UNKNOWN;
    ar->error = SIT_OK;
    ar->done = 0;

    long base = ftell(f);
    if (base < 0) return SIT_E_IO;

    uint8_t probe[sizeof(sit5_template) - 1 > 22 ? sizeof(sit5_template) - 1 : 22];
    size_t got = fread(probe, 1, sizeof(probe), f);
    if (fseek(f, base, SEEK_SET) != 0) return SIT_E_IO;

    if (looks_like_classic(probe, got)) return open_classic(ar, base);
    if (looks_like_sit5(probe, got)) return open_sit5(ar, base);

    /* MacBinary wrapper (.bin, downloaded .sea): a 128-byte header with a
     * zero at 0, a 1..63 byte name length at 1 and zeros at 74 and 82, then
     * the data fork, i.e. the archive, at offset 128. */
    if (got >= 22 && probe[0] == 0 && probe[1] >= 1 && probe[1] <= 63) {
        uint8_t mb[128];
        if (fread(mb, 1, sizeof(mb), f) == sizeof(mb) && mb[74] == 0 && mb[82] == 0) {
            long inner = base + 128;
            got = fread(probe, 1, sizeof(probe), f);
            if (fseek(f, inner, SEEK_SET) != 0) return SIT_E_IO;
            if (looks_like_classic(probe, got)) return open_classic(ar, inner);
            if (looks_like_sit5(probe, got)) return open_sit5(ar, inner);
        }
        if (fseek(f, base, SEEK_SET) != 0) return SIT_E_IO;
    }

    return SIT_E_FORMAT;
}

/* ------------------------------------------------------------------- */
/* classic-format entry walk                                            */
/* ------------------------------------------------------------------- */

/* StuffIt entry-header flag bits (XADStuffItParser.m) */
#define SIT_HDR_ENCRYPTED       0x80
#define SIT_HDR_START_FOLDER    0x20
#define SIT_HDR_END_FOLDER      0x21
#define SIT_HDR_FOLDER_ENC      0x10
#define SIT_HDR_FOLDER_MASK     0x6f /* ~(0x80|0x10), applied to an 8-bit method byte */

static void classic_build_path(sit_archive *ar, const uint8_t *namebytes, int namelen, char *out, size_t outsz)
{
    size_t pos = 0;
    for (int i = 0; i < ar->u.classic.depth && pos < outsz; i++) {
        int n = snprintf(out + pos, outsz - pos, "%s/", ar->u.classic.name[i]);
        if (n < 0) break;
        pos += (size_t)n;
        if (pos >= outsz) { pos = outsz - 1; break; }
    }
    if (pos < outsz) {
        size_t room = outsz - 1 - pos;
        size_t n = (size_t)namelen < room ? (size_t)namelen : room;
        memcpy(out + pos, namebytes, n);
        pos += n;
    }
    if (pos >= outsz) pos = outsz - 1;
    out[pos] = '\0';
}

static int classic_next_entry(sit_archive *ar, sit_entry *e)
{
    FILE *f = ar->f;

    for (;;) {
        if (ar->nextpos + 112 > ar->base + ar->totalsize) { ar->done = 1; return 0; }

        if (fseek(f, ar->nextpos, SEEK_SET) != 0) return SIT_E_IO;
        uint8_t hdr[112];
        if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr)) return SIT_E_IO;

        uint16_t stored_crc = be16(hdr + 110);
        uint16_t calc_crc = sit_crc16_update(0, hdr, 110);
        if (stored_crc != calc_crc) return SIT_E_CRC;

        int rsrcmethod = hdr[0];
        int datamethod = hdr[1];
        int namelen = hdr[2];
        if (namelen > 31) namelen = 31;
        const uint8_t *namebytes = hdr + 3;

        uint32_t rsrclen = be32(hdr + 84);
        uint32_t datalen = be32(hdr + 88);
        uint32_t rsrccomplen = be32(hdr + 92);
        uint32_t datacomplen = be32(hdr + 96);
        uint16_t datacrc = be16(hdr + 102);

        long start = ar->nextpos + 112;

        int dmask = datamethod & SIT_HDR_FOLDER_MASK;
        int rmask = rsrcmethod & SIT_HDR_FOLDER_MASK;

        if (dmask == SIT_HDR_START_FOLDER || rmask == SIT_HDR_START_FOLDER) {
            if (ar->u.classic.depth >= SIT_MAX_FOLDER_DEPTH) return SIT_E_LIMIT;
            char *slot = ar->u.classic.name[ar->u.classic.depth];
            int n = namelen < SIT_MAX_NAME - 1 ? namelen : SIT_MAX_NAME - 1;
            memcpy(slot, namebytes, (size_t)n);
            slot[n] = '\0';
            ar->u.classic.depth++;
            ar->nextpos = start; /* folders carry no fork data */
            continue;
        }

        if (dmask == SIT_HDR_END_FOLDER || rmask == SIT_HDR_END_FOLDER) {
            if (ar->u.classic.depth > 0) ar->u.classic.depth--;
            ar->nextpos = start;
            continue;
        }

        /* real file entry - always reported, even when one fork is
         * entirely absent (data_len==0 resource-only entries included,
         * e.g. Risk.sit's resource-fork-only entries); sit_extract()
         * handles a zero-length fork trivially. */
        uint16_t rsrccrc = be16(hdr + 100);

        memset(e, 0, sizeof(*e));
        classic_build_path(ar, namebytes, namelen, e->path, sizeof(e->path));
        memcpy(e->type, hdr + 66, 4); e->type[4] = '\0';
        memcpy(e->creator, hdr + 70, 4); e->creator[4] = '\0';
        e->data_method = (uint8_t)(datamethod & 0x0f);
        e->data_comp_len = datacomplen;
        e->data_len = datalen;
        e->data_crc = datacrc;
        e->data_offset = start + (long)rsrccomplen;
        e->rsrc_method = (uint8_t)(rsrcmethod & 0x0f);
        e->rsrc_comp_len = rsrccomplen;
        e->rsrc_len = rsrclen;
        e->rsrc_crc = rsrccrc;
        e->rsrc_offset = start;
        e->cdate = be32(hdr + 76);
        e->mdate = be32(hdr + 80);
        e->flags = ((datamethod & SIT_HDR_ENCRYPTED) ? SIT_FLAG_ENCRYPTED : 0)
                 | ((rsrcmethod & SIT_HDR_ENCRYPTED) ? SIT_FLAG_RSRC_ENCRYPTED : 0);

        ar->nextpos = start + (long)rsrccomplen + (long)datacomplen;
        return 1;
    }
}

/* ------------------------------------------------------------------- */
/* StuffIt5 entry walk                                                   */
/* ------------------------------------------------------------------- */

#define SIT5_FLAG_DIRECTORY 0x40
#define SIT5_FLAG_CRYPTED   0x20

static int sit5_lookup_dir(sit_archive *ar, uint32_t diroffs, const char **outpath)
{
    if (diroffs == 0) { *outpath = ""; return SIT_OK; }
    for (int i = 0; i < ar->u.sit5.ndirs; i++) {
        if (ar->u.sit5.dirs[i].offset == (long)diroffs) {
            *outpath = ar->u.sit5.dirs[i].path;
            return SIT_OK;
        }
    }
    return SIT_E_FORMAT; /* a child referenced a directory we never saw */
}

/* namebuf/fullpath (256 bytes each) are heap-allocated below (through
 * ar->alloc) rather than kept as locals, so this function's stack
 * frame stays small on a constrained target - TRY is locally
 * redefined to free them and jump to `cleanup` instead of returning
 * directly, for every use from the allocation point onward. */
static int sit5_next_entry(sit_archive *ar, sit_entry *e)
{
    FILE *f = ar->f;
    uint8_t *namebuf = NULL;
    char *fullpath = NULL;
    int rc = SIT_OK;

    for (;;) {
        if (ar->u.sit5.index >= ar->u.sit5.numentries) { ar->done = 1; return 0; }

        long entryoff = ar->nextpos;
        if (fseek(f, entryoff, SEEK_SET) != 0) return SIT_E_IO;

        uint32_t id;
        TRY(f_u32(f, &id));
        if (id != 0xA5A5A5A5u) return SIT_E_FORMAT;

        uint8_t ver;
        TRY(f_u8(f, &ver));
        TRY(f_skip(f, 1));
        uint16_t headersize;
        TRY(f_u16(f, &headersize));
        long headerend = entryoff + (long)headersize;
        TRY(f_skip(f, 1));
        uint8_t eflags;
        TRY(f_u8(f, &eflags));
        uint32_t cdate, mdate;
        TRY(f_u32(f, &cdate));
        TRY(f_u32(f, &mdate));
        TRY(f_skip(f, 8)); /* prevoffs, nextoffs */
        uint32_t diroffs;
        TRY(f_u32(f, &diroffs));
        uint16_t namelen;
        TRY(f_u16(f, &namelen));
        TRY(f_skip(f, 2)); /* header crc */
        uint32_t datalen, datacomplen;
        TRY(f_u32(f, &datalen));
        TRY(f_u32(f, &datacomplen));
        uint16_t datacrc;
        TRY(f_u16(f, &datacrc));
        TRY(f_skip(f, 2));

        int isdir = (eflags & SIT5_FLAG_DIRECTORY) != 0;
        uint16_t childnum = 0;
        uint8_t datamethod = 0;

        if (isdir) {
            TRY(f_u16(f, &childnum));
            if (datalen == 0xffffffffu) {
                /* bogus synthetic entry StuffIt5 emits after some directory
                 * entries; its header ends right here, no name/etc follows. */
                ar->u.sit5.numentries++;
                ar->u.sit5.index++;
                long p = ftell(f);
                if (p < 0) return SIT_E_IO;
                ar->nextpos = p;
                continue;
            }
        } else {
            uint8_t passlen;
            TRY(f_u8(f, &datamethod));
            TRY(f_u8(f, &passlen));
            if (passlen) TRY(f_skip(f, passlen)); /* encryption key bytes, decryption out of scope */
        }

#undef TRY
#define TRY(x) do { int _e = (x); if (_e) { rc = _e; goto cleanup; } } while (0)

        namebuf = (uint8_t *)ar->alloc.alloc(256, ar->alloc.ctx);
        if (!namebuf) { rc = SIT_E_NOMEM; goto cleanup; }
        int nl = namelen > 255 ? 255 : (int)namelen;
        if (fread(namebuf, 1, (size_t)nl, f) != (size_t)nl) { rc = SIT_E_IO; goto cleanup; }
        if (namelen > 255) TRY(f_skip(f, namelen - 255));
        namebuf[nl] = '\0';

        long curpos = ftell(f);
        if (curpos < 0) { rc = SIT_E_IO; goto cleanup; }
        if (curpos < headerend) {
            uint16_t commentsize;
            TRY(f_u16(f, &commentsize));
            TRY(f_skip(f, 2));
            TRY(f_skip(f, commentsize));
        }

        uint16_t something;
        TRY(f_u16(f, &something));
        TRY(f_skip(f, 2));
        uint8_t filetype[4], filecreator[4];
        if (fread(filetype, 1, 4, f) != 4) { rc = SIT_E_IO; goto cleanup; }
        if (fread(filecreator, 1, 4, f) != 4) { rc = SIT_E_IO; goto cleanup; }
        uint16_t finderflags;
        TRY(f_u16(f, &finderflags));
        (void)finderflags;
        TRY(f_skip(f, ver == 1 ? 22 : 18));

        int hasresource = something & 1;
        uint32_t resourcelen = 0, resourcecomplen = 0;
        uint8_t resourcemethod = 0;
        uint16_t resourcecrc = 0;
        if (hasresource) {
            uint8_t rpasslen;
            TRY(f_u32(f, &resourcelen));
            TRY(f_u32(f, &resourcecomplen));
            TRY(f_u16(f, &resourcecrc));
            TRY(f_skip(f, 2)); /* unknown, per XADStuffIt5Parser.m - previously missing here,
                                 * which misaligned resourcemethod/datastart by 2 bytes for any
                                 * entry with a resource fork; harmless before this change only
                                 * because no earlier sample archive exercised it. */
            TRY(f_u8(f, &resourcemethod));
            TRY(f_u8(f, &rpasslen));
            if (rpasslen) TRY(f_skip(f, rpasslen));
        }

        long datastart = ftell(f);
        if (datastart < 0) { rc = SIT_E_IO; goto cleanup; }

        const char *parentpath;
        TRY(sit5_lookup_dir(ar, diroffs, &parentpath));

        fullpath = (char *)ar->alloc.alloc(SIT_MAX_PATH, ar->alloc.ctx);
        if (!fullpath) { rc = SIT_E_NOMEM; goto cleanup; }
        if (parentpath[0]) snprintf(fullpath, SIT_MAX_PATH, "%s/%s", parentpath, (const char *)namebuf);
        else snprintf(fullpath, SIT_MAX_PATH, "%s", (const char *)namebuf);

        ar->alloc.free(namebuf, ar->alloc.ctx);
        namebuf = NULL;

        if (isdir) {
            if (ar->u.sit5.ndirs >= SIT_MAX_DIR_TABLE) { rc = SIT_E_LIMIT; goto cleanup; }
            sit_dir_table_entry *d = &ar->u.sit5.dirs[ar->u.sit5.ndirs++];
            d->offset = entryoff;
            snprintf(d->path, sizeof(d->path), "%s", fullpath);
            ar->alloc.free(fullpath, ar->alloc.ctx);
            fullpath = NULL;

            ar->u.sit5.numentries += childnum;
            ar->u.sit5.index++;
            ar->nextpos = datastart; /* reference seeks to datastart, not headerend, for directories */
            continue;
        }

        ar->u.sit5.index++;
        long next = datastart + (long)resourcecomplen + (long)datacomplen;

        /* Always report one entry per real file, both fork descriptors
         * filled in (a fork that doesn't exist reads as len==0,
         * comp_len==0) - previously an entry with data_len==0 and a
         * resource fork (hasresource) was dropped here entirely, which
         * hid resource-fork-only entries completely. */
        memset(e, 0, sizeof(*e));
        snprintf(e->path, sizeof(e->path), "%s", fullpath);
        ar->alloc.free(fullpath, ar->alloc.ctx);
        fullpath = NULL;
        memcpy(e->type, filetype, 4); e->type[4] = '\0';
        memcpy(e->creator, filecreator, 4); e->creator[4] = '\0';
        e->data_method = (uint8_t)(datamethod & 0x0f);
        e->data_comp_len = datacomplen;
        e->data_len = datalen;
        e->data_crc = (uint16_t)datacrc;
        e->data_offset = datastart + (long)resourcecomplen;
        e->rsrc_method = hasresource ? (uint8_t)(resourcemethod & 0x0f) : 0;
        e->rsrc_comp_len = resourcecomplen;
        e->rsrc_len = resourcelen;
        e->rsrc_crc = hasresource ? (uint16_t)resourcecrc : 0;
        e->rsrc_offset = datastart;
        e->cdate = cdate;
        e->mdate = mdate;
        e->flags = (((eflags & SIT5_FLAG_CRYPTED) && datalen) ? SIT_FLAG_ENCRYPTED : 0)
                 | (((eflags & SIT5_FLAG_CRYPTED) && resourcelen) ? SIT_FLAG_RSRC_ENCRYPTED : 0);

        ar->nextpos = next;
        return 1;
    }

cleanup:
    if (namebuf) ar->alloc.free(namebuf, ar->alloc.ctx);
    if (fullpath) ar->alloc.free(fullpath, ar->alloc.ctx);
    return rc;
}
#undef TRY
#define TRY(x) do { int _e = (x); if (_e) return _e; } while (0)

/* ------------------------------------------------------------------- */
/* public entry walk / extraction / close                               */
/* ------------------------------------------------------------------- */

int sit_next_entry(sit_archive *ar, sit_entry *e)
{
    if (!ar || !e) return SIT_E_INVAL;
    if (ar->error) return ar->error;
    if (ar->done) return 0;

    int r;
    switch (ar->format) {
        case SIT_FMT_CLASSIC: r = classic_next_entry(ar, e); break;
        case SIT_FMT_SIT5:    r = sit5_next_entry(ar, e); break;
        default: return SIT_E_FORMAT;
    }

    if (r < 0) { ar->error = r; ar->done = 1; }
    return r;
}

typedef struct {
    sit_sink_fn user_sink;
    void *user_ctx;
    uint16_t crc;
    sit_progress *prog;
} crc_wrap_ctx;

static int crc_wrap_sink(const uint8_t *buf, size_t n, void *ctx)
{
    crc_wrap_ctx *c = (crc_wrap_ctx *)ctx;
    c->crc = sit_crc16_update(c->crc, buf, n);
    if (c->prog) c->prog->bytes_out += (uint32_t)n;
    return c->user_sink(buf, n, c->user_ctx);
}

#define SIT_EXTRACT_STORED_BUFSIZE 1024

static int extract_stored(sit_io *io, uint32_t outlen, sit_sink_fn sink, void *ctx,
                           const sit_allocator *alloc)
{
    uint8_t *buf = (uint8_t *)alloc->alloc(SIT_EXTRACT_STORED_BUFSIZE, alloc->ctx);
    if (!buf) return SIT_E_NOMEM;

    int rc = SIT_OK;
    uint32_t remain = outlen;
    while (remain > 0) {
        size_t want = remain < SIT_EXTRACT_STORED_BUFSIZE ? remain : SIT_EXTRACT_STORED_BUFSIZE;
        size_t got = 0;
        while (got < want) {
            int c = sit_io_getbyte(io);
            if (c < 0) { rc = SIT_E_IO; goto done; }
            buf[got++] = (uint8_t)c;
        }
        if (sink(buf, got, ctx)) { rc = SIT_E_IO; goto done; }
        remain -= (uint32_t)got;
    }

done:
    alloc->free(buf, alloc->ctx);
    return rc;
}

int sit_extract(sit_archive *ar, const sit_entry *e, sit_fork which,
                 sit_sink_fn sink, void *ctx, sit_progress *prog)
{
    if (!ar || !e || !sink) return SIT_E_INVAL;

    uint8_t method;
    uint32_t comp_len, len;
    uint16_t crc;
    long offset;
    int enc_flag;

    switch (which) {
        case SIT_FORK_DATA:
            method = e->data_method; comp_len = e->data_comp_len; len = e->data_len;
            crc = e->data_crc; offset = e->data_offset; enc_flag = SIT_FLAG_ENCRYPTED;
            break;
        case SIT_FORK_RSRC:
            method = e->rsrc_method; comp_len = e->rsrc_comp_len; len = e->rsrc_len;
            crc = e->rsrc_crc; offset = e->rsrc_offset; enc_flag = SIT_FLAG_RSRC_ENCRYPTED;
            break;
        default:
            return SIT_E_INVAL;
    }

    if (e->flags & enc_flag) return SIT_E_ENCRYPTED;

    if (prog) { prog->bytes_out = 0; prog->bytes_total = len; prog->block_size = 0; }

    /* Zero-length fork (absent, or an entry that simply has nothing in
     * this fork) succeeds trivially without calling sink at all - same
     * as method 0 with outlen 0 always did, made explicit here so it
     * holds for every method (Arsenic in particular reads a block
     * header unconditionally, which a genuinely empty fork never has). */
    if (len == 0 && comp_len == 0) return SIT_OK;

    /* sit_io carries a 4KB internal read-ahead buffer (SIT_IO_BUFSIZE) -
     * heap-allocated here (via the caller-supplied allocator) rather
     * than kept as a local, so it doesn't sit on a constrained target's
     * call stack for the whole extraction. */
    sit_io *io = (sit_io *)ar->alloc.alloc(sizeof(sit_io), ar->alloc.ctx);
    if (!io) return SIT_E_NOMEM;
    sit_io_init(io, ar->f, offset, (long)comp_len);

    crc_wrap_ctx wrap = { sink, ctx, 0, prog };
    int rc;

    switch (method) {
        case 0:  rc = extract_stored(io, len, crc_wrap_sink, &wrap, &ar->alloc); break;
        case 1:  rc = sit_rle90_decompress(io, len, crc_wrap_sink, &wrap, &ar->alloc); break;
        case 2:  rc = sit_lzw_decompress(io, len, crc_wrap_sink, &wrap, &ar->alloc); break;
        case 3:  rc = sit_huffman_decompress(io, len, crc_wrap_sink, &wrap, &ar->alloc); break;
        case 13: rc = sit_lzh13_decompress(io, len, crc_wrap_sink, &wrap, &ar->alloc); break;
        case 15: rc = sit_arsenic_decompress(io, len, crc_wrap_sink, &wrap, &ar->alloc, prog); break;
        default: ar->alloc.free(io, ar->alloc.ctx); return SIT_E_UNSUPPORTED;
    }
    ar->alloc.free(io, ar->alloc.ctx);
    if (rc) return rc;

    /* Method 15's stored CRC field is stale/meaningless in the reference
     * implementation (see sit_arsenic.c) - only verify for other methods. */
    if (method != 15 && wrap.crc != crc) return SIT_E_CRC;

    return SIT_OK;
}

void sit_close(sit_archive *ar)
{
    if (!ar) return;
    memset(ar, 0, sizeof(*ar));
}
