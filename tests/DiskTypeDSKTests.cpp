#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include "media/rs232/diskTypeDSK.h"

// ---------------------------------------------------------------------------
// These tests exercise the MediaTypeDSK media layer directly -- the same
// seam a host drives over the wire: decode_sector() (CHS + fmttype -> byte
// offset), read()/write(), and sector_size(). No bus and no host are needed;
// the media layer is bus-agnostic, so its addressing hooks take the plain
// params the device extracts from a command packet -- built in-process here
// exactly as the host's disk BIOS packs them, and (with FNIO_IS_STDIO) a .DSK
// is just a stdio file.
//
// Layer 1 (primary): assert the offset math for every format against
// hand-computed offsets, including the Tarbell DD region boundary and the
// out-of-range cases. Layer 2: file-backed round-trips with a self-identifying
// sector fill so a wrong offset fails loudly on content. Plus the flat-IMG
// regression guard on the base decode_sector() default.
// ---------------------------------------------------------------------------

// Decode a CHS access the way the device does: pack the params the host sends
// (head=u8, track=u16, sector=u8, fmttype=u8) into the bus-agnostic array the
// media layer takes, and call decode_sector(). The media layer never sees a bus
// packet.
static void decode_chs(MediaTypeDSK &fl, uint8_t head, uint16_t trk, uint8_t sec,
                       uint8_t fmt)
{
    uint32_t p[4] = {head, trk, sec, fmt};
    fl.decode_sector(p, 4);
}

// Decode a CHS access and assert the resolved offset + size.
static void expect_ok(MediaTypeDSK &fl, uint8_t h, uint16_t t, uint8_t s, uint8_t fmt,
                      uint32_t off, uint16_t size)
{
    decode_chs(fl, h, t, s, fmt);
    CHECK(fl.cur_valid());
    CHECK(fl.cur_offset() == off);
    CHECK(fl.sector_size(0) == size);
}

// Decode an out-of-range CHS access and assert it is refused (no I/O will follow).
static void expect_bad(MediaTypeDSK &fl, uint8_t h, uint16_t t, uint8_t s, uint8_t fmt)
{
    decode_chs(fl, h, t, s, fmt);
    CHECK_FALSE(fl.cur_valid());
    CHECK(fl.sector_size(0) == 0); // read()/write() gate on !_cur_valid
}

// Decode a whole-track access (mode bit OR'd into fmttype) and assert the
// resolved track offset + track size. The sector param is passed but ignored.
static void expect_track(MediaTypeDSK &fl, uint8_t h, uint16_t t, uint8_t fmt,
                         uint32_t off, uint16_t size)
{
    decode_chs(fl, h, t, /*sec ignored*/ 1, fmt | FMT_MODE_TRACK);
    CHECK(fl.cur_valid());
    CHECK(fl.cur_offset() == off);
    CHECK(fl.sector_size(0) == size);
}

// ---------------------------------------------------------------------------
// Layer 1 -- offset math (the crown jewel)
// ---------------------------------------------------------------------------

TEST_CASE("IBM 8\" SD: 77 x 26 x 128")
{
    MediaTypeDSK fl;
    expect_ok(fl, 0, 0, 1, FMT_IBM_SD, 0, 128);
    expect_ok(fl, 0, 0, 26, FMT_IBM_SD, 25 * 128, 128);       // 3200
    expect_ok(fl, 0, 1, 1, FMT_IBM_SD, 26 * 128, 128);        // 3328 (= 1 track)
    expect_ok(fl, 0, 76, 26, FMT_IBM_SD, 256256 - 128, 128);  // last sector

    expect_bad(fl, 0, 0, 0, FMT_IBM_SD);   // sector < first_sector (1)
    expect_bad(fl, 0, 0, 27, FMT_IBM_SD);  // sector > first + sectors - 1
    expect_bad(fl, 0, 77, 1, FMT_IBM_SD);  // track >= num_tracks
    expect_bad(fl, 1, 0, 1, FMT_IBM_SD);   // head >= num_sides (single-sided)
}

TEST_CASE("IBM 8\" DD: 77 x 26 x 256")
{
    MediaTypeDSK fl;
    expect_ok(fl, 0, 0, 1, FMT_IBM_DD, 0, 256);
    expect_ok(fl, 0, 0, 26, FMT_IBM_DD, 25 * 256, 256);  // 6400
    expect_ok(fl, 0, 1, 1, FMT_IBM_DD, 26 * 256, 256);   // 6656 (= 1 track)
    expect_ok(fl, 0, 76, 26, FMT_IBM_DD, 512512 - 256, 256);
}

