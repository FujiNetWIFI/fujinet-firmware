/*
 * ndif.c - see ndif.h.
 *
 * Ported from ndif2raw.c (https://github.com/mhjacobson/ndif2raw,
 * BSD-3-Clause, Copyright (c) 2024-2025 Individual contributors - see
 * ndif2raw's COPYRIGHT): the `bcem` header/chunk-table layout
 * (struct ndif_header / struct ndif_chunk and main()'s per-block
 * dispatch loop), adc_decompress(), and the KenCode decoder
 * (kencode_popbits/kencode_decode_copy_len/kencode_decode_lit_len/
 * kencode_decode_copy_offset/kencode_decompress). Redistribution of
 * this ported logic is under the same BSD-3-Clause terms; see
 * ndif2raw's COPYRIGHT for the full license text and disclaimer.
 * Contributor credit per ndif2raw's README: @mhjacobson wrote the
 * initial ADC support, @Windoze345 added checksum verification and
 * KenCode support.
 *
 * The classic Mac resource-fork parsing (rsrc_find_bcem() and its
 * helpers, below) has no equivalent in ndif2raw.c/resourcefork.c to
 * port from - ndif2raw's resourcefork.c reads a resource fork via the
 * (Carbon, since-removed) Mac Resource Manager APIs rather than
 * parsing the on-disk layout itself. This is an original, from-scratch
 * implementation of just enough of that well-documented on-disk format
 * (Inside Macintosh: More Macintosh Toolbox, "Resource Manager") to
 * find one resource by type: the 16-byte fork header, the resource
 * map's type list, and one type's reference list.
 *
 * Deliberately NOT ported from ndif2raw.c: its whole-file CRC-32
 * verification (header.crc32 vs. a running CRC-32 over every decoded
 * block) - a nice-to-have integrity check the reference tool only
 * warns about on mismatch, not something this API's callers need to
 * function; its command-line argument parsing and the
 * AppleSingle/AppleDouble/resource-fork input-format switch (out of
 * scope per stuffit.h - resource-fork bytes
 * arrive here as a plain in-memory buffer, already extracted by
 * sit_extract()'s fork selector); and read_data()'s whole-data-fork
 * malloc (ndif_open()/ndif_prepare_chunk() below fseek()+fread() just
 * the bytes one chunk needs, from the caller's FILE*, to keep memory
 * bounded to two fixed scratch buffers regardless of image size).
 *
 * Every assert()/abort() in the reference decoders (which assume
 * well-formed input) is replaced here with an explicit bounds check
 * that fails with a negative NDIF_E_* code instead - untrusted chunk
 * tables and compressed data must never be able to read or write past
 * an allocated buffer. The KenCode bit-reader also always widens its
 * input to a 32-bit big-endian word regardless of how many bits are
 * actually left (harmless in the reference tool, which mallocs the
 * whole data fork and so always has a few bytes of slack past any
 * chunk's declared end); ndif_open() compensates by allocating the
 * compressed scratch buffer 4 bytes larger than the largest chunk
 * needs and ndif_prepare_chunk() zeroes that padding before each
 * KenCode/ADC decode, so the same widening never reads past the
 * allocation - see the comment on NDIF_COMPBUF_PAD below for why this
 * is provably safe (the extra bits are always shifted/masked away).
 */
#include "ndif.h"

#include <string.h>

/* ------------------------------------------------------------------- */
/* constants                                                             */
/* ------------------------------------------------------------------- */

#define NDIF_BLOCK_SIZE        512u
#define NDIF_BCEM_HEADER_SIZE  128u   /* on-disk struct ndif_header size */
#define NDIF_BCEM_CHUNK_SIZE   12u    /* on-disk struct ndif_chunk size */
#define NDIF_COMPBUF_PAD       4u     /* KenCode's bit-reader widens to 32 bits at a time;
                                        * this many extra zeroed bytes past the real
                                        * compressed length let it do that safely - see
                                        * ndif_kc_popbits() below for the proof that the
                                        * padding bits are never actually used. */

enum {
    NDIF_CHUNK_ZERO       = 0,
    NDIF_CHUNK_RAW        = 2,
    NDIF_CHUNK_KENCODE    = 128,
    NDIF_CHUNK_ADC        = 131,
    NDIF_CHUNK_TERMINATOR = 255
};

