/*
 * Host-side round-trip test for the Macintosh GCR track encoder.
 *
 * Encodes every track of a 400K or 800K sector image with
 * mac_gcr_encode_track(), then decodes the bitstream back with an
 * independent decoder (address/data mark search, 6&2 table, Sony
 * checksum) and checks that every sector comes back byte-for-byte with
 * the right cylinder/side/sector in its header.
 *
 * Usage: mac_gcr_test [image.dsk]
 * With no image a synthetic pattern-filled 800K disk is used.
 */

#include "media/mac/macGCR.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <vector>

static int gcr6_decode(uint8_t b) { return mac_gcr_decode6(b); }

// Inverse of the Sony sector scrambling; returns 0 if the checksum matches.
static int decode_sector(const uint8_t *in703, uint8_t *out524) { return mac_gcr_decode_sector(in703, out524); }

// Read `n` bytes from a circular bitstream starting at bit offset `pos`,
// byte-aligned to `pos` (the IWM aligns on the mark bytes).
static void bits_get(const uint8_t *trk, uint32_t nbits, uint32_t pos, uint8_t *out, int n)
{
    for (int i = 0; i < n; i++)
    {
        uint8_t v = 0;
        for (int b = 0; b < 8; b++)
        {
            uint32_t p = (pos + i * 8 + b) % nbits;
            v = (v << 1) | ((trk[p / 8] >> (7 - (p % 8))) & 1);
        }
        out[i] = v;
    }
}

static int decode_track(const uint8_t *trk, uint32_t nbits, int cyl, int side,
                        const uint8_t *expect, int nsec, int *found_out)
{
    int errors = 0;
    std::vector<bool> found(nsec, false);
    uint8_t buf[MAC_GCR_ENC_SECTOR + 8];

    // scan for address marks at every bit position (the real IWM does the
    // same, just serially)
    for (uint32_t pos = 0; pos < nbits; pos++)
    {
        bits_get(trk, nbits, pos, buf, 3);
        if (!(buf[0] == 0xd5 && buf[1] == 0xaa && buf[2] == 0x96))
            continue;

        bits_get(trk, nbits, pos + 24, buf, 5 + 2);
        int h[5];
        for (int k = 0; k < 5; k++)
            h[k] = gcr6_decode(buf[k]);
        if (h[0] < 0 || h[1] < 0 || h[2] < 0 || h[3] < 0 || h[4] < 0)
        {
            printf("  bad gcr in header at bit %u\n", pos);
            errors++;
            continue;
        }
        if ((h[0] ^ h[1] ^ h[2] ^ h[3]) != h[4])
        {
            printf("  header checksum error at bit %u\n", pos);
            errors++;
            continue;
        }
        if (!(buf[5] == 0xde && buf[6] == 0xaa))
        {
            printf("  missing address epilogue at bit %u\n", pos);
            errors++;
        }
        int hcyl = h[0] | ((h[2] & 1) << 6);
        int hside = (h[2] >> 5) & 1;
        int hsec = h[1];
        if (hcyl != cyl || hside != side || hsec >= nsec)
        {
            printf("  unexpected header C%d H%d S%d at bit %u\n", hcyl, hside, hsec, pos);
            errors++;
            continue;
        }

        // find the data mark within the next 100 bytes
        uint32_t dpos = 0;
        bool have = false;
        for (uint32_t q = pos + 24 + 7 * 8; q < pos + 24 + 7 * 8 + 100 * 8; q += 1)
        {
            bits_get(trk, nbits, q, buf, 3);
            if (buf[0] == 0xd5 && buf[1] == 0xaa && buf[2] == 0xad)
            {
                dpos = q + 24;
                have = true;
                break;
            }
        }
        if (!have)
        {
            printf("  no data mark after sector %d header\n", hsec);
            errors++;
            continue;
        }

        bits_get(trk, nbits, dpos, buf, 1 + MAC_GCR_ENC_SECTOR + 2);
        if (gcr6_decode(buf[0]) != hsec)
        {
            printf("  data field sector id mismatch for sector %d\n", hsec);
            errors++;
            continue;
        }
        uint8_t enc[MAC_GCR_ENC_SECTOR];
        bool badgcr = false;
        for (int k = 0; k < MAC_GCR_ENC_SECTOR; k++)
        {
            int v = gcr6_decode(buf[1 + k]);
            if (v < 0) { badgcr = true; break; }
            enc[k] = (uint8_t)v;
        }
        if (badgcr)
        {
            printf("  bad gcr in data of sector %d\n", hsec);
            errors++;
            continue;
        }
        if (!(buf[1 + MAC_GCR_ENC_SECTOR] == 0xde && buf[2 + MAC_GCR_ENC_SECTOR] == 0xaa))
        {
            printf("  missing data epilogue for sector %d\n", hsec);
            errors++;
        }
        uint8_t dec[MAC_GCR_SECTOR_TOTAL];
        if (decode_sector(enc, dec))
        {
            printf("  data checksum error in sector %d\n", hsec);
            errors++;
            continue;
        }
        if (memcmp(dec + MAC_GCR_TAG_SIZE, expect + hsec * MAC_GCR_SECTOR_SIZE, MAC_GCR_SECTOR_SIZE) != 0)
        {
            printf("  data mismatch in sector %d\n", hsec);
            errors++;
            continue;
        }
        found[hsec] = true;
        pos += 24; // skip past this mark
    }

    int nfound = 0;
    for (int s = 0; s < nsec; s++)
        if (found[s]) nfound++;
        else { printf("  sector %d not found\n", s); errors++; }
    *found_out = nfound;
    return errors;
}