TEST_CASE("Altair 8\": 77 x 32 x 137 (non-power-of-two sector)")
{
    MediaTypeDSK fl;
    expect_ok(fl, 0, 0, 1, FMT_ALTAIR, 0, 137);
    expect_ok(fl, 0, 0, 2, FMT_ALTAIR, 137, 137);
    expect_ok(fl, 0, 0, 32, FMT_ALTAIR, 31 * 137, 137);  // 4247
    expect_ok(fl, 0, 1, 1, FMT_ALTAIR, 32 * 137, 137);   // 4384 (= 1 track)

    expect_bad(fl, 0, 0, 33, FMT_ALTAIR); // only 32 sectors/track
}

TEST_CASE("FDC+: 2048 x 32 x 137 (Altair geometry, track number exceeds a byte)")
{
    MediaTypeDSK fl;
    // Sector 1 of each track sits at the track start (32 x 137 = 4384/track).
    expect_ok(fl, 0, 0, 1, FMT_FDCPLUS, 0, 137);
    expect_ok(fl, 0, 1, 1, FMT_FDCPLUS, 4384, 137);
    expect_ok(fl, 0, 255, 1, FMT_FDCPLUS, 255u * 4384, 137);
    expect_ok(fl, 0, 256, 1, FMT_FDCPLUS, 256u * 4384, 137);   // track > 255 resolves (u16)
    expect_ok(fl, 0, 2047, 1, FMT_FDCPLUS, 2047u * 4384, 137); // 8974048, last track start
    // Sub-track sector addressing (the point of making FDC+ "real").
    expect_ok(fl, 0, 0, 32, FMT_FDCPLUS, 31 * 137, 137);          // 4247, last sector track 0
    expect_ok(fl, 0, 2047, 32, FMT_FDCPLUS, 2047u * 4384 + 31 * 137, 137); // last sector, last track

    expect_bad(fl, 0, 0, 33, FMT_FDCPLUS);   // only 32 sectors/track
    expect_bad(fl, 0, 2048, 1, FMT_FDCPLUS); // track >= num_tracks
}

TEST_CASE("Tarbell DD: mixed sector COUNT, 2-region (general offset walk)")
{
    MediaTypeDSK fl;

    // Track 0 is IBM-SD-like: 26 x 128-byte sectors.
    expect_ok(fl, 0, 0, 1, FMT_TARBELL_DD, 0, 128);
    expect_ok(fl, 0, 0, 26, FMT_TARBELL_DD, 25 * 128, 128); // 3200
    expect_bad(fl, 0, 0, 27, FMT_TARBELL_DD);               // track 0 has only 26

    // The region boundary: track 1 begins right after track 0's 26 x 128 = 3328.
    expect_ok(fl, 0, 1, 1, FMT_TARBELL_DD, 3328, 128);
    // Track 1 has 51 sectors.
    expect_ok(fl, 0, 1, 51, FMT_TARBELL_DD, 3328 + 50 * 128, 128); // 9728
    expect_bad(fl, 0, 1, 52, FMT_TARBELL_DD);                      // track 1 has only 51

    // Track 2 begins after track 0 (3328) + track 1 (51 x 128 = 6528).
    expect_ok(fl, 0, 2, 1, FMT_TARBELL_DD, 3328 + 51 * 128, 128); // 9856

    expect_bad(fl, 0, 77, 1, FMT_TARBELL_DD); // track >= num_tracks
}

TEST_CASE("Bad fmttype is refused")
{
    MediaTypeDSK fl;
    expect_bad(fl, 0, 0, 1, FMT_COUNT);      // == table size, out of range
    expect_bad(fl, 0, 0, 1, FMT_COUNT + 7);  // well past the table
}