#define NDIF_FOURCC(a, b, c, d) \
    (((uint32_t)(uint8_t)(a) << 24) | ((uint32_t)(uint8_t)(b) << 16) | \
     ((uint32_t)(uint8_t)(c) << 8)  |  (uint32_t)(uint8_t)(d))

/* ------------------------------------------------------------------- */
/* private chunk-table entry (chunk_count of these, allocator-owned)    */
/* ------------------------------------------------------------------- */
typedef struct {
    uint32_t logical_offset;  /* first block this chunk covers */
    uint32_t nblocks;         /* span, in blocks - computed at open time */
    uint32_t backing_offset;  /* byte offset into the data fork, relative to the header's own backing_offset */
    uint32_t backing_size;    /* compressed byte length in the data fork (0 for zero/raw) */
    uint8_t  type;
} ndif_chunk_priv;

/* ------------------------------------------------------------------- */
/* big-endian readers, bounds-checked against an explicit buffer length */
/* ------------------------------------------------------------------- */

static int rf_u16(const uint8_t *buf, size_t len, size_t off, uint16_t *out)
{
    if (off + 2 > len) return 0;
    *out = (uint16_t)(((uint16_t)buf[off] << 8) | buf[off + 1]);
    return 1;
}

static int rf_u32(const uint8_t *buf, size_t len, size_t off, uint32_t *out)
{
    if (off + 4 > len) return 0;
    *out = ((uint32_t)buf[off] << 24) | ((uint32_t)buf[off + 1] << 16) |
           ((uint32_t)buf[off + 2] << 8) | (uint32_t)buf[off + 3];
    return 1;
}

/* Unchecked BE readers over a range already validated by the caller
 * (used once the bcem resource's total length has been confirmed to
 * cover the fixed header and every chunk-table entry). */
static uint16_t peek_be16(const uint8_t *p) { return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }
static uint32_t peek_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* ------------------------------------------------------------------- */
/* classic Mac resource fork parsing - see ndif.c's top comment         */
/* ------------------------------------------------------------------- */

/* Finds the first resource of type `want_type` in the resource fork
 * `rsrc` (rsrc_len bytes). On success, returns 1 and sets *out_off /
 * *out_len to the resource's data bytes (a sub-range of rsrc, not
 * copied). Returns 0 if the fork is well-formed but has no resource of
 * that type, or NDIF_E_FORMAT if the fork itself is not well-formed
 * enough to say either way (every offset/length here is validated
 * against rsrc_len before use - a hostile/corrupt resource fork can
 * only ever fail this function, never read past its buffer). */
