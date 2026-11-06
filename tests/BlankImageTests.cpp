#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdio>
#include <vector>

#include "BlankImage.h"

namespace
{

// The build forces FNIO_IS_STDIO for this target, so fnFile is std::FILE.
#define BLANK_TEST_FILE "blankimage_test.tmp"

// Where the sectors with something in them start: 18/0 on a 1541 and a 1571, 53/0 on a 1571's
// second side, 40/0 on a 1581
const size_t D64_BAM = 0x16500;
const size_t D71_SIDE_2_BAM = 0x41000;
const size_t D81_HEADER = 0x61800;

// A fresh file for create() to write
struct TempFile
{
    fnFile *fp;

    TempFile() : fp(fopen(BLANK_TEST_FILE, "w+b")) { REQUIRE(fp != nullptr); }
    ~TempFile()
    {
        fclose(fp);
        remove(BLANK_TEST_FILE);
    }

    long size()
    {
        fseek(fp, 0, SEEK_END);
        return ftell(fp);
    }

    std::vector<uint8_t> bytes()
    {
        std::vector<uint8_t> b(size());
        rewind(fp);
        REQUIRE(fread(b.data(), 1, b.size(), fp) == b.size());
        return b;
    }
};

// What create() writes for a name; by default 720 sectors of 128 bytes, as the C64 test asks
std::vector<uint8_t> image(const char *name, uint16_t sectorSize = 128,
                           uint16_t numSectors = 720)
{
    TempFile t;
    REQUIRE(BlankImage::create(t.fp, name, sectorSize, numSectors).is_success());
    return t.bytes();
}

// Indexes of the 256-byte sectors that aren't all zeros
std::vector<size_t> used_sectors(const std::vector<uint8_t> &img)
{
    std::vector<size_t> used;
    for (size_t s = 0; s < img.size() / 256; s++)
        if (std::any_of(&img[s * 256], &img[s * 256] + 256, [](uint8_t b) { return b != 0; }))
            used.push_back(s);
    return used;
}

unsigned bits_set(const uint8_t *p, size_t n)
{
    unsigned bits = 0;
    for (size_t i = 0; i < n; i++)
        for (int b = 0; b < 8; b++)
            bits += (p[i] >> b) & 1;
    return bits;
}

// Free blocks as DOS counts them: the free counts of the entries `size` bytes apart from
// `first` on, `tracks` of them, but for the directory track `skip` (0 for none). The bitmaps
// after each count must hold that many bits.
unsigned free_blocks(const uint8_t *first, unsigned tracks, unsigned size, unsigned skip)
{
    unsigned total = 0;
    for (unsigned t = 1; t <= tracks; t++)
    {
        const uint8_t *entry = first + size * (t - 1);
        CHECK(bits_set(entry + 1, size - 1) == entry[0]);
        if (t != skip)
            total += entry[0];
    }
    return total;
}

std::vector<uint8_t> disk_name(const std::vector<uint8_t> &img, size_t offset)
{
    return std::vector<uint8_t>(&img[offset], &img[offset] + 16);
}

} // namespace

TEST_CASE("a name that is not a Commodore disk gets numSectors * sectorSize zero bytes")
{
    struct { const char *name; uint16_t sectorSize; uint16_t numSectors; } cases[] = {
        {"/fnnewdisk.img", 128, 720}, // 92160 bytes, as fujinet-lib's disk test asks
        {"blank.dsk", 256, 3},
        {"blank.prg", 512, 1},
        {"noextension", 128, 1},
        {"x.d6", 128, 2}, // too short to be a .d64
    };
    for (auto c : cases)
    {
        CAPTURE(c.name);
        TempFile t;
        CHECK(BlankImage::create(t.fp, c.name, c.sectorSize, c.numSectors).is_success());
        auto bytes = t.bytes();
        CHECK(bytes.size() == (size_t)c.sectorSize * c.numSectors);
        CHECK(std::count(bytes.begin(), bytes.end(), 0) == (long)bytes.size());
    }
}

TEST_CASE("a size of 0 is refused and writes nothing")
{
    TempFile t;
    CHECK(BlankImage::create(t.fp, "/blank.img", 128, 0).is_error());
    CHECK(BlankImage::create(t.fp, "/blank.img", 0, 720).is_error());
    CHECK(BlankImage::create(t.fp, "/blank.img", 0, 0).is_error());
    CHECK(t.size() == 0);
}

TEST_CASE("a size past 16 MiB is refused and writes nothing")
{
    TempFile t;
    CHECK(BlankImage::create(t.fp, "/blank.img", 65535, 65535).is_error());
    CHECK(BlankImage::create(t.fp, "/blank.img", 4096, 4097).is_error());
    CHECK(t.size() == 0);
}

TEST_CASE("16 MiB is the largest raw image")
{
    TempFile t;
    CHECK(BlankImage::create(t.fp, "/blank.img", 4096, 4096).is_success());
    CHECK(t.size() == 16 * 1024 * 1024);
}

