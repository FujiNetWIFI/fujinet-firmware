#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include "IMDFixture.h"
#include "diskTypeIMD.h"

namespace
{

// The build forces FNIO_IS_STDIO for this target, so fnFile is std::FILE.
// A name of its own so a parallel ctest run cannot collide with IMDImageTests.
#define IMD_TEST_FILE "mediatypeimd_test.tmp"

// Mounts a built image through the adapter exactly as rs232Disk::mount() would.
struct Mounted
{
    fnFile      *fp = nullptr;
    MediaTypeIMD media;
    mediatype_t  type = MEDIATYPE_UNKNOWN;

    Mounted(const IMD &b, bool writable = false) : media(writable)
    {
        fp = fopen(IMD_TEST_FILE, "w+b");
        REQUIRE(fp != nullptr);
        REQUIRE(fwrite(b.out.data(), 1, b.out.size(), fp) == b.out.size());
        rewind(fp);
        type = media.mount(fp, (uint32_t)b.out.size());
    }

    ~Mounted()
    {
        media.unmount(); // closes fp
        remove(IMD_TEST_FILE);
    }
};

// A 2 cylinder x 2 head, 4 sector, 128-byte MFM image. Fill byte encodes the
// track so a read can prove which LBA it landed on.
IMD two_by_two()
{
    IMD b;
    b.header();
    b.simple_track(3, 0, 0, 4, 0xA0);
    b.simple_track(3, 0, 1, 4, 0xA1);
    b.simple_track(3, 1, 0, 4, 0xB0);
    b.simple_track(3, 1, 1, 4, 0xB1);
    return b;
}

// status() sends the controller byte negated; doctest also forbids a bare & in
// an assertion, so unpack both here.
uint8_t ctrl(const uint8_t st[4])
{
    return (uint8_t)~st[1];
}

bool has(uint8_t bits, uint8_t mask)
{
    return (bits & mask) != 0;
}

bool all_equal(const uint8_t *p, uint32_t n, uint8_t v)
{
    for (uint32_t i = 0; i < n; i++)
        if (p[i] != v)
            return false;
    return n != 0;
}

} // namespace

TEST_CASE("mount reports IMD geometry")
{
    Mounted m(two_by_two());

    REQUIRE(m.type == MEDIATYPE_IMD);
    CHECK(m.media._disktype == MEDIATYPE_IMD);

    SUBCASE("percom describes the geometry rectangle")
    {
        CHECK(m.media._percomBlock.num_tracks == 2);         // cylinders per side
        CHECK(m.media._percomBlock.num_sides == 1);          // 0 = SS, 1 = DS
        CHECK(m.media._percomBlock.sectors_per_track == 4);
        CHECK(m.media._percomBlock.density == DENSITY_MFM);
        CHECK(m.media._percomBlock.sector_size == 128);
        CHECK(m.media._percomBlock.drive_present == 0xFF);
    }

    SUBCASE("single sided FM image reports FM and one side")
    {
        IMD b;
        b.header();
        b.simple_track(0, 0, 0, 4, 0x11); // mode 0 = FM 500kbps
        Mounted fm(b);

        REQUIRE(fm.type == MEDIATYPE_IMD);
        CHECK(fm.media._percomBlock.num_sides == 0);
        CHECK(fm.media._percomBlock.density == DENSITY_FM);
    }
}

TEST_CASE("drive sectors are IMD LBAs, 0-based")
{
    Mounted m(two_by_two());
    REQUIRE(m.type == MEDIATYPE_IMD);

    uint32_t readcount = 0;

    SUBCASE("sector 0 is the first sector of the first track")
    {
        CHECK(m.media.read(0, &readcount).is_success());
        CHECK(readcount == 128);
        CHECK(all_equal(m.media._disk_sectorbuff, 128, 0xA0));
    }

    SUBCASE("the LBA space runs across tracks in order")
    {
        CHECK(m.media.read(4, &readcount).is_success());
        CHECK(all_equal(m.media._disk_sectorbuff, 128, 0xA1));

        CHECK(m.media.read(8, &readcount).is_success());
        CHECK(all_equal(m.media._disk_sectorbuff, 128, 0xB0));

        CHECK(m.media.read(15, &readcount).is_success());
        CHECK(all_equal(m.media._disk_sectorbuff, 128, 0xB1));
    }

    SUBCASE("one past the last sector is an error, not a wrap")
    {
        REQUIRE(m.media.read(15, &readcount).is_success());

        CHECK(m.media.read(16, &readcount).is_error());
        CHECK(readcount == 0);
        uint8_t st[4] = {};
        m.media.status(st);
        CHECK(has(ctrl(st), DISK_CTRL_STATUS_SECTOR_MISSING));
    }
}