static int rsrc_find(const uint8_t *rsrc, size_t rsrc_len, uint32_t want_type,
                      size_t *out_off, size_t *out_len)
{
    uint32_t data_off, map_off, data_len, map_len;
    if (!rf_u32(rsrc, rsrc_len, 0, &data_off)) return NDIF_E_FORMAT;
    if (!rf_u32(rsrc, rsrc_len, 4, &map_off)) return NDIF_E_FORMAT;
    if (!rf_u32(rsrc, rsrc_len, 8, &data_len)) return NDIF_E_FORMAT;
    if (!rf_u32(rsrc, rsrc_len, 12, &map_len)) return NDIF_E_FORMAT;

    if ((uint64_t)data_off + data_len > (uint64_t)rsrc_len) return NDIF_E_FORMAT;
    if ((uint64_t)map_off + map_len > (uint64_t)rsrc_len) return NDIF_E_FORMAT;
    if (map_len < 28) return NDIF_E_FORMAT; /* 16 (header copy) + 4 (next map) + 2 (refnum) + 2 (attrs) + 2 (type list off) + 2 (name list off) */

    uint16_t type_list_off;
    if (!rf_u16(rsrc, rsrc_len, (size_t)map_off + 24, &type_list_off)) return NDIF_E_FORMAT;

    size_t tl = (size_t)map_off + type_list_off;
    uint16_t ntypes_m1;
    if (!rf_u16(rsrc, rsrc_len, tl, &ntypes_m1)) return NDIF_E_FORMAT;
    uint32_t ntypes = (ntypes_m1 == 0xFFFFu) ? 0u : (uint32_t)ntypes_m1 + 1u;

    size_t p = tl + 2;
    for (uint32_t i = 0; i < ntypes; i++) {
        uint32_t type;
        uint16_t nres_m1, reflist_off;
        if (!rf_u32(rsrc, rsrc_len, p, &type)) return NDIF_E_FORMAT;
        if (!rf_u16(rsrc, rsrc_len, p + 4, &nres_m1)) return NDIF_E_FORMAT;
        if (!rf_u16(rsrc, rsrc_len, p + 6, &reflist_off)) return NDIF_E_FORMAT;

        if (type == want_type) {
            uint32_t nres = (uint32_t)nres_m1 + 1u;
            size_t rl = tl + reflist_off;

            for (uint32_t j = 0; j < nres; j++) {
                size_t e = rl + (size_t)j * 12u;
                uint32_t packed; /* 1 byte attributes + 3-byte data offset, on-disk */
                if (!rf_u32(rsrc, rsrc_len, e + 4, &packed)) return NDIF_E_FORMAT;

                uint32_t data_rel_off = packed & 0x00FFFFFFu;
                uint64_t abs_off = (uint64_t)data_off + data_rel_off;
                if (abs_off > (uint64_t)rsrc_len) return NDIF_E_FORMAT;

                uint32_t reslen;
                if (!rf_u32(rsrc, rsrc_len, (size_t)abs_off, &reslen)) return NDIF_E_FORMAT;

                uint64_t data_start = abs_off + 4;
                if (data_start + reslen > (uint64_t)rsrc_len) return NDIF_E_FORMAT;

                *out_off = (size_t)data_start;
                *out_len = (size_t)reslen;
                return 1;
            }
            return 0; /* type present with (nres==0, impossible per the -1 encoding) - not found */
        }

        p += 8;
    }

    return 0;
}

int ndif_probe(const uint8_t *rsrc, size_t rsrc_len)
{
    if (!rsrc) return 0;
    size_t off, len;
    return rsrc_find(rsrc, rsrc_len, NDIF_FOURCC('b', 'c', 'e', 'm'), &off, &len) == 1;
}

/* ------------------------------------------------------------------- */
/* ADC decompressor (ported from ndif2raw.c's adc_decompress())         */
/* ------------------------------------------------------------------- */

static int ndif_adc_decompress(const uint8_t *src, size_t srclen,
                                uint8_t *dst, size_t dstlen, size_t *out_len)
{
    const uint8_t *ptr = src;
    const uint8_t *send = src + srclen;
    uint8_t *dstptr = dst;
    uint8_t *dend = dst + dstlen;

    memset(dst, 0, dstlen);

    while (ptr < send) {
        if (dstptr >= dend) return NDIF_E_CORRUPT;
        uint8_t tag = *ptr;
        size_t len;

        if (tag & 0x80) {
            /* literal run */
            len = (size_t)(tag & 0x7F) + 1;
            ptr += 1;
            if ((size_t)(send - ptr) < len) return NDIF_E_CORRUPT;
            if ((size_t)(dend - dstptr) < len) return NDIF_E_CORRUPT;
            memcpy(dstptr, ptr, len);
            ptr += len;
            dstptr += len;
        } else if (tag & 0x40) {
            /* relative copy, 16-bit offset */
            len = (size_t)(tag & 0x3F) + 4;
            ptr += 1;
            if ((size_t)(send - ptr) < 2) return NDIF_E_CORRUPT;
            uint32_t offset = (uint32_t)(((uint32_t)ptr[0] << 8) | ptr[1]) + 1u;
            ptr += 2;
            if (offset > (size_t)(dstptr - dst)) return NDIF_E_CORRUPT;
            if ((size_t)(dend - dstptr) < len) return NDIF_E_CORRUPT;
            for (size_t i = 0; i < len; i++) dstptr[i] = dstptr[i - offset];
            dstptr += len;
        } else {
            /* relative copy, 10-bit offset */
            if ((size_t)(send - ptr) < 2) return NDIF_E_CORRUPT;
            len = (size_t)(tag >> 2) + 3;
            uint32_t raw16 = (uint32_t)(((uint32_t)ptr[0] << 8) | ptr[1]);
            uint32_t offset = (raw16 & 0x3FFu) + 1u;
            ptr += 2;
            if (offset > (size_t)(dstptr - dst)) return NDIF_E_CORRUPT;
            if ((size_t)(dend - dstptr) < len) return NDIF_E_CORRUPT;
            for (size_t i = 0; i < len; i++) dstptr[i] = dstptr[i - offset];
            dstptr += len;
        }
    }

    *out_len = (size_t)(dstptr - dst);
    return NDIF_OK;
}