// append `n` bytes to a bit vector at the current bit length
static void bits_append(std::vector<uint8_t> &v, uint32_t &nbits, const uint8_t *p, int n)
{
    for (int i = 0; i < n; i++)
        for (int b = 0; b < 8; b++)
        {
            if (nbits / 8 >= v.size())
                v.push_back(0);
            if ((p[i] >> (7 - b)) & 1)
                v[nbits / 8] |= 0x80 >> (nbits % 8);
            nbits++;
        }
}

static void bits_append_zero(std::vector<uint8_t> &v, uint32_t &nbits, int n)
{
    while (n--)
    {
        if (nbits / 8 >= v.size())
            v.push_back(0);
        nbits++;
    }
}

// Build what the IWM puts on the write line for one sector write (and,
// with `with_address`, a format-style address field first), preceded by
// `lead` zero bits so the marks land at odd alignments.
static void build_capture(std::vector<uint8_t> &v, uint32_t &nbits, int lead, bool with_address,
                          int cyl, int side, int sec, uint8_t format, const uint8_t *sec524)
{
    static const uint8_t sync[6] = {0xff, 0x3f, 0xcf, 0xf3, 0xfc, 0xff};
    static const uint8_t amark[3] = {0xd5, 0xaa, 0x96}, aepi[4] = {0xde, 0xaa, 0xff, 0xff};
    static const uint8_t dmark[3] = {0xd5, 0xaa, 0xad}, depi[3] = {0xde, 0xaa, 0xff};
    v.clear();
    nbits = 0;
    bits_append_zero(v, nbits, lead);
    if (with_address)
    {
        uint8_t h[5] = {(uint8_t)(cyl & 0x3f), (uint8_t)sec, (uint8_t)(((side & 1) << 5) | ((cyl >> 6) & 0x1f)), format, 0};
        h[4] = h[0] ^ h[1] ^ h[2] ^ h[3];
        for (int i = 0; i < 5; i++) bits_append(v, nbits, sync, 6);
        bits_append(v, nbits, amark, 3);
        for (int k = 0; k < 5; k++) { uint8_t e = mac_gcr_encode6(h[k]); bits_append(v, nbits, &e, 1); }
        bits_append(v, nbits, aepi, 4);
    }
    for (int i = 0; i < 5; i++) bits_append(v, nbits, sync, 6);
    bits_append(v, nbits, dmark, 3);
    uint8_t e = mac_gcr_encode6((uint8_t)sec);
    bits_append(v, nbits, &e, 1);
    uint8_t enc[MAC_GCR_ENC_SECTOR];
    mac_gcr_encode_sector(sec524, enc);
    for (int k = 0; k < MAC_GCR_ENC_SECTOR; k++) { e = mac_gcr_encode6(enc[k]); bits_append(v, nbits, &e, 1); }
    bits_append(v, nbits, depi, 3);
    bits_append_zero(v, nbits, 40);
}

