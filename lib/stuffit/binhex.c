/*
 * binhex.c - see binhex.h.
 *
 * Ported from XADBinHexParser.m (The Unarchiver, LGPL v2.1+): the
 * banner scan/recognition logic from +recognizeFileWithHandle:..., and
 * the three-layer decode (6-bit alphabet -> RLE90 -> logical byte
 * stream) from XADBinHexHandle's GetBits/DecodeByte/produceByteAtOffset:.
 * The RLE90 layer itself is sit_rle90_reader (sit_internal.h /
 * sit_rle90.c), reused rather than duplicated.
 *
 * Layout of the decoded logical stream (all of this is pulled through
 * the two decode layers, one byte at a time - see hqx_open()):
 *
 *   u8   namelen (1..63)
 *   u8[namelen] filename
 *   u8   version (ignored)
 *   u32be type
 *   u32be creator
 *   u16be finder_flags
 *   u32be datalen
 *   u32be resourcelen
 *   u16be header_crc     (CRC-CCITT over namelen..resourcelen inclusive)
 *   u8[datalen] data fork bytes
 *   u16be data_crc        (CRC-CCITT, freshly re-seeded, over just the data bytes)
 *   u8[resourcelen] resource fork bytes   (not read - see hqx_open())
 *   u16be rsrc_crc                          (not read either)
 */
#include "binhex.h"
#include "sit_internal.h"

#include <string.h>

/* ------------------------------------------------------------------- */
/* banner scan / detection                                              */
/* ------------------------------------------------------------------- */

#define HQX_SCAN_MAX 8192
static const char HQX_MAGIC[] = "(This file must be converted with BinHex";
#define HQX_MAGIC_LEN (sizeof(HQX_MAGIC) - 1) /* 40 */

/* Finds the ':' that starts the encoded stream, per
 * +recognizeFileWithHandle:firstBytes:name:. On success *colon_off is
 * its absolute offset in f; f's position is unspecified on return
 * either way (the caller always seeks explicitly next). */
static int hqx_detect(FILE *f, const sit_allocator *alloc, long base, long *colon_off)
{
    uint8_t *buf = (uint8_t *)alloc->alloc(HQX_SCAN_MAX, alloc->ctx);
    if (!buf) return SIT_E_NOMEM;

    if (fseek(f, base, SEEK_SET) != 0) { alloc->free(buf, alloc->ctx); return SIT_E_IO; }
    size_t got = fread(buf, 1, HQX_SCAN_MAX, f);

    int found = 0;
    for (size_t i = 0; i + HQX_MAGIC_LEN <= got; i++) {
        if (memcmp(buf + i, HQX_MAGIC, HQX_MAGIC_LEN) != 0) continue;

        size_t p = i + HQX_MAGIC_LEN;
        while (p < got && buf[p] != '\n' && buf[p] != '\r') p++;
        if (p == got) break; /* truncated right at the buffer edge - treat as not found */

        while (p < got && (buf[p] == '\n' || buf[p] == '\r' || buf[p] == '\t' || buf[p] == ' ')) p++;
        if (p == got) break;

        if (buf[p] == ':') {
            *colon_off = base + (long)p;
            found = 1;
        }
        break; /* only the first banner occurrence is ever checked, matching the reference */
    }

    alloc->free(buf, alloc->ctx);
    return found;
}

/* ------------------------------------------------------------------- */
/* layer 1: 6-bit alphabet -> raw byte stream                           */
/* ------------------------------------------------------------------- */

/* Exact alphabet string, index == 6-bit value - do not reorder. */
static const char HQX_ALPHABET[65] =
    "!\"#$%&'()*+,-012345689@ABCDEFGHIJKLMNPQRSTUVXYZ[`abcdefhijklmpqr";

static int hqx_alphabet_index(int c)
{
    for (int i = 0; i < 64; i++) {
        if ((unsigned char)HQX_ALPHABET[i] == (unsigned char)c) return i;
    }
    return -1;
}

typedef struct {
    FILE *f;
    int atend; /* the ':' terminator was seen - clean end of stream */
    int err;   /* SIT_E_* once set (bad alphabet character / short read) */
} hqx_sixbit;

static void hqx_sixbit_init(hqx_sixbit *r, FILE *f)
{
    r->f = f;
    r->atend = 0;
    r->err = 0;
}

/* Next 6-bit code (0-63), or -1 at the ':' terminator / on error (check
 * r->err to tell those apart). \r and \n between codes are skipped
 * transparently (BinHex text wraps lines); any other byte outside the
 * alphabet is corrupt input. */