TEST_CASE("sector_size reports the real per-sector size")
{
    // 128-byte track 0, then a 512-byte track: a mixed density CP/M disk
    IMD b;
    b.header();
    b.simple_track(3, 0, 0, 2, 0x77);
    b.thdr(3, 1, 0, 2, 2); // size code 2 = 512
    b.map({1, 2});
    b.comp(0x02, 0x33);
    b.comp(0x02, 0x44);
    Mounted m(b);
    REQUIRE(m.type == MEDIATYPE_IMD);

    CHECK(m.media.sector_size(0) == 128);
    CHECK(m.media.sector_size(1) == 128);
    CHECK(m.media.sector_size(2) == 512);
    CHECK(m.media.sector_size(3) == 512);

    SUBCASE("out of range is 0, which the bus turns into a rejected write")
    {
        CHECK(m.media.sector_size(4) == 0);
    }

    SUBCASE("a read returns the sector's own length")
    {
        uint32_t readcount = 0;
        CHECK(m.media.read(0, &readcount).is_success());
        CHECK(readcount == 128);

        CHECK(m.media.read(2, &readcount).is_success());
        CHECK(readcount == 512);
        CHECK(all_equal(m.media._disk_sectorbuff, 512, 0x33));
    }
}

TEST_CASE("sectors larger than the bus buffer refuse the mount")
{
    IMD b;
    b.header();
    b.thdr(3, 0, 0, 1, 3); // size code 3 = 1024, over DISK_SECTORBUF_SIZE
    b.map({1});
    b.comp(0x02, 0x5A);

    Mounted m(b);
    CHECK(m.type == MEDIATYPE_UNKNOWN);
}

TEST_CASE("a non-IMD file refuses the mount")
{
    IMD b;
    b.pad(512, 0xE5); // no comment terminator anywhere

    Mounted m(b);
    CHECK(m.type == MEDIATYPE_UNKNOWN);
}

TEST_CASE("read-only mounts refuse writes")
{
    Mounted m(two_by_two(), false);
    REQUIRE(m.type == MEDIATYPE_IMD);

    memset(m.media._disk_sectorbuff, 0x5A, 128);
    CHECK(m.media.write(0, false).is_error());

    uint8_t st[4] = {};
    m.media.status(st);
    CHECK(has(st[0], DISK_DRIVE_STATUS_WRITE_PROTECT_ERROR));
    CHECK(has(ctrl(st), DISK_CTRL_STATUS_WRITE_PROTECT_ERROR));

    SUBCASE("and the sector still reads back unchanged")
    {
        uint32_t readcount = 0;
        REQUIRE(m.media.read(0, &readcount).is_success());
        CHECK(all_equal(m.media._disk_sectorbuff, 128, 0xA0));
    }
}

TEST_CASE("writable mounts round-trip a sector in place")
{
    Mounted m(two_by_two(), true);
    REQUIRE(m.type == MEDIATYPE_IMD);

    // A uniform buffer fits a compressed record in place
    memset(m.media._disk_sectorbuff, 0x5A, 128);
    REQUIRE(m.media.write(6, false).is_success());

    uint32_t readcount = 0;
    REQUIRE(m.media.read(6, &readcount).is_success());
    CHECK(readcount == 128);
    CHECK(all_equal(m.media._disk_sectorbuff, 128, 0x5A));

    SUBCASE("a neighboring sector is untouched")
    {
        REQUIRE(m.media.read(5, &readcount).is_success());
        CHECK(all_equal(m.media._disk_sectorbuff, 128, 0xA1));
    }

    SUBCASE("a non-uniform buffer is stored")
    {
        memset(m.media._disk_sectorbuff, 0x11, 128);
        m.media._disk_sectorbuff[64] = 0x22;
        REQUIRE(m.media.write(6, false).is_success());

        REQUIRE(m.media.read(6, &readcount).is_success());
        CHECK(m.media._disk_sectorbuff[0] == 0x11);
        CHECK(m.media._disk_sectorbuff[64] == 0x22);
        REQUIRE(m.media.read(5, &readcount).is_success());
        CHECK(all_equal(m.media._disk_sectorbuff, 128, 0xA1));
    }

    SUBCASE("past the end is refused")
    {
        CHECK(m.media.write(16, false).is_error());
    }
}