/* ------------------------------------------------------------------- */
/* KenCode decompressor (ported from ndif2raw.c's kencode_* functions)  */
/* ------------------------------------------------------------------- */

typedef struct {
    size_t node_count;
    const uint8_t *src_buf;
    size_t src_bitlen;
    size_t src_bitpos;
    int err; /* set to 1 the moment input is exhausted early or a value is out of range;
              * every caller checks this immediately after each call and aborts the
              * whole decode with NDIF_E_CORRUPT rather than trusting whatever
              * (bounded, but meaningless) value came back. */
} ndif_kc_state;

/* Reads bit_len (<=32) bits starting at src_bitpos as a big-endian bit
 * string. Always widens to a 32-bit word first (matching the
 * reference decoder) for the shift/mask below, but only after
 * confirming src_bitpos+bit_len <= src_bitlen - the extra bits that
 * widening may pull in beyond src_bitlen therefore always land at or
 * past bit position src_bitlen, are always shifted out or masked away,
 * and are never part of the returned value. ndif_open()/
 * ndif_prepare_chunk() guarantee src_buf has NDIF_COMPBUF_PAD zeroed
 * bytes available past the declared compressed length, so the 32-bit
 * widening read itself never runs past the allocation. */
static uint32_t ndif_kc_popbits(ndif_kc_state *st, size_t bit_len)
{
    if (bit_len == 0) return 0;
    if (st->err || bit_len > 32 || st->src_bitpos + bit_len > st->src_bitlen) {
        st->err = 1;
        return 0;
    }

    size_t cur_bitpos = st->src_bitpos;
    st->src_bitpos += bit_len;
    const uint8_t *p = st->src_buf + (cur_bitpos / 8);
    uint32_t word = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
    uint32_t mask = (bit_len == 32) ? 0xFFFFFFFFu : ((1u << bit_len) - 1u);
    return (word >> (32 - bit_len - (cur_bitpos & 7))) & mask;
}

static size_t ndif_kc_decode_copy_len(ndif_kc_state *st)
{
    size_t len_idx = 0;
    while (len_idx < 10 && ndif_kc_popbits(st, 1) != 0) len_idx += 1;
    if (st->err) return 0;

    switch (len_idx) {
        case 0: return ndif_kc_popbits(st, 1);
        case 1:
            if (!ndif_kc_popbits(st, 1)) return 2;
            return ndif_kc_popbits(st, 1) + 3;
        case 2:
            if (ndif_kc_popbits(st, 1)) return ndif_kc_popbits(st, 2) + 7;
            return ndif_kc_popbits(st, 1) + 5;
        case 3: return ndif_kc_popbits(st, 3) + 11;
        case 4: return ndif_kc_popbits(st, 3) + 19;
        case 5: return ndif_kc_popbits(st, 5) + 27;
        case 6: return ndif_kc_popbits(st, 6) + 59;
        case 7: return ndif_kc_popbits(st, 7) + 123;
        case 8: return ndif_kc_popbits(st, 8) + 251;
        case 9: return ndif_kc_popbits(st, 9) + 507;
        default: return ndif_kc_popbits(st, 10) + 1019;
    }
}

static size_t ndif_kc_decode_lit_len(ndif_kc_state *st)
{
    if (!ndif_kc_popbits(st, 1)) return 1;

    switch (ndif_kc_popbits(st, 2)) {
        case 0: return 2;
        case 1: return 3;
        case 2: return ndif_kc_popbits(st, 2) + 4;
        case 3: {
            size_t read_bits = ndif_kc_popbits(st, 4);
            if (read_bits < 8) return read_bits + 8;
            if (read_bits < 12) return ndif_kc_popbits(st, 2) + (read_bits * 4) - 16;
            return ndif_kc_popbits(st, 3) + (read_bits * 8) - 64;
        }
        default:
            st->err = 1; /* unreachable: popbits(2) is in [0,3] - kept for -Wall/-Wextra and defense */
            return 0;
    }
}