TEST_CASE("Track mode: offset = start of track, size = whole track")
{
    MediaTypeDSK fl;

    // Uniform formats: track start and track size (= spt x sector_size).
    expect_track(fl, 0, 0, FMT_IBM_SD, 0, 26 * 128);        // 3328
    expect_track(fl, 0, 1, FMT_IBM_SD, 26 * 128, 26 * 128); // starts at 3328
    expect_track(fl, 0, 76, FMT_IBM_SD, 256256 - 3328, 3328); // last track
    expect_track(fl, 0, 1, FMT_IBM_DD, 26 * 256, 26 * 256); // 6656 @ 6656 (largest track)
    expect_track(fl, 0, 1, FMT_ALTAIR, 32 * 137, 32 * 137); // 4384 @ 4384
    expect_track(fl, 0, 5, FMT_FDCPLUS, 5u * 4384, 4384);   // 32 x 137 == whole track

    // Tarbell DD: the two regions have different track sizes.
    expect_track(fl, 0, 0, FMT_TARBELL_DD, 0, 26 * 128);          // 3328
    expect_track(fl, 0, 1, FMT_TARBELL_DD, 3328, 51 * 128);       // 6528 @ 3328
    expect_track(fl, 0, 2, FMT_TARBELL_DD, 3328 + 6528, 51 * 128); // 6528 @ 9856
}

TEST_CASE("Track mode ignores the sector param but still bounds-checks track/head")
{
    MediaTypeDSK fl;

    // A sector value that would be out of range in sector mode is fine in track
    // mode -- it is not consulted.
    decode_chs(fl, 0, 1, /*sec*/ 200, FMT_IBM_SD | FMT_MODE_TRACK);
    CHECK(fl.cur_valid());
    CHECK(fl.cur_offset() == 26 * 128);
    CHECK(fl.sector_size(0) == 26 * 128);

    // Track/head bounds still apply.
    decode_chs(fl, 0, 77, 1, FMT_IBM_SD | FMT_MODE_TRACK);
    CHECK_FALSE(fl.cur_valid());
    decode_chs(fl, 1, 0, 1, FMT_IBM_SD | FMT_MODE_TRACK);
    CHECK_FALSE(fl.cur_valid());
}

TEST_CASE("An unknown access mode is refused (not treated as a sector access)")
{
    MediaTypeDSK fl;
    // High bit set alone (0x80) is neither SECTOR (0x00) nor TRACK (0x40).
    decode_chs(fl, 0, 0, 1, FMT_IBM_SD | 0x80);
    CHECK_FALSE(fl.cur_valid());
    CHECK(fl.sector_size(0) == 0);
}

TEST_CASE("A short (legacy/malformed) access without fmttype is refused")
{
    MediaTypeDSK fl;
    // Three params -- no fmttype. Geometry cannot be resolved.
    uint32_t p[3] = {0, 0, 1};
    fl.decode_sector(p, 3);
    CHECK_FALSE(fl.cur_valid());
    CHECK(fl.sector_size(0) == 0);
}

TEST_CASE("Staging buffer sizing: Floppy holds the largest sector AND track")
{
    MediaTypeDSK fl;
    CHECK(fl.sector_buffer_size() == DSK_BUFFER_SIZE);
    CHECK(fl.sector_buffer() != nullptr);
    // The buffer must hold the largest single sector (IBM 8" DD, 256)...
    CHECK(DSK_BUFFER_SIZE >= DSK_MAX_SECTOR_SIZE);
    CHECK(DSK_MAX_SECTOR_SIZE == 256);
    // ...and, in track mode, the largest whole track (IBM 8" DD, 26 x 256).
    CHECK(DSK_BUFFER_SIZE >= DSK_MAX_TRACK_SIZE);
    CHECK(DSK_MAX_TRACK_SIZE == 6656);
    // ...while the shared base buffer that IMG/ROM carry is unchanged at 512.
    CHECK(DISK_SECTORBUF_SIZE == 512);
}

// ---------------------------------------------------------------------------
// Layer 2 -- file-backed round-trips (self-identifying sector fill)
//
// Each sector's first 4 bytes are {'D', head, track&0xFF, sector&0xFF} and the
// remainder is filled with the linear sector index &0xFF. A read addressed by
// CHS therefore proves the offset landed on the intended sector by content; a
// wrong locate() reads a different sector's stamp and the check fails.
// ---------------------------------------------------------------------------