TEST_CASE("damaged records are reported but still delivered")
{
    IMD b;
    b.header();
    b.thdr(3, 0, 0, 3, 0);
    b.map({1, 2, 3});
    b.comp(0x06, 0xDE);   // compressed + data error
    b.comp(0x04, 0xAD);   // deleted address mark
    b.unavailable();      // never readable
    Mounted m(b, true);
    REQUIRE(m.type == MEDIATYPE_IMD);

    uint32_t readcount = 0;
    uint8_t  st[4] = {};

    SUBCASE("a data error still returns the recovered contents")
    {
        CHECK(m.media.read(0, &readcount).is_success());
        CHECK(readcount == 128);
        CHECK(all_equal(m.media._disk_sectorbuff, 128, 0xDE));

        m.media.status(st);
        CHECK(has(ctrl(st), DISK_CTRL_STATUS_CRC_ERROR));
    }

    SUBCASE("a deleted address mark is ordinary data plus a flag")
    {
        CHECK(m.media.read(1, &readcount).is_success());
        CHECK(all_equal(m.media._disk_sectorbuff, 128, 0xAD));

        m.media.status(st);
        CHECK(has(ctrl(st), DISK_CTRL_STATUS_SECTOR_DELETED));
    }

    SUBCASE("an unavailable sector is a missing sector, never fabricated")
    {
        CHECK(m.media.read(2, &readcount).is_error());
        CHECK(readcount == 0);

        m.media.status(st);
        CHECK(has(ctrl(st), DISK_CTRL_STATUS_SECTOR_MISSING));
    }

    SUBCASE("an unavailable sector cannot be written either")
    {
        memset(m.media._disk_sectorbuff, 0x5A, 128);
        CHECK(m.media.write(2, false).is_error());

        m.media.status(st);
        CHECK(has(ctrl(st), DISK_CTRL_STATUS_SECTOR_MISSING));
    }

    SUBCASE("the flags do not stick to the next good read")
    {
        REQUIRE(m.media.read(0, &readcount).is_success());
        REQUIRE(m.media.read(1, &readcount).is_success());

        m.media.status(st);
        CHECK_FALSE(has(ctrl(st), DISK_CTRL_STATUS_CRC_ERROR));
    }
}

TEST_CASE("status reflects the media")
{
    uint8_t st[4] = {};

    SUBCASE("MFM double sided")
    {
        Mounted m(two_by_two(), true);
        REQUIRE(m.type == MEDIATYPE_IMD);
        m.media.status(st);
        CHECK(has(st[0], DISK_DRIVE_STATUS_DOUBLE_DENSITY));
        CHECK(has(st[0], DISK_DRIVE_STATUS_DOUBLE_SIDED));
        CHECK_FALSE(has(st[0], DISK_DRIVE_STATUS_WRITE_PROTECT_ERROR));
    }

    SUBCASE("8 inch SSSD CP/M is FM, single sided, 26 sectors per track")
    {
        IMD b;
        b.header();
        b.simple_track(0, 0, 0, 26, 0xE5);
        Mounted m(b);
        REQUIRE(m.type == MEDIATYPE_IMD);

        m.media.status(st);
        CHECK_FALSE(has(st[0], DISK_DRIVE_STATUS_DOUBLE_DENSITY));
        CHECK_FALSE(has(st[0], DISK_DRIVE_STATUS_DOUBLE_SIDED));
        CHECK(has(st[0], DISK_DRIVE_STATUS_ENHANCED_DENSITY));
        CHECK(has(st[0], DISK_DRIVE_STATUS_WRITE_PROTECT_ERROR));
    }
}

TEST_CASE("format is refused rather than faked")
{
    Mounted m(two_by_two(), true);
    REQUIRE(m.type == MEDIATYPE_IMD);

    uint32_t responsesize = 0xDEADBEEF;
    CHECK(m.media.format(&responsesize).is_error());
    CHECK(responsesize == 0);
}

TEST_CASE("discover_mediatype routes .imd to the adapter")
{
    CHECK(MediaType::discover_mediatype("disk.imd") == MEDIATYPE_IMD);
    CHECK(MediaType::discover_mediatype("DISK.IMD") == MEDIATYPE_IMD);
    CHECK(MediaType::discover_mediatype("disk.img") == MEDIATYPE_UNKNOWN);
    CHECK(MediaType::discover_mediatype("game.rom") == MEDIATYPE_ROM);
    CHECK(MediaType::discover_mediatype("load.xex") == MEDIATYPE_IMG);
}