static int hqx_sixbit_get(hqx_sixbit *r)
{
    if (r->err || r->atend) return -1;

    for (;;) {
        int c = fgetc(r->f);
        if (c == EOF) { r->err = SIT_E_IO; return -1; }
        if (c == '\r' || c == '\n') continue;
        if (c == ':') { r->atend = 1; return -1; }

        int v = hqx_alphabet_index(c);
        if (v < 0) { r->err = SIT_E_CORRUPT; return -1; }
        return v;
    }
}

/* ------------------------------------------------------------------- */
/* layer 1b: 6-bit codes -> 8-bit bytes (3 bytes per 4 codes)           */
/* ------------------------------------------------------------------- */

typedef struct {
    hqx_sixbit six;
    uint32_t bytecount;
    int prev_bits; /* previous code's raw 6-bit value, carried across the state machine */
    int err;
} hqx_decoder;

static void hqx_decoder_init(hqx_decoder *d, FILE *f)
{
    hqx_sixbit_init(&d->six, f);
    d->bytecount = 0;
    d->prev_bits = 0;
    d->err = 0;
}

/* sit_bytesrc-shaped: produces the decoded byte stream that RLE90 sits
 * on top of. Truncation mid-header/mid-data (the six-bit stream ending
 * cleanly at ':' before enough bytes were produced) is a format error
 * here, since every caller always knows exactly how many bytes it
 * still needs. */
static int hqx_decoder_getbyte(void *ctx)
{
    hqx_decoder *d = (hqx_decoder *)ctx;
    if (d->err) return -1;

    int state = (int)(d->bytecount % 3);
    int bits1, bits2, out;

    if (state == 0) {
        bits1 = hqx_sixbit_get(&d->six);
        if (bits1 < 0) { d->err = d->six.err ? d->six.err : SIT_E_FORMAT; return -1; }
        bits2 = hqx_sixbit_get(&d->six);
        if (bits2 < 0) { d->err = d->six.err ? d->six.err : SIT_E_FORMAT; return -1; }
        out = (bits1 << 2) | (bits2 >> 4);
        d->prev_bits = bits2;
    } else if (state == 1) {
        bits1 = d->prev_bits;
        bits2 = hqx_sixbit_get(&d->six);
        if (bits2 < 0) { d->err = d->six.err ? d->six.err : SIT_E_FORMAT; return -1; }
        out = (bits1 << 4) | (bits2 >> 2);
        d->prev_bits = bits2;
    } else {
        bits1 = d->prev_bits;
        bits2 = hqx_sixbit_get(&d->six);
        if (bits2 < 0) { d->err = d->six.err ? d->six.err : SIT_E_FORMAT; return -1; }
        out = (bits1 << 6) | bits2;
    }

    d->bytecount++;
    return out & 0xFF;
}

