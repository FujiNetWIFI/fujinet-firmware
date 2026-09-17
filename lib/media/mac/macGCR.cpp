#include "macGCR.h"

#include <string.h>

// Apple 6-and-2 GCR code table: 6-bit value -> disk byte
static const uint8_t gcr6_table[64] = {
    0x96, 0x97, 0x9a, 0x9b, 0x9d, 0x9e, 0x9f, 0xa6,
    0xa7, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb2, 0xb3,
    0xb4, 0xb5, 0xb6, 0xb7, 0xb9, 0xba, 0xbb, 0xbc,
    0xbd, 0xbe, 0xbf, 0xcb, 0xcd, 0xce, 0xcf, 0xd3,
    0xd6, 0xd7, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde,
    0xdf, 0xe5, 0xe6, 0xe7, 0xe9, 0xea, 0xeb, 0xec,
    0xed, 0xee, 0xef, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6,
    0xf7, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff,
};

// Five self-sync "bytes" (1111111100) packed into six disk bytes.
static const uint8_t self_sync[6] = {0xff, 0x3f, 0xcf, 0xf3, 0xfc, 0xff};

static const uint8_t address_mark[3] = {0xd5, 0xaa, 0x96};
static const uint8_t data_mark[3]    = {0xd5, 0xaa, 0xad};
static const uint8_t address_epilogue[4] = {0xde, 0xaa, 0xff, 0xff};
static const uint8_t data_epilogue[3]    = {0xde, 0xaa, 0xff};

// sync groups before each address mark; a real drive writes roughly
// 40 bytes of sync between sectors.
#define SYNC_GROUPS_BEFORE_ADDRESS 5
#define SYNC_GROUPS_BEFORE_DATA    1

static const int zone_sectors[MAC_GCR_ZONES] = {12, 11, 10, 9, 8};

// Bits per revolution per zone. The drive turns at 394/429/472/525/590
// rpm in the five zones with a ~2 us bit cell. These figures are taken
// from an Applesauce MOOF capture of a real 400K disk, rounded down to
// a whole byte, so our tracks are exactly as long as real ones.
static const uint32_t zone_bits[MAC_GCR_ZONES] = {74432, 68272, 61880, 55688, 49312};

static inline int zone_of(int cyl)
{
    if (cyl < 0) cyl = 0;
    if (cyl >= MAC_GCR_CYLINDERS) cyl = MAC_GCR_CYLINDERS - 1;
    return cyl / 16;
}

int mac_gcr_sectors_per_track(int cyl)
{
    return zone_sectors[zone_of(cyl)];
}

int mac_gcr_sectors_before_cyl(int cyl)
{
    int n = 0;
    for (int c = 0; c < cyl && c < MAC_GCR_CYLINDERS; c++)
        n += zone_sectors[zone_of(c)];
    return n;
}

uint32_t mac_gcr_track_bits(int cyl)
{
    return zone_bits[zone_of(cyl)];
}

size_t mac_gcr_track_bytes(int cyl)
{
    return (mac_gcr_track_bits(cyl) + 7) / 8;
}

uint8_t mac_gcr_encode6(uint8_t v)
{
    return gcr6_table[v & 0x3f];
}

/*
 * Sony sector encoding: the 524 bytes are split into three interleaved
 * streams, each byte is XORed with a running three-byte checksum, and the
 * result is regrouped into 6-bit values (three 6-bit values plus one
 * value carrying the three top bit pairs). Same algorithm as the ROM's
 * Sony driver, MAME and FluxEngine/greaseweazle.
 */