// The sectors mkfs.cpm -f ibm-3740 leaves, each as a full record
TEST_CASE("create writes a blank IBM-3740 image")
{
    fnFile *fp = fopen(IMD_TEST_FILE, "w+b");
    REQUIRE(fp != nullptr);
    REQUIRE(MediaTypeIMD::create(fp, 128, 77 * 26).is_success());
    fseek(fp, 0, SEEK_END);
    std::vector<uint8_t> got(ftell(fp));
    rewind(fp);
    REQUIRE(fread(got.data(), 1, got.size(), fp) == got.size());
    fclose(fp);
    remove(IMD_TEST_FILE);

    // "IMD 1.18: dd/mm/yyyy hh:mm:ss\r\n" then the terminator
    const size_t hdr = 31;
    REQUIRE(got.size() > hdr);
    CHECK(memcmp(got.data(), "IMD 1.18: ", 10) == 0);
    CHECK(got[12] == '/');
    CHECK(got[15] == '/');
    CHECK(got[23] == ':');
    CHECK(got[26] == ':');
    CHECK(memcmp(&got[29], "\r\n\x1A", 3) == 0);

    std::vector<uint8_t> want;
    for (uint8_t cyl = 0; cyl < 77; cyl++)
    {
        want.insert(want.end(), {0x00, cyl, 0x00, 26, 0x00});
        for (uint8_t id = 1; id <= 26; id++)
            want.push_back(id);
        for (int i = 0; i < 26; i++)
        {
            want.push_back(0x01);
            want.insert(want.end(), 128, (uint8_t)(cyl < 3 ? 0xE5 : 0x00));
        }
    }
    CHECK(got.size() == hdr + 1 + want.size());
    CHECK(std::equal(want.begin(), want.end(), got.begin() + hdr + 1));
}

TEST_CASE("an image create() made mounts with E5 through the directory track")
{
    fnFile *fp = fopen(IMD_TEST_FILE, "w+b");
    REQUIRE(fp != nullptr);
    REQUIRE(MediaTypeIMD::create(fp, 128, 77 * 26).is_success());
    uint32_t size = (uint32_t)ftell(fp);
    rewind(fp);

    MediaTypeIMD media(false);
    REQUIRE(media.mount(fp, size) == MEDIATYPE_IMD);
    uint32_t n = 0;
    CHECK(media.sector_size(0) == 128);
    REQUIRE(media.read(0, &n).is_success());
    CHECK(all_equal(media._disk_sectorbuff, n, 0xE5));
    REQUIRE(media.read(3 * 26 - 1, &n).is_success());
    CHECK(all_equal(media._disk_sectorbuff, n, 0xE5));
    REQUIRE(media.read(3 * 26, &n).is_success());
    CHECK(all_equal(media._disk_sectorbuff, n, 0x00));
    REQUIRE(media.read(77 * 26 - 1, &n).is_success());
    CHECK(media.read(77 * 26, &n).is_error());
    media.unmount(); // closes fp
    remove(IMD_TEST_FILE);
}

TEST_CASE("an image create() made takes real data in any sector")
{
    fnFile *fp = fopen(IMD_TEST_FILE, "w+b");
    REQUIRE(fp != nullptr);
    REQUIRE(MediaTypeIMD::create(fp, 128, 77 * 26).is_success());
    uint32_t size = (uint32_t)ftell(fp);
    rewind(fp);

    MediaTypeIMD media(true);
    REQUIRE(media.mount(fp, size) == MEDIATYPE_IMD);
    for (uint32_t lba : {0u, 3u * 26, 77u * 26 - 1})
    {
        CAPTURE(lba);
        for (int i = 0; i < 128; i++)
            media._disk_sectorbuff[i] = (uint8_t)(lba + i);
        CHECK(media.write(lba, false).is_success());
        memset(media._disk_sectorbuff, 0, 128);
        uint32_t n = 0;
        REQUIRE(media.read(lba, &n).is_success());
        CHECK(media._disk_sectorbuff[0] == (uint8_t)lba);
        CHECK(media._disk_sectorbuff[127] == (uint8_t)(lba + 127));
    }
    media.unmount(); // closes fp
    remove(IMD_TEST_FILE);
}

TEST_CASE("create refuses any geometry but IBM-3740")
{
    struct { uint16_t sectorSize; uint32_t numSectors; } sizes[] = {
        {128, 720}, {256, 77 * 26}, {512, 77 * 26}, {128, 0}, {0, 77 * 26},
    };
    for (auto s : sizes)
    {
        CAPTURE(s.sectorSize);
        CAPTURE(s.numSectors);
        fnFile *fp = fopen(IMD_TEST_FILE, "w+b");
        REQUIRE(fp != nullptr);
        CHECK(MediaTypeIMD::create(fp, s.sectorSize, s.numSectors).is_error());
        fseek(fp, 0, SEEK_END);
        CHECK(ftell(fp) == 0);
        fclose(fp);
        remove(IMD_TEST_FILE);
    }
}