/* ------------------------------------------------------------------- */
/* CRC-CCITT/XMODEM: poly 0x1021, init 0, MSB-first, no reflection,     */
/* no final xor. Distinct from sit_crc16.c's CRC-16/ARC (data-fork      */
/* CRCs of the StuffIt formats themselves) - BinHex uses this one for   */
/* its own three header/data/resource checksums.                       */
/* ------------------------------------------------------------------- */
static uint16_t hqx_crc_update(uint16_t crc, const uint8_t *buf, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        crc = (uint16_t)(crc ^ ((uint16_t)buf[i] << 8));
        for (int b = 0; b < 8; b++) {
            if (crc & 0x8000) crc = (uint16_t)((crc << 1) ^ 0x1021);
            else crc = (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/* ------------------------------------------------------------------- */
/* pulling exact byte counts through decoder+RLE90                      */
/* ------------------------------------------------------------------- */

static int hqx_read_n(sit_rle90_reader *rle, uint8_t *buf, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        int c = sit_rle90_reader_getbyte(rle);
        if (c < 0) return rle->err ? rle->err : SIT_E_FORMAT;
        buf[i] = (uint8_t)c;
    }
    return SIT_OK;
}

static uint32_t rbe32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static uint16_t rbe16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

/* ------------------------------------------------------------------- */
/* public API                                                            */
/* ------------------------------------------------------------------- */

int hqx_open(FILE *f, const sit_allocator *a, hqx_file *h)
{
    if (!f || !h) return SIT_E_INVAL;

    memset(h, 0, sizeof(*h));
    h->alloc = a ? a : &sit_libc_allocator;

    long base = ftell(f);
    if (base < 0) return SIT_E_IO;

    long colon_off = 0;
    int found = hqx_detect(f, h->alloc, base, &colon_off);
    if (found < 0) return found;
    if (!found) return SIT_E_FORMAT;

    if (fseek(f, colon_off + 1, SEEK_SET) != 0) return SIT_E_IO;

    hqx_decoder dec;
    hqx_decoder_init(&dec, f);
    sit_bytesrc bsrc = { hqx_decoder_getbyte, &dec };
    sit_rle90_reader rle;
    sit_rle90_reader_init(&rle, bsrc);

    uint16_t crc = 0;
    int rc;

    uint8_t namelenbyte;
    rc = hqx_read_n(&rle, &namelenbyte, 1);
    if (rc) return rc;
    int namelen = namelenbyte;
    if (namelen < 1 || namelen > 63) return SIT_E_FORMAT;
    crc = hqx_crc_update(crc, &namelenbyte, 1);

    uint8_t namebuf[63];
    rc = hqx_read_n(&rle, namebuf, (size_t)namelen);
    if (rc) return rc;
    crc = hqx_crc_update(crc, namebuf, (size_t)namelen);

    /* version(1) + type(4) + creator(4) + finder_flags(2) + datalen(4) + resourcelen(4) = 19 bytes */
    uint8_t rest[19];
    rc = hqx_read_n(&rle, rest, sizeof(rest));
    if (rc) return rc;
    crc = hqx_crc_update(crc, rest, sizeof(rest));

    uint8_t crcbuf[2];
    rc = hqx_read_n(&rle, crcbuf, sizeof(crcbuf));
    if (rc) return rc;
    uint16_t stored_header_crc = rbe16(crcbuf);
    if (stored_header_crc != crc) return SIT_E_CRC;

    uint32_t type = rbe32(rest + 1);
    uint32_t creator = rbe32(rest + 5);
    uint16_t finder_flags = rbe16(rest + 9);
    uint32_t datalen = rbe32(rest + 11);
    uint32_t resourcelen = rbe32(rest + 15);

    memcpy(h->name, namebuf, (size_t)namelen);
    h->name[namelen] = '\0';
    h->type[0] = (char)(type >> 24); h->type[1] = (char)(type >> 16);
    h->type[2] = (char)(type >> 8);  h->type[3] = (char)type; h->type[4] = '\0';
    h->creator[0] = (char)(creator >> 24); h->creator[1] = (char)(creator >> 16);
    h->creator[2] = (char)(creator >> 8);  h->creator[3] = (char)creator; h->creator[4] = '\0';
    h->finder_flags = finder_flags;
    h->data_len = datalen;
    h->resource_len = resourcelen;

    /* Always allocate at least 1 byte so hqx_data_fork()'s fmemopen()
     * never gets a NULL buffer, even for a (pathological) zero-length
     * wrapped data fork. */
    size_t want = datalen ? (size_t)datalen : 1;
    h->data = (uint8_t *)h->alloc->alloc(want, h->alloc->ctx);
    if (!h->data) return SIT_E_NOMEM;

    if (datalen) {
        rc = hqx_read_n(&rle, h->data, (size_t)datalen);
        if (rc) { h->alloc->free(h->data, h->alloc->ctx); h->data = NULL; return rc; }
    }

    uint8_t datacrcbuf[2];
    rc = hqx_read_n(&rle, datacrcbuf, sizeof(datacrcbuf));
    if (rc) { h->alloc->free(h->data, h->alloc->ctx); h->data = NULL; return rc; }

    uint16_t stored_data_crc = rbe16(datacrcbuf);
    uint16_t calc_data_crc = hqx_crc_update(0, h->data, datalen);
    if (stored_data_crc != calc_data_crc) {
        h->alloc->free(h->data, h->alloc->ctx);
        h->data = NULL;
        return SIT_E_CRC;
    }

    /* Resource fork bytes (if any) and its own trailing CRC follow, but
     * we stop here on purpose - see binhex.h / hqx_open()'s comment. */
    return SIT_OK;
}

FILE *hqx_data_fork(hqx_file *h)
{
    if (!h || !h->data) return NULL;
#ifdef _WIN32
    /* no fmemopen() in the Windows C runtime: copy into a temporary file */
    FILE *f = tmpfile();
    if (f == NULL) return NULL;
    if (fwrite(h->data, 1, h->data_len, f) != h->data_len) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    return f;
#else
    return fmemopen(h->data, h->data_len, "rb");
#endif
}

void hqx_close(hqx_file *h)
{
    if (!h) return;
    if (h->data && h->alloc) h->alloc->free(h->data, h->alloc->ctx);
    memset(h, 0, sizeof(*h));
}