// Generate a uniform single-region image (tracks x spt x secsize), first sector 1.
static void gen_uniform(const char *path, int tracks, int spt, int secsize)
{
    std::FILE *f = std::fopen(path, "wb");
    REQUIRE(f != nullptr);
    std::vector<uint8_t> sec(secsize);
    for (int t = 0; t < tracks; t++)
        for (int s = 1; s <= spt; s++)
        {
            int idx = t * spt + (s - 1);
            std::fill(sec.begin(), sec.end(), (uint8_t)(idx & 0xFF));
            sec[0] = 'D';
            sec[1] = 0;
            sec[2] = (uint8_t)(t & 0xFF);
            sec[3] = (uint8_t)(s & 0xFF);
            std::fwrite(sec.data(), 1, secsize, f);
        }
    std::fclose(f);
}

// Region-aware Tarbell DD generator: track 0 = 26 x 128, tracks 1.. = 51 x 128.
static void gen_tarbell(const char *path, int tracks)
{
    std::FILE *f = std::fopen(path, "wb");
    REQUIRE(f != nullptr);
    std::vector<uint8_t> sec(128);
    for (int t = 0; t < tracks; t++)
    {
        int spt = (t == 0) ? 26 : 51;
        for (int s = 1; s <= spt; s++)
        {
            std::fill(sec.begin(), sec.end(), (uint8_t)(s & 0xFF));
            sec[0] = 'D';
            sec[1] = 0;
            sec[2] = (uint8_t)(t & 0xFF);
            sec[3] = (uint8_t)(s & 0xFF);
            std::fwrite(sec.data(), 1, 128, f);
        }
    }
    std::fclose(f);
}

static void check_stamp(MediaTypeDSK &fl, uint8_t h, uint16_t t, uint8_t s, uint8_t fmt,
                        uint32_t want_size)
{
    decode_chs(fl, h, t, s, fmt);
    REQUIRE(fl.cur_valid());
    uint32_t rc = 0;
    CHECK_FALSE(fl.read(0, &rc)); // error_is_true: false == success
    CHECK(rc == want_size);
    const uint8_t *b = fl.sector_buffer();
    CHECK(b[0] == 'D');
    CHECK(b[1] == 0);
    CHECK(b[2] == (uint8_t)(t & 0xFF));
    CHECK(b[3] == (uint8_t)(s & 0xFF));
}

TEST_CASE("Round-trip read: IBM SD stamps read back by CHS")
{
    const char *path = "diskdsk_rt_ibmsd.dsk";
    const int tracks = 3, spt = 26, ss = 128;
    gen_uniform(path, tracks, spt, ss);

    std::FILE *f = std::fopen(path, "r+b");
    REQUIRE(f != nullptr);
    MediaTypeDSK fl;
    CHECK(fl.mount(f, (uint32_t)(tracks * spt * ss)) == MEDIATYPE_DSK);

    check_stamp(fl, 0, 0, 1, FMT_IBM_SD, 128);
    check_stamp(fl, 0, 0, 26, FMT_IBM_SD, 128);
    check_stamp(fl, 0, 1, 1, FMT_IBM_SD, 128); // crosses a track boundary
    check_stamp(fl, 0, 2, 13, FMT_IBM_SD, 128);

    // The filled body byte independently confirms the offset (= linear index).
    decode_chs(fl, 0, 2, 13, FMT_IBM_SD);
    uint32_t rc = 0;
    CHECK_FALSE(fl.read(0, &rc));
    int idx = 2 * spt + (13 - 1);
    CHECK(fl.sector_buffer()[4] == (uint8_t)(idx & 0xFF));

    fl.unmount();
    std::remove(path);
}