static size_t ndif_kc_decode_copy_offset(ndif_kc_state *st, size_t dst_pos)
{
    size_t bit_len = 0;
    if (dst_pos > 172032 && st->node_count > 131072) bit_len = 14;
    else if (dst_pos > 70000 && st->node_count > 65536) bit_len = 13;
    else if (dst_pos > 43008 && st->node_count > 32768) bit_len = 12;
    else if (dst_pos > 21504 && st->node_count > 16384) bit_len = 11;
    else if (dst_pos > 10752 && st->node_count > 8192) bit_len = 10;
    else if (dst_pos > 5376 && st->node_count > 4096) bit_len = 9;
    else if (dst_pos > 2688 && st->node_count > 2048) bit_len = 8;
    else if (dst_pos > 1000) bit_len = 7;
    else if (dst_pos > 672) bit_len = 6;
    else if (dst_pos > 160) bit_len = 5;
    else if (dst_pos > 80) bit_len = 4;
    else if (dst_pos > 40) bit_len = 3;
    else if (dst_pos > 20) bit_len = 2;
    else if (dst_pos > 10) bit_len = 1;

    if (!ndif_kc_popbits(st, 1)) return ndif_kc_popbits(st, bit_len) + 1;

    size_t base_len = (size_t)1 << bit_len;

    if (ndif_kc_popbits(st, 1)) {
        base_len = 5 * base_len + 1;

        if (base_len + 1 >= dst_pos) return base_len + ndif_kc_popbits(st, 1);
        if (base_len + 3 >= dst_pos) return base_len + ndif_kc_popbits(st, 2);

        size_t j = base_len + 3;
        for (size_t i = 3; i <= bit_len + 4; i++) {
            j += ((size_t)1 << (i - 1));
            size_t k = (j != 1664) ? j : 1644;
            if (k >= dst_pos || i == bit_len + 4) return base_len + ndif_kc_popbits(st, i);
        }
        /* unreachable: the loop above always returns at i == bit_len+4 */
        st->err = 1;
        return 0;
    }

    return base_len + ndif_kc_popbits(st, bit_len + 2) + 1;
}

/* dst_pos may legitimately equal dstlen when the very last op exactly
 * fills the buffer; every write is bounds-checked against dstlen
 * before it happens, matching the assert()s in the reference decoder. */
static int ndif_kencode_decompress(const uint8_t *src, size_t srclen,
                                    uint8_t *dst, size_t dstlen, size_t *out_len)
{
    ndif_kc_state st;
    st.node_count = 10240; /* fixed for NDIF images, per ndif2raw.c */
    st.src_buf = src;
    st.src_bitlen = srclen * 8;
    st.src_bitpos = 0;
    st.err = 0;

    int allow_lit = 1;
    size_t dst_pos = 0;

    while (dst_pos < dstlen && st.src_bitpos < st.src_bitlen) {
        size_t copy_len = ndif_kc_decode_copy_len(&st);
        if (st.err) return NDIF_E_CORRUPT;

        if (copy_len == 0 && allow_lit) {
            size_t lit_len = ndif_kc_decode_lit_len(&st);
            if (st.err) return NDIF_E_CORRUPT;
            if (st.src_bitpos + lit_len * 8 > st.src_bitlen) return NDIF_E_CORRUPT;
            if (dst_pos + lit_len > dstlen) return NDIF_E_CORRUPT;

            const uint8_t *src_ptr = st.src_buf + (st.src_bitpos / 8);
            if ((st.src_bitpos & 7) == 0) {
                memcpy(dst + dst_pos, src_ptr, lit_len);
            } else {
                for (size_t i = 0; i < lit_len; i++) {
                    uint16_t w = (uint16_t)(((uint16_t)src_ptr[0] << 8) | src_ptr[1]);
                    src_ptr += 1;
                    dst[dst_pos + i] = (uint8_t)((w >> (8 - (st.src_bitpos & 7))) & 0xFF);
                }
            }

            dst_pos += lit_len;
            st.src_bitpos += lit_len * 8;
            allow_lit = (lit_len > 62);
        } else {
            copy_len += allow_lit ? 2 : 3;
            if (dst_pos + copy_len > dstlen) return NDIF_E_CORRUPT;

            size_t copy_offset = ndif_kc_decode_copy_offset(&st, dst_pos);
            if (st.err) return NDIF_E_CORRUPT;
            if (copy_offset == 0 || copy_offset > dst_pos) return NDIF_E_CORRUPT;

            for (size_t i = 0; i < copy_len; i++) dst[dst_pos + i] = dst[dst_pos + i - copy_offset];
            dst_pos += copy_len;
            allow_lit = 1;
        }
    }

    *out_len = dst_pos;
    return NDIF_OK;
}