void mac_gcr_encode_sector(const uint8_t *in, uint8_t *out)
{
    const int lookup_len = MAC_GCR_SECTOR_TOTAL / 3; // 174
    uint8_t b1[lookup_len + 1];
    uint8_t b2[lookup_len + 1];
    uint8_t b3[lookup_len + 1];
    const uint8_t *p = in;
    uint32_t c1 = 0, c2 = 0, c3 = 0;

    for (int j = 0;; j++)
    {
        c1 = (c1 & 0xff) << 1;
        if (c1 & 0x100)
            c1++;

        uint8_t val = *p++;
        c3 += val;
        if (c1 & 0x100)
        {
            c3++;
            c1 &= 0xff;
        }
        b1[j] = (val ^ c1) & 0xff;

        val = *p++;
        c2 += val;
        if (c3 > 0xff)
        {
            c2++;
            c3 &= 0xff;
        }
        b2[j] = (val ^ c3) & 0xff;

        if ((p - in) == MAC_GCR_SECTOR_TOTAL)
            break;

        val = *p++;
        c1 += val;
        if (c2 > 0xff)
        {
            c1++;
            c2 &= 0xff;
        }
        b3[j] = (val ^ c2) & 0xff;
    }
    uint8_t c4 = ((c1 & 0xc0) >> 6) | ((c2 & 0xc0) >> 4) | ((c3 & 0xc0) >> 2);
    b3[lookup_len] = 0;

    for (int i = 0; i <= lookup_len; i++)
    {
        uint8_t w1 = b1[i] & 0x3f;
        uint8_t w2 = b2[i] & 0x3f;
        uint8_t w3 = b3[i] & 0x3f;
        uint8_t w4 = ((b1[i] & 0xc0) >> 2) | ((b2[i] & 0xc0) >> 4) | ((b3[i] & 0xc0) >> 6);

        *out++ = w4;
        *out++ = w1;
        *out++ = w2;
        if (i != lookup_len)
            *out++ = w3;
    }

    *out++ = c4 & 0x3f;
    *out++ = c3 & 0x3f;
    *out++ = c2 & 0x3f;
    *out++ = c1 & 0x3f;
}

// physical order of logical sectors on a track for the given interleave
static void sector_order(int nsec, int interleave, int *order)
{
    for (int i = 0; i < nsec; i++)
        order[i] = -1;
    for (int i = 0, pos = 0; i < nsec; i++)
    {
        while (order[pos] != -1)
            pos = (pos + 1) % nsec;
        order[pos] = i;
        pos = (pos + interleave) % nsec;
    }
}

struct track_writer
{
    uint8_t *buf;
    size_t cap;
    size_t pos;

    void put(uint8_t b) { if (pos < cap) buf[pos] = b; pos++; }
    void put(const uint8_t *p, size_t n) { while (n--) put(*p++); }
    void sync(int groups) { while (groups--) put(self_sync, sizeof(self_sync)); }
};

uint32_t mac_gcr_encode_track(const uint8_t *sectors, const uint8_t *tags,
                              int cyl, int side, uint8_t format,
                              uint8_t *out, size_t out_size)
{
    const int nsec = mac_gcr_sectors_per_track(cyl);
    const uint32_t nbits = mac_gcr_track_bits(cyl);
    const size_t nbytes = nbits / 8;
    int interleave = format & 0x1f;
    if (interleave < 1)
        interleave = 1;

    if (out == NULL || out_size < nbytes)
        return 0;

    memset(out, 0xff, nbytes);
    track_writer w = {out, nbytes, 0};

    // physical order of logical sectors on the track
    int order[MAC_GCR_MAX_SECTORS];
    sector_order(nsec, interleave, order);

    uint8_t sec524[MAC_GCR_SECTOR_TOTAL];
    uint8_t enc[MAC_GCR_ENC_SECTOR];

    for (int i = 0; i < nsec; i++)
    {
        int sec = order[i];

        // ---- address field ----
        uint8_t hdr[5];
        hdr[0] = cyl & 0x3f;
        hdr[1] = (uint8_t)sec;
        hdr[2] = (uint8_t)(((side & 1) << 5) | ((cyl >> 6) & 0x1f));
        hdr[3] = format;
        hdr[4] = hdr[0] ^ hdr[1] ^ hdr[2] ^ hdr[3];

        w.sync(SYNC_GROUPS_BEFORE_ADDRESS);
        w.put(address_mark, sizeof(address_mark));
        for (int k = 0; k < 5; k++)
            w.put(mac_gcr_encode6(hdr[k]));
        w.put(address_epilogue, sizeof(address_epilogue));

        // ---- data field ----
        if (tags)
            memcpy(sec524, tags + sec * MAC_GCR_TAG_SIZE, MAC_GCR_TAG_SIZE);
        else
            memset(sec524, 0, MAC_GCR_TAG_SIZE);
        memcpy(sec524 + MAC_GCR_TAG_SIZE, sectors + sec * MAC_GCR_SECTOR_SIZE, MAC_GCR_SECTOR_SIZE);

        mac_gcr_encode_sector(sec524, enc);

        w.sync(SYNC_GROUPS_BEFORE_DATA);
        w.put(data_mark, sizeof(data_mark));
        w.put(mac_gcr_encode6((uint8_t)sec));
        for (int k = 0; k < MAC_GCR_ENC_SECTOR; k++)
            w.put(mac_gcr_encode6(enc[k]));
        w.put(data_epilogue, sizeof(data_epilogue));
    }

    // The rest of the track, up to the index, is sync. Whole sync groups
    // keep the 10-bit self-sync framing intact; any odd remainder is
    // already 0xff from the memset.
    while (w.pos + sizeof(self_sync) <= nbytes)
        w.sync(1);

    if (w.pos > nbytes)
        return 0; // sectors did not fit; caller treats as failure

    return nbits;
}