TEST_CASE("Round-trip write: hits the right offset, leaves the layout intact")
{
    const char *path = "diskdsk_rt_write.dsk";
    const int tracks = 3, spt = 26, ss = 128;
    gen_uniform(path, tracks, spt, ss);

    // Recompute the expected image, then stamp sector (0,1,5) with the write pattern.
    std::vector<uint8_t> expected((size_t)tracks * spt * ss);
    for (int t = 0; t < tracks; t++)
        for (int s = 1; s <= spt; s++)
        {
            int idx = t * spt + (s - 1);
            uint8_t *d = &expected[(size_t)idx * ss];
            std::memset(d, (uint8_t)(idx & 0xFF), ss);
            d[0] = 'D';
            d[1] = 0;
            d[2] = (uint8_t)(t & 0xFF);
            d[3] = (uint8_t)(s & 0xFF);
        }
    const int wt = 1, wsx = 5;
    const uint32_t woff = (uint32_t)((wt * spt + (wsx - 1)) * ss); // 30 * 128 = 3840
    std::memset(&expected[woff], 0xAB, ss);

    std::FILE *f = std::fopen(path, "r+b");
    REQUIRE(f != nullptr);
    MediaTypeDSK fl;
    CHECK(fl.mount(f, (uint32_t)(tracks * spt * ss)) == MEDIATYPE_DSK);

    // Write the pattern to (0,1,5).
    decode_chs(fl, 0, wt, wsx, FMT_IBM_SD);
    CHECK(fl.cur_offset() == woff);
    std::memset(fl.sector_buffer(), 0xAB, fl.sector_buffer_size());
    CHECK_FALSE(fl.write(0, false)); // false == success

    // Re-read it back.
    decode_chs(fl, 0, wt, wsx, FMT_IBM_SD);
    uint32_t rc = 0;
    CHECK_FALSE(fl.read(0, &rc));
    CHECK(rc == (uint32_t)ss);
    for (int i = 0; i < ss; i++)
        CHECK(fl.sector_buffer()[i] == 0xAB);

    fl.unmount(); // flush + close

    // The whole file must match the expected image byte-for-byte: the write hit
    // the right offset/length and touched nothing else (raw-dump invariant).
    std::FILE *rf = std::fopen(path, "rb");
    REQUIRE(rf != nullptr);
    std::vector<uint8_t> got(expected.size());
    size_t n = std::fread(got.data(), 1, got.size(), rf);
    std::fclose(rf);
    CHECK(n == expected.size());
    CHECK(got == expected);

    std::remove(path);
}

TEST_CASE("Round-trip read: Tarbell DD region boundary read back by CHS")
{
    const char *path = "diskdsk_rt_tarbell.dsk";
    const int tracks = 3;
    gen_tarbell(path, tracks);
    const uint32_t size = (uint32_t)((26 + 51 + 51) * 128); // 16384

    std::FILE *f = std::fopen(path, "r+b");
    REQUIRE(f != nullptr);
    MediaTypeDSK fl;
    CHECK(fl.mount(f, size) == MEDIATYPE_DSK);

    check_stamp(fl, 0, 0, 26, FMT_TARBELL_DD, 128); // last of the 26-sector track 0
    check_stamp(fl, 0, 1, 1, FMT_TARBELL_DD, 128);  // first of the 51-sector region
    check_stamp(fl, 0, 1, 51, FMT_TARBELL_DD, 128); // last of track 1
    check_stamp(fl, 0, 2, 1, FMT_TARBELL_DD, 128);  // start of track 2

    fl.unmount();
    std::remove(path);
}

TEST_CASE("Round-trip read: a whole track reads back all its sector stamps in order")
{
    const char *path = "diskdsk_rt_track.dsk";
    const int tracks = 3, spt = 26, ss = 128;
    gen_uniform(path, tracks, spt, ss);

    std::FILE *f = std::fopen(path, "r+b");
    REQUIRE(f != nullptr);
    MediaTypeDSK fl;
    CHECK(fl.mount(f, (uint32_t)(tracks * spt * ss)) == MEDIATYPE_DSK);

    // Read all of track 1 in one transfer.
    decode_chs(fl, 0, 1, 1, FMT_IBM_SD | FMT_MODE_TRACK);
    REQUIRE(fl.cur_valid());
    uint32_t rc = 0;
    CHECK_FALSE(fl.read(0, &rc));
    CHECK(rc == (uint32_t)(spt * ss)); // 3328

    // Every sector's stamp appears at its sub-offset within the track buffer.
    const uint8_t *b = fl.sector_buffer();
    for (int s = 1; s <= spt; s++)
    {
        const uint8_t *sec = b + (size_t)(s - 1) * ss;
        CHECK(sec[0] == 'D');
        CHECK(sec[1] == 0);
        CHECK(sec[2] == 1); // track
        CHECK(sec[3] == (uint8_t)s);
        int idx = 1 * spt + (s - 1);
        CHECK(sec[4] == (uint8_t)(idx & 0xFF));
    }

    fl.unmount();
    std::remove(path);
}