// Exercise the write path: decode captured data fields at every alignment,
// patch a sector into an encoded track and check the track still decodes.
static int test_write_support(const std::vector<uint8_t> &image, int sides, uint8_t format)
{
    int errors = 0;
    const int cyl = 37, side = sides - 1, sec = 5;
    const int nsec = mac_gcr_sectors_per_track(cyl);
    size_t off = ((size_t)mac_gcr_sectors_before_cyl(cyl) * sides + (size_t)side * nsec) * MAC_GCR_SECTOR_SIZE;
    std::vector<uint8_t> sectors(image.begin() + off, image.begin() + off + (size_t)nsec * MAC_GCR_SECTOR_SIZE);

    uint8_t new524[MAC_GCR_SECTOR_TOTAL];
    for (int i = 0; i < MAC_GCR_SECTOR_TOTAL; i++)
        new524[i] = (uint8_t)(0xa5 ^ (i * 3));

    std::vector<uint8_t> cap;
    uint32_t nbits;
    mac_gcr_written_sector ws[4];
    for (int lead = 0; lead < 16; lead++)
    {
        for (int with_address = 0; with_address < 2; with_address++)
        {
            build_capture(cap, nbits, lead, with_address, cyl, side, sec, format, new524);
            int n = mac_gcr_decode_capture(cap.data(), nbits, ws, 4);
            if (n != 1 || ws[0].sector != sec || !ws[0].checksum_ok ||
                memcmp(ws[0].data, new524, MAC_GCR_SECTOR_TOTAL) != 0 ||
                (with_address && (ws[0].cyl != cyl || ws[0].side != side || ws[0].format != format)) ||
                (!with_address && ws[0].cyl != -1))
            {
                printf("  capture decode failed: lead %d addr %d -> n=%d sec=%d ok=%d cyl=%d side=%d\n",
                       lead, with_address, n, n ? ws[0].sector : -1, n ? ws[0].checksum_ok : 0,
                       n ? ws[0].cyl : -1, n ? ws[0].side : -1);
                errors++;
            }
        }
    }

    // a corrupted capture must not be accepted
    build_capture(cap, nbits, 3, false, cyl, side, sec, format, new524);
    cap[cap.size() / 2] ^= 0x10;
    int n = mac_gcr_decode_capture(cap.data(), nbits, ws, 4);
    if (n == 1 && ws[0].checksum_ok)
    {
        printf("  corrupted capture passed the checksum\n");
        errors++;
    }

    // patch the sector into a track and re-decode the whole track
    std::vector<uint8_t> trk(mac_gcr_track_bytes(cyl));
    uint32_t tbits = mac_gcr_encode_track(sectors.data(), NULL, cyl, side, format, trk.data(), trk.size());
    if (!mac_gcr_patch_sector(trk.data(), trk.size(), cyl, format, sec, new524))
    {
        printf("  patch_sector failed\n");
        return errors + 1;
    }
    memcpy(sectors.data() + (size_t)sec * MAC_GCR_SECTOR_SIZE, new524 + MAC_GCR_TAG_SIZE, MAC_GCR_SECTOR_SIZE);
    int found = 0;
    int errs = decode_track(trk.data(), tbits, cyl, side, sectors.data(), nsec, &found);
    if (errs || found != nsec)
    {
        printf("  track decode after patch: %d sectors, %d error(s)\n", found, errs);
        errors += errs ? errs : 1;
    }
    printf("write support: %d error(s)\n", errors);
    return errors;
}

int main(int argc, char **argv)
{
    std::vector<uint8_t> image;

    if (argc > 1)
    {
        FILE *f = fopen(argv[1], "rb");
        if (!f) { perror(argv[1]); return 2; }
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        image.resize(n);
        if (fread(image.data(), 1, n, f) != (size_t)n) { perror("read"); return 2; }
        fclose(f);
    }
    else
    {
        image.resize(MAC_GCR_800K_BYTES);
        for (size_t i = 0; i < image.size(); i++)
            image[i] = (uint8_t)((i * 7) ^ (i >> 9) ^ (i >> 13));
    }

    int sides;
    uint8_t format;
    if (image.size() == MAC_GCR_400K_BYTES) { sides = 1; format = MAC_GCR_FORMAT_SS; }
    else if (image.size() == MAC_GCR_800K_BYTES) { sides = 2; format = MAC_GCR_FORMAT_DS; }
    else { printf("image must be 400K or 800K (got %zu bytes)\n", image.size()); return 2; }

    printf("image: %zu bytes, %d side(s), format 0x%02x\n", image.size(), sides, format);

    int total_errors = 0, total_sectors = 0;
    size_t maxbytes = 0;
    for (int cyl = 0; cyl < MAC_GCR_CYLINDERS; cyl++)
    {
        int nsec = mac_gcr_sectors_per_track(cyl);
        size_t tb = mac_gcr_track_bytes(cyl);
        if (tb > maxbytes) maxbytes = tb;
        std::vector<uint8_t> trk(tb);
        for (int side = 0; side < sides; side++)
        {
            // image layout: for each cylinder, side 0 sectors then side 1 sectors
            size_t off = ((size_t)mac_gcr_sectors_before_cyl(cyl) * sides + (size_t)side * nsec) * MAC_GCR_SECTOR_SIZE;
            const uint8_t *sectors = image.data() + off;

            uint32_t nbits = mac_gcr_encode_track(sectors, NULL, cyl, side, format, trk.data(), trk.size());
            if (nbits == 0 || nbits != mac_gcr_track_bits(cyl))
            {
                printf("cyl %d side %d: encode failed (%u bits)\n", cyl, side, nbits);
                total_errors++;
                continue;
            }
            int found = 0;
            int errs = decode_track(trk.data(), nbits, cyl, side, sectors, nsec, &found);
            total_sectors += found;
            total_errors += errs;
            if (errs || cyl % 16 == 0)
                printf("cyl %2d side %d: %d sectors, %u bits, %d error(s)\n", cyl, side, found, nbits, errs);
        }
    }
    total_errors += test_write_support(image, sides, format);

    int expected = mac_gcr_sectors_before_cyl(MAC_GCR_CYLINDERS) * sides;
    printf("decoded %d/%d sectors, %d error(s), largest track %zu bytes\n",
           total_sectors, expected, total_errors, maxbytes);
    return (total_errors == 0 && total_sectors == expected) ? 0 : 1;
}