/* ------------------------------------------------------------------- */
/* bcem header / chunk table parsing                                    */
/* ------------------------------------------------------------------- */

static int ndif_parse_bcem(ndif_image *img, const uint8_t *bcem, size_t bcem_len)
{
    if (bcem_len < NDIF_BCEM_HEADER_SIZE) return NDIF_E_FORMAT;

    uint16_t version = peek_be16(bcem + 0);
    uint32_t nblock = peek_be32(bcem + 68);
    uint32_t max_chunk_size_blocks = peek_be32(bcem + 72);
    uint32_t backing_offset = peek_be32(bcem + 76);
    uint32_t nchunk_total = peek_be32(bcem + 124);

    if (version < 10 || version > 12) return NDIF_E_FORMAT; /* version 2's layout differs entirely */
    if (max_chunk_size_blocks == 0 || nchunk_total == 0) return NDIF_E_FORMAT;

    uint64_t need = (uint64_t)NDIF_BCEM_HEADER_SIZE + (uint64_t)nchunk_total * NDIF_BCEM_CHUNK_SIZE;
    if (need > (uint64_t)bcem_len) return NDIF_E_FORMAT;

    ndif_chunk_priv *chunks =
        (ndif_chunk_priv *)img->priv_alloc.alloc(sizeof(ndif_chunk_priv) * (size_t)nchunk_total, img->priv_alloc.ctx);
    if (!chunks) return NDIF_E_NOMEM;

    const uint8_t *cp = bcem + NDIF_BCEM_HEADER_SIZE;
    for (uint32_t i = 0; i < nchunk_total; i++) {
        const uint8_t *e = cp + (size_t)i * NDIF_BCEM_CHUNK_SIZE;
        uint32_t lo_and_type = peek_be32(e);
        chunks[i].logical_offset = (lo_and_type >> 8) & 0x00FFFFFFu;
        chunks[i].type = (uint8_t)(lo_and_type & 0xFFu);
        chunks[i].backing_offset = peek_be32(e + 4);
        chunks[i].backing_size = peek_be32(e + 8);
        chunks[i].nblocks = 0;
    }

    uint32_t real_count = nchunk_total;
    if (chunks[nchunk_total - 1].type == NDIF_CHUNK_TERMINATOR) real_count = nchunk_total - 1;
    if (real_count == 0) {
        img->priv_alloc.free(chunks, img->priv_alloc.ctx);
        return NDIF_E_FORMAT;
    }

    uint64_t max_span_bytes = (uint64_t)max_chunk_size_blocks * NDIF_BLOCK_SIZE;
    uint32_t max_comp_size = 0;

    for (uint32_t i = 0; i < real_count; i++) {
        uint32_t end_block = (i + 1 < real_count) ? chunks[i + 1].logical_offset : nblock;
        if (end_block < chunks[i].logical_offset) {
            img->priv_alloc.free(chunks, img->priv_alloc.ctx);
            return NDIF_E_FORMAT;
        }
        chunks[i].nblocks = end_block - chunks[i].logical_offset;

        uint64_t span_bytes = (uint64_t)chunks[i].nblocks * NDIF_BLOCK_SIZE;
        if (span_bytes > max_span_bytes) max_span_bytes = span_bytes;

        switch (chunks[i].type) {
            case NDIF_CHUNK_ZERO:
            case NDIF_CHUNK_RAW:
                break;
            case NDIF_CHUNK_KENCODE:
            case NDIF_CHUNK_ADC:
                if (chunks[i].backing_size > max_comp_size) max_comp_size = chunks[i].backing_size;
                break;
            default:
                img->priv_alloc.free(chunks, img->priv_alloc.ctx);
                return NDIF_E_UNSUPPORTED_CHUNK;
        }
    }

    size_t decompbuf_cap = (size_t)max_span_bytes;
    if (decompbuf_cap == 0) decompbuf_cap = 1;
    size_t compbuf_cap = (size_t)max_comp_size + NDIF_COMPBUF_PAD;

    uint8_t *decompbuf = (uint8_t *)img->priv_alloc.alloc(decompbuf_cap, img->priv_alloc.ctx);
    uint8_t *compbuf = (uint8_t *)img->priv_alloc.alloc(compbuf_cap, img->priv_alloc.ctx);
    if (!decompbuf || !compbuf) {
        if (decompbuf) img->priv_alloc.free(decompbuf, img->priv_alloc.ctx);
        if (compbuf) img->priv_alloc.free(compbuf, img->priv_alloc.ctx);
        img->priv_alloc.free(chunks, img->priv_alloc.ctx);
        return NDIF_E_NOMEM;
    }

    img->block_count = nblock;
    img->chunk_count = real_count;
    img->priv_backing_offset = backing_offset;
    img->priv_chunks = chunks;
    img->priv_decompbuf = decompbuf;
    img->priv_decompbuf_cap = decompbuf_cap;
    img->priv_compbuf = compbuf;
    img->priv_compbuf_cap = compbuf_cap;
    return NDIF_OK;
}