TEST_CASE("a .d64 is a 35-track image with every block free")
{
    auto img = image("/blank.d64");
    REQUIRE(img.size() == 174848);

    const uint8_t *bam = &img[D64_BAM];
    CHECK(bam[0] == 18); // the first directory sector is 18/1
    CHECK(bam[1] == 1);
    CHECK(bam[2] == 'A'); // 1541 DOS version
    CHECK(bam[3] == 0);
    CHECK(free_blocks(bam + 4, 35, 4, 18) == 664);
    CHECK(bam[4 + 4 * 17] == 17); // track 18's 19 sectors less the BAM and the directory
    CHECK((bam[4 + 4 * 17 + 1] & 3) == 0);

    // The directory is empty and the last sector of its chain
    CHECK(img[D64_BAM + 256] == 0);
    CHECK(img[D64_BAM + 256 + 1] == 0xFF);
    CHECK(used_sectors(img) == std::vector<size_t>{357, 358});
}

TEST_CASE("a .d64 header holds the name, an ID and the DOS type")
{
    auto img = image("/games/My Disk.d64");
    const uint8_t *header = &img[D64_BAM + 0x90];

    const uint8_t name[] = {'M', 'Y', ' ', 'D', 'I', 'S', 'K', 0xA0, 0xA0, 0xA0, 0xA0, 0xA0,
                            0xA0, 0xA0, 0xA0, 0xA0};
    CHECK(disk_name(img, D64_BAM + 0x90) == std::vector<uint8_t>(name, name + 16));
    CHECK(header[16] == 0xA0);
    CHECK(header[17] == 0xA0);
    CHECK(header[18] == 'F'); // the ID
    CHECK(header[19] == 'N');
    CHECK(header[20] == 0xA0);
    CHECK(header[21] == '2'); // the DOS type
    CHECK(header[22] == 'A');
    CHECK(header[23] == 0xA0);
}

TEST_CASE("a disk name is the file name in upper case, at most 16 characters")
{
    auto img = image("/a.b/this is a very long disk name.d64");
    const char *want = "THIS IS A VERY L";
    CHECK(disk_name(img, D64_BAM + 0x90) == std::vector<uint8_t>(want, want + 16));

    // Whatever isn't printable PETSCII becomes a space
    img = image("a\tb\x7f.d64");
    CHECK(disk_name(img, D64_BAM + 0x90)[1] == ' ');
    CHECK(disk_name(img, D64_BAM + 0x90)[3] == ' ');
}

TEST_CASE("the extension is matched in any case, and the size asked for does not matter")
{
    auto lower = image("/disk.d64");
    CHECK(image("/disk.D64") == lower);
    CHECK(image("/disk.d64", 512, 3) == lower);
    CHECK(image("/disk.d64", 0, 0) == lower);
}

TEST_CASE("a .d71 is a double-sided image with every block free")
{
    auto img = image("/blank.d71");
    REQUIRE(img.size() == 349696);

    const uint8_t *bam = &img[D64_BAM];
    CHECK(bam[0] == 18);
    CHECK(bam[1] == 1);
    CHECK(bam[2] == 'A');
    CHECK(bam[3] == 0x80); // double sided

    // The first side is the 1541's; the second side's counts follow the name, its bitmaps
    // are in 53/0, and the 1571 allocates all of track 53
    const uint8_t *counts = bam + 0xDD;
    const uint8_t *side_2 = &img[D71_SIDE_2_BAM];
    unsigned total = free_blocks(bam + 4, 35, 4, 18);
    for (unsigned t = 36; t <= 70; t++)
    {
        const uint8_t *bitmap = side_2 + 3 * (t - 36);
        CHECK(bits_set(bitmap, 3) == counts[t - 36]);
        if (t != 53)
            total += counts[t - 36];
    }
    CHECK(total == 1328);
    CHECK(counts[53 - 36] == 0);
    CHECK(used_sectors(img) == std::vector<size_t>{357, 358, 1040});
}

TEST_CASE("a .d81 is an 80-track image with every block free")
{
    auto img = image("/blank.d81");
    REQUIRE(img.size() == 819200);

    const uint8_t *header = &img[D81_HEADER];
    CHECK(header[0] == 40); // the first directory sector is 40/3
    CHECK(header[1] == 3);
    CHECK(header[2] == 'D'); // 1581 DOS version
    const uint8_t id[] = {'F', 'N'};
    CHECK(std::equal(id, id + 2, header + 0x16));
    CHECK(header[0x19] == '3'); // the DOS type
    CHECK(header[0x1A] == 'D');

    // Two BAM sectors of 40 tracks each, linked, and track 40's first four sectors in use
    const uint8_t *first = header + 256;
    const uint8_t *second = header + 512;
    CHECK(first[0] == 40);
    CHECK(first[1] == 2);
    CHECK(second[0] == 0);
    CHECK(second[1] == 0xFF);
    CHECK(first[3] == 0xBB);
    CHECK(second[3] == 0xBB);
    CHECK(free_blocks(first + 0x10, 40, 6, 40) + free_blocks(second + 0x10, 40, 6, 0) == 3160);
    CHECK(first[0x10 + 6 * 39] == 36);
    CHECK((first[0x10 + 6 * 39 + 1] & 0x0F) == 0);

    CHECK(header[512 + 256] == 0);
    CHECK(header[512 + 256 + 1] == 0xFF);
    CHECK(used_sectors(img) == std::vector<size_t>{1560, 1561, 1562, 1563});
}