/* ------------------------------------------------------------------------
 * Write support: decoding and in-place patching
 * ---------------------------------------------------------------------- */

int mac_gcr_decode6(uint8_t b)
{
    static int8_t inverse[256];
    static bool init = false;
    if (!init)
    {
        memset(inverse, -1, sizeof(inverse));
        for (int i = 0; i < 64; i++)
            inverse[gcr6_table[i]] = (int8_t)i;
        init = true;
    }
    return inverse[b];
}

int mac_gcr_decode_sector(const uint8_t *in703, uint8_t *out524)
{
    const int lookup_len = MAC_GCR_SECTOR_TOTAL / 3;
    uint8_t b1[lookup_len + 1], b2[lookup_len + 1], b3[lookup_len + 1];
    const uint8_t *in = in703;

    for (int i = 0; i <= lookup_len; i++)
    {
        uint8_t w4 = *in++;
        uint8_t w1 = *in++;
        uint8_t w2 = *in++;
        uint8_t w3 = (i != lookup_len) ? *in++ : 0;
        b1[i] = (w1 & 0x3f) | ((w4 << 2) & 0xc0);
        b2[i] = (w2 & 0x3f) | ((w4 << 4) & 0xc0);
        b3[i] = (w3 & 0x3f) | ((w4 << 6) & 0xc0);
    }

    uint32_t c1 = 0, c2 = 0, c3 = 0;
    uint8_t *out = out524;
    for (int count = 0;; count++)
    {
        c1 = (c1 & 0xff) << 1;
        if (c1 & 0x100)
            c1++;
        uint8_t val = b1[count] ^ c1;
        c3 += val;
        if (c1 & 0x100) { c3++; c1 &= 0xff; }
        *out++ = val;

        val = b2[count] ^ c3;
        c2 += val;
        if (c3 > 0xff) { c2++; c3 &= 0xff; }
        *out++ = val;
        if ((out - out524) == MAC_GCR_SECTOR_TOTAL)
            break;

        val = b3[count] ^ c2;
        c1 += val;
        if (c2 > 0xff) { c1++; c2 &= 0xff; }
        *out++ = val;
    }
    uint8_t c4 = ((c1 & 0xc0) >> 6) | ((c2 & 0xc0) >> 4) | ((c3 & 0xc0) >> 2);
    uint8_t g4 = *in++, g3 = *in++, g2 = *in++, g1 = *in++;
    return (g4 == (c4 & 0x3f) && g3 == (c3 & 0x3f) && g2 == (c2 & 0x3f) && g1 == (c1 & 0x3f)) ? 0 : 1;
}

// bytes per physical sector slot and the offset of the data nibbles in it,
// mirroring the put()/sync() sequence in mac_gcr_encode_track()
#define SLOT_ADDRESS_BYTES (SYNC_GROUPS_BEFORE_ADDRESS * (int)sizeof(self_sync) + \
                            (int)sizeof(address_mark) + 5 + (int)sizeof(address_epilogue))
#define SLOT_DATA_HEAD     (SYNC_GROUPS_BEFORE_DATA * (int)sizeof(self_sync) + (int)sizeof(data_mark) + 1)
#define SLOT_BYTES         (SLOT_ADDRESS_BYTES + SLOT_DATA_HEAD + MAC_GCR_ENC_SECTOR + (int)sizeof(data_epilogue))

size_t mac_gcr_sector_data_offset(int cyl, uint8_t format, int sec)
{
    const int nsec = mac_gcr_sectors_per_track(cyl);
    int interleave = format & 0x1f;
    if (interleave < 1)
        interleave = 1;
    if (sec < 0 || sec >= nsec)
        return 0;

    int order[MAC_GCR_MAX_SECTORS];
    sector_order(nsec, interleave, order);
    for (int slot = 0; slot < nsec; slot++)
        if (order[slot] == sec)
            return (size_t)slot * SLOT_BYTES + SLOT_ADDRESS_BYTES + SLOT_DATA_HEAD;
    return 0;
}