TEST_CASE("Round-trip write: a whole-track write lands contiguously, layout intact")
{
    const char *path = "diskdsk_rt_trackwrite.dsk";
    const int tracks = 3, spt = 26, ss = 128;
    const int tracklen = spt * ss; // 3328
    gen_uniform(path, tracks, spt, ss);

    // Expected image: original, with all of track 1 overwritten by 0xCD.
    std::vector<uint8_t> expected((size_t)tracks * spt * ss);
    for (int t = 0; t < tracks; t++)
        for (int s = 1; s <= spt; s++)
        {
            int idx = t * spt + (s - 1);
            uint8_t *d = &expected[(size_t)idx * ss];
            std::memset(d, (uint8_t)(idx & 0xFF), ss);
            d[0] = 'D';
            d[1] = 0;
            d[2] = (uint8_t)(t & 0xFF);
            d[3] = (uint8_t)(s & 0xFF);
        }
    std::memset(&expected[(size_t)1 * tracklen], 0xCD, tracklen); // track 1

    std::FILE *f = std::fopen(path, "r+b");
    REQUIRE(f != nullptr);
    MediaTypeDSK fl;
    CHECK(fl.mount(f, (uint32_t)(tracks * spt * ss)) == MEDIATYPE_DSK);

    decode_chs(fl, 0, 1, 1, FMT_IBM_SD | FMT_MODE_TRACK);
    CHECK(fl.cur_offset() == (uint32_t)tracklen); // track 1 starts at 3328
    CHECK(fl.sector_size(0) == (uint16_t)tracklen);
    std::memset(fl.sector_buffer(), 0xCD, fl.sector_buffer_size());
    CHECK_FALSE(fl.write(0, false));

    fl.unmount();

    std::FILE *rf = std::fopen(path, "rb");
    REQUIRE(rf != nullptr);
    std::vector<uint8_t> got(expected.size());
    size_t n = std::fread(got.data(), 1, got.size(), rf);
    std::fclose(rf);
    CHECK(n == expected.size());
    CHECK(got == expected); // track 1 rewritten, tracks 0 and 2 untouched

    std::remove(path);
}

TEST_CASE("read()/write() refuse an out-of-range address without touching the file")
{
    const char *path = "diskdsk_rt_guard.dsk";
    gen_uniform(path, 2, 26, 128);

    std::FILE *f = std::fopen(path, "r+b");
    REQUIRE(f != nullptr);
    MediaTypeDSK fl;
    CHECK(fl.mount(f, (uint32_t)(2 * 26 * 128)) == MEDIATYPE_DSK);

    decode_chs(fl, 0, 99, 1, FMT_IBM_SD); // track out of range
    CHECK_FALSE(fl.cur_valid());
    uint32_t rc = 123;
    CHECK(fl.read(0, &rc).is_error());  // error
    CHECK(fl.write(0, false).is_error()); // error

    fl.unmount();
    std::remove(path);
}

// ---------------------------------------------------------------------------
// Custom formats (FMT_CUSTOM) -- host-supplied geometry via set_geometry().
// A SET_GEOMETRY packet carries one region; the append flag builds multi-region
// formats. FMT_CUSTOM then addresses that runtime geometry like any baked one.
// ---------------------------------------------------------------------------

// Apply a SET_GEOMETRY region the way the device does: pack the params the host
// sends into the bus-agnostic array and call set_geometry(). flags bit0 =
// head_major, bit1 = append.
static void set_geom(MediaTypeDSK &fl, uint16_t ft, uint16_t lt, uint8_t spt,
                     uint16_t ss, uint8_t first, uint8_t head_mask, uint8_t sides,
                     uint8_t flags)
{
    uint32_t p[8] = {ft, lt, spt, ss, first, head_mask, sides, flags};
    fl.set_geometry(p, 8);
}

TEST_CASE("FMT_CUSTOM before any set_geometry is refused")
{
    MediaTypeDSK fl;
    expect_bad(fl, 0, 0, 1, FMT_CUSTOM);
}