/* ------------------------------------------------------------------- */
/* public API                                                            */
/* ------------------------------------------------------------------- */

int ndif_open(ndif_image *img, FILE *data, const uint8_t *rsrc, size_t rsrc_len, const sit_allocator *a)
{
    if (!img || !rsrc || !a || !a->alloc || !a->free) return NDIF_E_INVAL;

    memset(img, 0, sizeof(*img));
    img->priv_data = data;
    img->priv_alloc = *a;

    size_t bcem_off, bcem_len;
    int rc = rsrc_find(rsrc, rsrc_len, NDIF_FOURCC('b', 'c', 'e', 'm'), &bcem_off, &bcem_len);
    if (rc < 0) return rc;
    if (rc == 0) return NDIF_E_FORMAT;

    return ndif_parse_bcem(img, rsrc + bcem_off, bcem_len);
}

/* Decodes chunk `idx` into img->priv_decompbuf; *out_len receives the
 * number of valid bytes (chunks[idx].nblocks * 512). */
static int ndif_prepare_chunk(ndif_image *img, uint32_t idx, size_t *out_len)
{
    ndif_chunk_priv *chunks = (ndif_chunk_priv *)img->priv_chunks;
    ndif_chunk_priv *c = &chunks[idx];
    size_t span_bytes = (size_t)c->nblocks * NDIF_BLOCK_SIZE;

    if (span_bytes > img->priv_decompbuf_cap) return NDIF_E_CORRUPT;

    switch (c->type) {
        case NDIF_CHUNK_ZERO:
            memset(img->priv_decompbuf, 0, span_bytes);
            break;

        case NDIF_CHUNK_RAW: {
            long abs_off = (long)img->priv_backing_offset + (long)c->backing_offset;
            if (fseek(img->priv_data, abs_off, SEEK_SET) != 0) return NDIF_E_IO;
            if (span_bytes > 0) {
                size_t got = fread(img->priv_decompbuf, 1, span_bytes, img->priv_data);
                if (got != span_bytes) return NDIF_E_IO;
            }
            break;
        }

        case NDIF_CHUNK_KENCODE:
        case NDIF_CHUNK_ADC: {
            if ((size_t)c->backing_size + NDIF_COMPBUF_PAD > img->priv_compbuf_cap) return NDIF_E_CORRUPT;

            long abs_off = (long)img->priv_backing_offset + (long)c->backing_offset;
            if (fseek(img->priv_data, abs_off, SEEK_SET) != 0) return NDIF_E_IO;
            if (c->backing_size > 0) {
                size_t got = fread(img->priv_compbuf, 1, c->backing_size, img->priv_data);
                if (got != c->backing_size) return NDIF_E_IO;
            }
            memset(img->priv_compbuf + c->backing_size, 0, NDIF_COMPBUF_PAD);

            size_t produced = 0;
            int rc = (c->type == NDIF_CHUNK_ADC)
                ? ndif_adc_decompress(img->priv_compbuf, c->backing_size, img->priv_decompbuf, span_bytes, &produced)
                : ndif_kencode_decompress(img->priv_compbuf, c->backing_size, img->priv_decompbuf, span_bytes, &produced);
            if (rc != NDIF_OK) return rc;
            if (produced != span_bytes) return NDIF_E_CORRUPT;
            break;
        }

        default:
            /* Cannot happen: ndif_open() rejects unsupported chunk types up front. */
            return NDIF_E_UNSUPPORTED_CHUNK;
    }

    if (out_len) *out_len = span_bytes;
    return NDIF_OK;
}