bool mac_gcr_patch_sector(uint8_t *track, size_t track_size, int cyl,
                          uint8_t format, int sec, const uint8_t *in524)
{
    size_t off = mac_gcr_sector_data_offset(cyl, format, sec);
    if (track == NULL || off == 0 || off + MAC_GCR_ENC_SECTOR > track_size)
        return false;

    uint8_t enc[MAC_GCR_ENC_SECTOR];
    mac_gcr_encode_sector(in524, enc);
    for (int k = 0; k < MAC_GCR_ENC_SECTOR; k++)
        track[off + k] = mac_gcr_encode6(enc[k]);
    return true;
}

// read n bytes from a bitstream starting at bit `pos`; bits past the end read as 0
static void capture_bytes(const uint8_t *bits, uint32_t nbits, uint32_t pos, uint8_t *out, int n)
{
    for (int i = 0; i < n; i++)
    {
        uint8_t v = 0;
        for (int b = 0; b < 8; b++)
        {
            uint32_t p = pos + (uint32_t)i * 8 + b;
            int bit = (p < nbits) ? ((bits[p / 8] >> (7 - (p % 8))) & 1) : 0;
            v = (uint8_t)((v << 1) | bit);
        }
        out[i] = v;
    }
}

int mac_gcr_decode_capture(const uint8_t *bits, uint32_t nbits,
                           mac_gcr_written_sector *out, int max_out)
{
    int found = 0;
    bool have_addr = false;
    int a_cyl = -1, a_side = -1, a_sec = -1;
    uint8_t a_format = 0;
    uint8_t buf[1 + MAC_GCR_ENC_SECTOR];
    uint8_t six[MAC_GCR_ENC_SECTOR];

    if (bits == NULL || nbits < 24)
        return 0;

    for (uint32_t pos = 0; pos + 24 <= nbits && found < max_out;)
    {
        capture_bytes(bits, nbits, pos, buf, 3);
        if (!(buf[0] == 0xd5 && buf[1] == 0xaa))
        {
            pos++;
            continue;
        }

        if (buf[2] == 0x96)
        {
            // address field: cyl low, sector, side/cyl high, format, checksum
            capture_bytes(bits, nbits, pos + 24, buf, 5);
            int h[5];
            bool ok = true;
            for (int k = 0; k < 5; k++)
            {
                h[k] = mac_gcr_decode6(buf[k]);
                if (h[k] < 0)
                    ok = false;
            }
            if (ok && ((h[0] ^ h[1] ^ h[2] ^ h[3]) == h[4]))
            {
                a_cyl = h[0] | ((h[2] & 0x1f) << 6);
                a_side = (h[2] >> 5) & 1;
                a_sec = h[1];
                a_format = (uint8_t)h[3];
                have_addr = true;
                pos += 24 + 5 * 8;
                continue;
            }
            pos++;
            continue;
        }

        if (buf[2] == 0xad)
        {
            // data field: sector number then 703 nibbles
            capture_bytes(bits, nbits, pos + 24, buf, 1 + MAC_GCR_ENC_SECTOR);
            int sec = mac_gcr_decode6(buf[0]);
            bool ok = (sec >= 0 && sec < MAC_GCR_MAX_SECTORS);
            for (int k = 0; k < MAC_GCR_ENC_SECTOR && ok; k++)
            {
                int v = mac_gcr_decode6(buf[1 + k]);
                if (v < 0)
                    ok = false;
                else
                    six[k] = (uint8_t)v;
            }
            if (!ok)
            {
                pos++;
                continue;
            }
            mac_gcr_written_sector *w = &out[found++];
            w->sector = sec;
            w->checksum_ok = (mac_gcr_decode_sector(six, w->data) == 0);
            if (have_addr && a_sec == sec)
            {
                w->cyl = a_cyl;
                w->side = a_side;
                w->format = a_format;
            }
            else
            {
                w->cyl = -1;
                w->side = -1;
                w->format = 0;
            }
            have_addr = false;
            pos += 24 + (1 + MAC_GCR_ENC_SECTOR) * 8;
            continue;
        }

        pos++;
    }
    return found;
}