TEST_CASE("Custom uniform 512-byte single-sided: offsets and track mode")
{
    MediaTypeDSK fl;
    // 10 tracks, 8 sectors/track, 512-byte sectors, first sector 1, single-sided.
    set_geom(fl, 0, 9, 8, 512, 1, 0xFF, 1, 0x00);

    expect_ok(fl, 0, 0, 1, FMT_CUSTOM, 0, 512);
    expect_ok(fl, 0, 0, 8, FMT_CUSTOM, 7 * 512, 512);
    expect_ok(fl, 0, 1, 1, FMT_CUSTOM, 8 * 512, 512);       // one track in
    expect_ok(fl, 0, 9, 8, FMT_CUSTOM, (10 * 8 - 1) * 512, 512); // last sector

    expect_bad(fl, 0, 0, 9, FMT_CUSTOM);  // only 8 sectors
    expect_bad(fl, 0, 10, 1, FMT_CUSTOM); // only 10 tracks
    expect_bad(fl, 1, 0, 1, FMT_CUSTOM);  // single-sided

    // Track mode: whole 8 x 512 = 4096-byte track (fits the static buffer).
    expect_track(fl, 0, 1, FMT_CUSTOM, 8 * 512, 8 * 512);
}

TEST_CASE("Custom 1024-byte sector grows the staging buffer for track mode")
{
    MediaTypeDSK fl;
    // 8 x 1024 = 8192-byte track exceeds the 6656-byte static buffer.
    set_geom(fl, 0, 4, 8, 1024, 1, 0xFF, 1, 0x00);

    CHECK(fl.sector_buffer_size() >= 8192);
    CHECK(fl.sector_buffer() != nullptr);

    expect_ok(fl, 0, 0, 1, FMT_CUSTOM, 0, 1024);
    expect_track(fl, 0, 0, FMT_CUSTOM, 0, 8 * 1024); // whole track = 8192
    expect_track(fl, 0, 1, FMT_CUSTOM, 8 * 1024, 8 * 1024);
}

TEST_CASE("Custom double-sided interleaved vs head-major side ordering")
{
    // 2 tracks, 2 sides, 4 sectors/track, 256-byte sectors.
    SUBCASE("interleaved (cylinder-major): c0h0, c0h1, c1h0, c1h1")
    {
        MediaTypeDSK fl;
        set_geom(fl, 0, 1, 4, 256, 1, 0xFF, 2, 0x00); // flags bit0 clear
        uint32_t trk_bytes = 4 * 256;
        expect_ok(fl, 0, 0, 1, FMT_CUSTOM, 0 * trk_bytes, 256);
        expect_ok(fl, 1, 0, 1, FMT_CUSTOM, 1 * trk_bytes, 256); // side 1 of cyl 0
        expect_ok(fl, 0, 1, 1, FMT_CUSTOM, 2 * trk_bytes, 256); // side 0 of cyl 1
        expect_ok(fl, 1, 1, 1, FMT_CUSTOM, 3 * trk_bytes, 256);
    }
    SUBCASE("head-major (sequential): all of side 0, then all of side 1")
    {
        MediaTypeDSK fl;
        set_geom(fl, 0, 1, 4, 256, 1, 0xFF, 2, 0x01); // flags bit0 set
        uint32_t trk_bytes = 4 * 256;
        expect_ok(fl, 0, 0, 1, FMT_CUSTOM, 0 * trk_bytes, 256);
        expect_ok(fl, 0, 1, 1, FMT_CUSTOM, 1 * trk_bytes, 256); // side 0, track 1
        expect_ok(fl, 1, 0, 1, FMT_CUSTOM, 2 * trk_bytes, 256); // side 1 begins
        expect_ok(fl, 1, 1, 1, FMT_CUSTOM, 3 * trk_bytes, 256);
    }
}

TEST_CASE("Custom per-side track-0 geometry (head_mask'd regions, general walk)")
{
    MediaTypeDSK fl;
    // Track 0 side 0 is a 128-byte SD boot side; side 1 and track 1 are 256-byte.
    set_geom(fl, 0, 0, 26, 128, 1, 0x01, 2, 0x00); // fresh:  T0 side 0 only
    set_geom(fl, 0, 0, 26, 256, 1, 0x02, 2, 0x02); // append: T0 side 1 only
    set_geom(fl, 1, 1, 26, 256, 1, 0xFF, 2, 0x02); // append: T1 both sides

    // Cylinder-major walk over per-slot region bytes:
    //   (0,0) 26x128 = 3328 ; (0,1) 26x256 = 6656 ; (1,0)/(1,1) 26x256 each.
    expect_ok(fl, 0, 0, 1, FMT_CUSTOM, 0, 128);            // T0H0: 128-byte sectors
    expect_ok(fl, 1, 0, 1, FMT_CUSTOM, 3328, 256);         // T0H1: 256-byte, after H0
    expect_ok(fl, 0, 1, 1, FMT_CUSTOM, 3328 + 6656, 256);  // T1H0 = 9984
    expect_ok(fl, 1, 1, 1, FMT_CUSTOM, 9984 + 26 * 256, 256); // T1H1 = 16640

    // Side-specific sector size is honored on the same cylinder.
    expect_ok(fl, 0, 0, 26, FMT_CUSTOM, 25 * 128, 128);    // last of the 128B side
    expect_ok(fl, 1, 0, 26, FMT_CUSTOM, 3328 + 25 * 256, 256);
}

