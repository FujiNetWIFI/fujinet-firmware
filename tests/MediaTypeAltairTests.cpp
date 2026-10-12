#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdio>
#include <cstring>
#include <vector>

#include "diskTypeAltair.h"

namespace
{

// The build forces FNIO_IS_STDIO for this target, so fnFile is std::FILE.
#define ALTAIR_TEST_FILE "mediatypealtair_test.tmp"

// Byte i of drive sector (track, sector), so a read proves where it landed.
uint8_t pattern(uint32_t lba, uint32_t i)
{
    return (uint8_t)(lba * 7 + i);
}

// Writes an image of `size` bytes whose first `sectors` sectors carry pattern()
// and whose tail is padding, then mounts it as rs232Disk::mount() would.
struct Mounted
{
    fnFile *fp = nullptr;
    MediaTypeAltair media;
    mediatype_t type = MEDIATYPE_UNKNOWN;

    Mounted(uint32_t size, uint32_t sectors, bool writable = false) : media(writable)
    {
        fp = fopen(ALTAIR_TEST_FILE, "w+b");
        REQUIRE(fp != nullptr);
        std::vector<uint8_t> sec(ALTAIR_SECTOR_SIZE);
        for (uint32_t lba = 0; lba < sectors; lba++)
        {
            for (uint32_t i = 0; i < ALTAIR_SECTOR_SIZE; i++)
                sec[i] = pattern(lba, i);
            REQUIRE(fwrite(sec.data(), 1, sec.size(), fp) == sec.size());
        }
        // Padding (and the bulk of the 8 MB image) is a sparse zero tail.
        if (size > sectors * ALTAIR_SECTOR_SIZE)
        {
            REQUIRE(fseek(fp, size - 1, SEEK_SET) == 0);
            REQUIRE(fputc(0, fp) == 0);
        }
        rewind(fp);
        type = media.mount(fp, size);
    }

    ~Mounted()
    {
        media.unmount(); // closes fp
        remove(ALTAIR_TEST_FILE);
    }

    long file_size()
    {
        fseek(fp, 0, SEEK_END);
        return ftell(fp);
    }
};

bool matches(const uint8_t *p, uint32_t lba)
{
    for (uint32_t i = 0; i < ALTAIR_SECTOR_SIZE; i++)
        if (p[i] != pattern(lba, i))
            return false;
    return true;
}

uint8_t ctrl(const uint8_t st[4])
{
    return (uint8_t)~st[1];
}

} // namespace

TEST_CASE("only .DSK files of an FDC+ size are Altair images")
{
    CHECK(MediaTypeAltair::looks_like_altair("cpm.dsk", 337568));
    CHECK(MediaTypeAltair::looks_like_altair("CPM.DSK", 337664));
    CHECK(MediaTypeAltair::looks_like_altair("mini.Dsk", 76720));
    CHECK(MediaTypeAltair::looks_like_altair("mini.dsk", 76800));
    CHECK(MediaTypeAltair::looks_like_altair("big.dsk", 8978432));

    CHECK_FALSE(MediaTypeAltair::looks_like_altair("cpm.dsk", 337567));
    CHECK_FALSE(MediaTypeAltair::looks_like_altair("pc.dsk", 368640));
    CHECK_FALSE(MediaTypeAltair::looks_like_altair("cpm.img", 337568));
    CHECK_FALSE(MediaTypeAltair::looks_like_altair("dsk", 337568));
    CHECK_FALSE(MediaTypeAltair::looks_like_altair(nullptr, 337568));

    // The extension alone still means a plain image to discover_mediatype().
    CHECK(MediaType::discover_mediatype("cpm.dsk") == MEDIATYPE_UNKNOWN);
}

TEST_CASE("an 8 inch image mounts with its geometry in the PERCOM block")
{
    Mounted m(337664, 77 * 32);
    REQUIRE(m.type == MEDIATYPE_ALTAIR);
    CHECK(m.media._disktype == MEDIATYPE_ALTAIR);
    CHECK(m.media._percomBlock.num_tracks == 77);
    CHECK(m.media._percomBlock.sectors_per_track == 32);
    CHECK(m.media._percomBlock.sector_size == ALTAIR_SECTOR_SIZE);
    CHECK(m.media._percomBlock.drive_present == 0xFF);
    CHECK(m.media.sector_size(0) == ALTAIR_SECTOR_SIZE);
}