int ndif_extract(ndif_image *img, int (*sink)(const uint8_t *buf, size_t n, void *ctx), void *ctx)
{
    if (!img || !sink) return NDIF_E_INVAL;

    for (uint32_t i = 0; i < img->chunk_count; i++) {
        size_t len = 0;
        int rc = ndif_prepare_chunk(img, i, &len);
        if (rc != NDIF_OK) return rc;

        if (len > 0) {
            if (sink(img->priv_decompbuf, len, ctx) != 0) return NDIF_E_IO;
        }
    }

    return NDIF_OK;
}

int ndif_read_blocks(ndif_image *img, uint32_t block, uint32_t nblocks, uint8_t *out)
{
    if (!img || !out) return NDIF_E_INVAL;
    if (nblocks == 0) return NDIF_OK;
    if ((uint64_t)block + nblocks > (uint64_t)img->block_count) return NDIF_E_INVAL;

    ndif_chunk_priv *chunks = (ndif_chunk_priv *)img->priv_chunks;

    uint32_t idx = 0;
    while (idx < img->chunk_count &&
           !(block >= chunks[idx].logical_offset && block < chunks[idx].logical_offset + chunks[idx].nblocks)) {
        idx++;
    }
    if (idx >= img->chunk_count) return NDIF_E_CORRUPT;

    uint32_t cur = block;
    uint32_t remaining = nblocks;
    uint8_t *outp = out;

    while (remaining > 0) {
        if (idx >= img->chunk_count) return NDIF_E_CORRUPT;

        size_t len = 0;
        int rc = ndif_prepare_chunk(img, idx, &len);
        if (rc != NDIF_OK) return rc;

        uint32_t chunk_start = chunks[idx].logical_offset;
        uint32_t chunk_nblocks = chunks[idx].nblocks;
        uint32_t intra = cur - chunk_start;
        uint32_t avail = chunk_nblocks - intra;
        uint32_t take = (remaining < avail) ? remaining : avail;

        memcpy(outp, img->priv_decompbuf + (size_t)intra * NDIF_BLOCK_SIZE, (size_t)take * NDIF_BLOCK_SIZE);

        outp += (size_t)take * NDIF_BLOCK_SIZE;
        cur += take;
        remaining -= take;
        idx++;
    }

    return NDIF_OK;
}

void ndif_close(ndif_image *img)
{
    if (!img) return;
    if (img->priv_chunks) img->priv_alloc.free(img->priv_chunks, img->priv_alloc.ctx);
    if (img->priv_compbuf) img->priv_alloc.free(img->priv_compbuf, img->priv_alloc.ctx);
    if (img->priv_decompbuf) img->priv_alloc.free(img->priv_decompbuf, img->priv_alloc.ctx);
    memset(img, 0, sizeof(*img));
}

const char *ndif_strerror(int code)
{
    switch (code) {
        case NDIF_OK:                    return "success";
        case NDIF_E_IO:                  return "I/O error reading the data fork";
        case NDIF_E_FORMAT:              return "not a well-formed resource fork, or no 'bcem' resource";
        case NDIF_E_CORRUPT:             return "corrupt bcem chunk table or compressed chunk data";
        case NDIF_E_UNSUPPORTED_CHUNK:   return "unsupported bcem chunk type";
        case NDIF_E_NOMEM:               return "out of memory";
        case NDIF_E_INVAL:               return "invalid argument";
        default:                         return "unknown ndif error";
    }
}