TEST_CASE("Custom Tarbell (via append) resolves identically to the baked format")
{
    MediaTypeDSK custom;
    set_geom(custom, 0, 0, 26, 128, 1, 0xFF, 1, 0x00);  // fresh:  track 0
    set_geom(custom, 1, 76, 51, 128, 1, 0xFF, 1, 0x02); // append: tracks 1-76

    MediaTypeDSK baked;

    struct { uint8_t h; uint16_t t; uint8_t s; } cases[] = {
        {0, 0, 1}, {0, 0, 26}, {0, 1, 1}, {0, 1, 51}, {0, 2, 1}, {0, 76, 51},
    };
    for (auto &c : cases)
    {
        decode_chs(custom, c.h, c.t, c.s, FMT_CUSTOM);
        decode_chs(baked, c.h, c.t, c.s, FMT_TARBELL_DD);
        CHECK(custom.cur_valid());
        CHECK(baked.cur_valid());
        CHECK(custom.cur_offset() == baked.cur_offset());
        CHECK(custom.sector_size(0) == baked.sector_size(0));
    }
}

TEST_CASE("A fresh (non-append) set_geometry resets a prior multi-region build")
{
    MediaTypeDSK fl;
    // Build a 2-region format, then replace it with a single-region fresh call.
    set_geom(fl, 0, 0, 26, 128, 1, 0xFF, 1, 0x00);
    set_geom(fl, 1, 76, 51, 128, 1, 0xFF, 1, 0x02);
    // Fresh: a small uniform 5-track x 4 x 256 format.
    set_geom(fl, 0, 4, 4, 256, 1, 0xFF, 1, 0x00);

    expect_ok(fl, 0, 0, 1, FMT_CUSTOM, 0, 256);
    expect_ok(fl, 0, 4, 4, FMT_CUSTOM, (5 * 4 - 1) * 256, 256); // last sector
    expect_bad(fl, 0, 5, 1, FMT_CUSTOM);                        // only 5 tracks now
}

TEST_CASE("An invalid custom geometry leaves FMT_CUSTOM refused")
{
    MediaTypeDSK fl;
    // sector_size 0 is rejected -> _custom_valid stays false.
    set_geom(fl, 0, 9, 8, 0, 1, 0xFF, 1, 0x00);
    expect_bad(fl, 0, 0, 1, FMT_CUSTOM);

    // sector_size over the 1024 cap is rejected too.
    set_geom(fl, 0, 9, 8, 2048, 1, 0xFF, 1, 0x00);
    expect_bad(fl, 0, 0, 1, FMT_CUSTOM);
}

// ---------------------------------------------------------------------------
// Regression guard for the flat-IMG path: the base decode_sector() default
// returns params[0] unchanged (what the flat path resolves to), and the base
// staging buffer stays the 512-byte member. Tested through a minimal LBA
// subclass so this needs none of MediaTypeImg's device dependencies.
// ---------------------------------------------------------------------------

struct LbaStub : public MediaType
{
    mediatype_t mount(fnFile *, uint32_t, fujiHost *, const char *) override
    {
        return MEDIATYPE_IMG;
    }
    error_is_true read(uint32_t, uint32_t *) override { RETURN_SUCCESS_AS_FALSE(); }
    void status(uint8_t[4]) override {}
};

TEST_CASE("Base decode_sector() default is the identity LBA -> params[0]")
{
    LbaStub img;
    uint32_t p[1] = {12345};
    CHECK(img.decode_sector(p, 1) == 12345u);

    // Base staging buffer is unchanged: the 512-byte shared member.
    CHECK(img.sector_buffer() == img._disk_sectorbuff);
    CHECK(img.sector_buffer_size() == DISK_SECTORBUF_SIZE);
}