TEST_CASE("a minidisk and the 8 MB drive report their geometry")
{
    {
        Mounted m(76720, 35 * 16);
        REQUIRE(m.type == MEDIATYPE_ALTAIR);
        CHECK(m.media._percomBlock.num_tracks == 35);
        CHECK(m.media._percomBlock.sectors_per_track == 16);
    }
    {
        Mounted m(8978432, 64);
        REQUIRE(m.type == MEDIATYPE_ALTAIR);
        CHECK(m.media._percomBlock.num_tracks == 0);
        CHECK(m.media._percomBlock.sectors_per_track == 32);

        uint32_t n = 0;
        CHECK(m.media.read(2048 * 32 - 1, &n).is_success());
        CHECK(n == ALTAIR_SECTOR_SIZE);
        CHECK(m.media.read(2048 * 32, &n).is_error());
    }
}

TEST_CASE("a size outside the FDC+ set refuses the mount")
{
    Mounted m(337600, 0);
    CHECK(m.type == MEDIATYPE_UNKNOWN);
}

TEST_CASE("drive sector N is 137 bytes at N * 137")
{
    Mounted m(337568, 77 * 32);
    REQUIRE(m.type == MEDIATYPE_ALTAIR);

    for (uint32_t lba : {0u, 1u, 31u, 32u, 1000u, 77u * 32 - 1})
    {
        uint32_t n = 0;
        REQUIRE(m.media.read(lba, &n).is_success());
        CHECK(n == ALTAIR_SECTOR_SIZE);
        CHECK(matches(m.media._disk_sectorbuff, lba));
    }

    uint32_t n = 99;
    CHECK(m.media.read(77 * 32, &n).is_error());
    CHECK(n == 0);
    uint8_t st[4] = {};
    m.media.status(st);
    CHECK((ctrl(st) & DISK_CTRL_STATUS_SECTOR_MISSING) != 0);
}

TEST_CASE("a write lands in place and never grows the file")
{
    Mounted m(337664, 77 * 32, true);
    REQUIRE(m.type == MEDIATYPE_ALTAIR);

    // The last sector sits next to the padding, where a 512-byte image would grow.
    const uint32_t lba = 77 * 32 - 1;
    memset(m.media._disk_sectorbuff, 0xE5, ALTAIR_SECTOR_SIZE);
    REQUIRE(m.media.write(lba, false).is_success());
    CHECK(m.file_size() == 337664);

    memset(m.media._disk_sectorbuff, 0, sizeof(m.media._disk_sectorbuff));
    uint32_t n = 0;
    REQUIRE(m.media.read(lba, &n).is_success());
    for (uint32_t i = 0; i < ALTAIR_SECTOR_SIZE; i++)
        REQUIRE(m.media._disk_sectorbuff[i] == 0xE5);

    REQUIRE(m.media.read(lba - 1, &n).is_success());
    CHECK(matches(m.media._disk_sectorbuff, lba - 1));

    CHECK(m.media.write(77 * 32, false).is_error());
    CHECK(m.file_size() == 337664);
}

TEST_CASE("a read-only mount refuses writes and says so")
{
    Mounted m(337568, 77 * 32, false);
    REQUIRE(m.type == MEDIATYPE_ALTAIR);

    CHECK(m.media.write(0, false).is_error());

    uint8_t st[4] = {};
    m.media.status(st);
    CHECK((st[0] & DISK_DRIVE_STATUS_WRITE_PROTECT_ERROR) != 0);
    CHECK((ctrl(st) & DISK_CTRL_STATUS_WRITE_PROTECT_ERROR) != 0);

    uint32_t n = 0;
    REQUIRE(m.media.read(0, &n).is_success());
    CHECK(matches(m.media._disk_sectorbuff, 0));
}
