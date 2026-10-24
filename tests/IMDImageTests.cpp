#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "IMDImage.h"
#include "IMDFixture.h"

namespace
{

// Not tmpfile(): under MSYS2 that inherits msvcrt's create-in-the-drive-root
// behavior, and Windows is in the PC CI matrix.
#define IMD_TEST_FILE "imdimage_test.tmp"

// Keeps file and image together; IMDImage does not own the file.
struct Mounted
{
    std::fstream           file;
    IMDImage img{&file};
    IMDStatus              st = IMDStatus::IoError;

    Mounted(const IMD &b, bool writable = false)
    {
        {
            std::ofstream out(IMD_TEST_FILE, std::ios::binary | std::ios::trunc);
            REQUIRE(out.write((const char *)b.out.data(), (std::streamsize)b.out.size()));
        }
        file.open(IMD_TEST_FILE, std::ios::in | std::ios::out | std::ios::binary);
        REQUIRE(file.is_open());
        st = img.open(writable);
    }

    ~Mounted()
    {
        img.close();
        file.close();
        remove(IMD_TEST_FILE);
    }

    long file_size()
    {
        file.flush();
        file.clear();
        file.seekg(0, std::ios::end);
        return (long)file.tellg();
    }
};

std::vector<uint8_t> read_lba(IMDImage &img, uint32_t lba, IMDStatus &st)
{
    std::vector<uint8_t> buf(img.sectorSize(lba));
    uint16_t             got = 0;
    st = img.readSector(lba, buf.data(), (uint32_t)buf.size(), &got);
    buf.resize(got);
    return buf;
}

bool all_equal(const std::vector<uint8_t> &v, uint8_t b)
{
    for (uint8_t x : v)
        if (x != b)
            return false;
    return !v.empty();
}

} // namespace

TEST_CASE("header and comment handling")
{
    SUBCASE("signature line is stripped, comment is kept")
    {
        Mounted m(IMD().header().simple_track(0, 0, 0, 1, 0xE5));
        REQUIRE(m.st == IMDStatus::Ok);
        CHECK(std::string(m.img.comment()) == "test\r\n");
    }

    SUBCASE("signature is optional - SIMH omits it entirely")
    {
        Mounted m(IMD().header(nullptr, "just a comment\r\n").simple_track(0, 0, 0, 2, 0x11));
        REQUIRE(m.st == IMDStatus::Ok);
        CHECK(m.img.lbaCount() == 2);
        CHECK(std::string(m.img.comment()) == "just a comment\r\n");
    }

    SUBCASE("space-padded hour is accepted")
    {
        Mounted m(IMD().header("IMD 1.17: 17/01/2010  8:16:34\r\n", "cpm\r\n")
                      .simple_track(0, 0, 0, 1, 0xE5));
        REQUIRE(m.st == IMDStatus::Ok);
        CHECK(std::string(m.img.comment()) == "cpm\r\n");
    }

    SUBCASE("no 0x1A terminator is not an IMD")
    {
        IMD b;
        for (const char *p = "IMD 1.18: no terminator here"; *p; p++)
            b.out.push_back((uint8_t)*p);
        Mounted m(b);
        CHECK(m.st == IMDStatus::NotIMD);
        CHECK_FALSE(m.img.isOpen());
    }

    SUBCASE("terminator with no tracks is not a usable image")
    {
        IMD b;
        b.out.push_back(IMD_EOF_MARKER);
        Mounted m(b);
        CHECK(m.st == IMDStatus::NotIMD);
    }
}

TEST_CASE("all nine sector record types")
{
    IMD b;
    b.header().thdr(0, 0, 0, 9, 0).map({1, 2, 3, 4, 5, 6, 7, 8, 9});
    b.unavailable();          // 0x00
    b.full(0x01, 128, 0x11);  // normal
    b.comp(0x02, 0x22);       // compressed
    b.full(0x03, 128, 0x33);  // deleted
    b.comp(0x04, 0x44);       // compressed deleted
    b.full(0x05, 128, 0x55);  // error
    b.comp(0x06, 0x66);       // compressed error
    b.full(0x07, 128, 0x77);  // deleted error
    b.comp(0x08, 0x88);       // compressed deleted error

    Mounted m(b);
    REQUIRE(m.st == IMDStatus::Ok);
    REQUIRE(m.img.lbaCount() == 9);

    IMDSectorInfo si;

    SUBCASE("unavailable sectors are never fabricated")
    {
        REQUIRE(m.img.sectorInfo(0, si) == IMDStatus::Ok);
        CHECK(si.unavailable);
        IMDStatus st;
        read_lba(m.img, 0, st);
        CHECK(st == IMDStatus::Unavailable);
    }

    SUBCASE("flags decode from the (type-1) bit field")
    {
        struct { uint32_t lba; bool comp, del, err; } expect[] = {
            {1, false, false, false}, {2, true,  false, false},
            {3, false, true,  false}, {4, true,  true,  false},
            {5, false, false, true},  {6, true,  false, true},
            {7, false, true,  true},  {8, true,  true,  true},
        };
        for (auto &e : expect)
        {
            REQUIRE(m.img.sectorInfo(e.lba, si) == IMDStatus::Ok);
            CHECK(si.compressed == e.comp);
            CHECK(si.deleted == e.del);
            CHECK(si.had_error == e.err);
            CHECK_FALSE(si.unavailable);
        }
    }

    SUBCASE("compressed records expand to a full sector")
    {
        uint8_t fills[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
        for (uint32_t i = 0; i < 8; i++)
        {
            IMDStatus st;
            auto d = read_lba(m.img, i + 1, st);
            CHECK(st == IMDStatus::Ok);
            CHECK(d.size() == 128);
            CHECK(all_equal(d, fills[i]));
        }
    }

    SUBCASE("data-error records still return their recovered contents")
    {
        IMDStatus st;
        auto d = read_lba(m.img, 5, st);
        CHECK(st == IMDStatus::Ok);
        REQUIRE(m.img.sectorInfo(5, si) == IMDStatus::Ok);
        CHECK(si.had_error);
        CHECK(all_equal(d, 0x55));
    }
}

TEST_CASE("malformed structures")
{
    SUBCASE("record type above 0x08 cannot be resynchronised")
    {
        IMD b;
        b.header().thdr(0, 0, 0, 2, 0).map({1, 2});
        b.full(0x01, 128, 0xAA);
        b.raw(0x09).pad(128, 0xBB); // bogus type, plus bytes so the tail is not padding
        Mounted m(b);
        CHECK(m.st == IMDStatus::BadRecordType);
    }

    SUBCASE("size code 7 is rejected")
    {
        IMD b;
        b.header().thdr(0, 0, 0, 2, 7).map({1, 2}).full(0x01, 128, 0xAA);
        Mounted m(b);
        CHECK(m.st == IMDStatus::BadSizeCode);
    }

    SUBCASE("a record cut short is truncated, not padding")
    {
        IMD b;
        b.header().thdr(0, 0, 0, 2, 0).map({1, 2});
        b.full(0x01, 128, 0xAA);
        b.raw(0x01).pad(40, 0xBB); // needs 128 payload bytes, only 40 present
        Mounted m(b);
        CHECK(m.st == IMDStatus::Truncated);
    }

    SUBCASE("a zero-sector track header is not valid")
    {
        IMD b;
        b.header().simple_track(0, 0, 0, 1, 0xE5);
        b.thdr(0, 1, 0, 0, 0).raw(0x77); // nsec 0, then a non-uniform tail
        Mounted m(b);
        CHECK(m.st == IMDStatus::NotIMD);
    }
}

TEST_CASE("trailing transport padding is tolerated")
{
    SUBCASE("XMODEM zero padding")
    {
        IMD b;
        b.header().simple_track(0, 0, 0, 26, 0xE5);
        b.pad(100, 0x00);
        Mounted m(b);
        REQUIRE(m.st == IMDStatus::Ok);
        CHECK(m.img.lbaCount() == 26);
        CHECK(m.img.trailingGarbage() == 100);
    }

    SUBCASE("CP/M 0x1A padding")
    {
        IMD b;
        b.header().simple_track(0, 0, 0, 8, 0xE5);
        b.pad(37, 0x1A);
        Mounted m(b);
        REQUIRE(m.st == IMDStatus::Ok);
        CHECK(m.img.lbaCount() == 8);
        CHECK(m.img.trailingGarbage() == 37);
    }

    SUBCASE("a clean image reports no trailing bytes")
    {
        Mounted m(IMD().header().simple_track(0, 0, 0, 4, 0xE5));
        REQUIRE(m.st == IMDStatus::Ok);
        CHECK(m.img.trailingGarbage() == 0);
    }
}

TEST_CASE("cylinder and head maps")
{
    SUBCASE("absent maps default to the track's physical cyl/head")
    {
        Mounted m(IMD().header().simple_track(5, 3, 1, 2, 0xE5));
        REQUIRE(m.st == IMDStatus::Ok);
        IMDSectorInfo si;
        REQUIRE(m.img.sectorInfo(0, si) == IMDStatus::Ok);
        CHECK(si.cyl == 3);
        CHECK(si.head == 1);

        IMDTrackInfo ti;
        REQUIRE(m.img.trackInfo(0, ti) == IMDStatus::Ok);
        CHECK_FALSE(ti.has_cyl_map);
        CHECK_FALSE(ti.has_head_map);
    }

    SUBCASE("cylinder map alone")
    {
        IMD b;
        b.header().thdr(5, 0, IMD_HEAD_CYL_MAP, 2, 0).map({1, 2}).map({77, 77});
        b.comp(0x02, 0xE5).comp(0x02, 0xE5);
        Mounted m(b);
        REQUIRE(m.st == IMDStatus::Ok);
        IMDSectorInfo si;
        REQUIRE(m.img.sectorInfo(0, si) == IMDStatus::Ok);
        CHECK(si.cyl == 77);
        CHECK(si.head == 0);
    }

    SUBCASE("head map alone")
    {
        IMD b;
        b.header().thdr(5, 2, IMD_HEAD_HEAD_MAP, 2, 0).map({1, 2}).map({1, 1});
        b.comp(0x02, 0xE5).comp(0x02, 0xE5);
        Mounted m(b);
        REQUIRE(m.st == IMDStatus::Ok);
        IMDSectorInfo si;
        REQUIRE(m.img.sectorInfo(0, si) == IMDStatus::Ok);
        CHECK(si.cyl == 2);
        CHECK(si.head == 1);
    }

    SUBCASE("both maps, cylinder first per spec")
    {
        IMD b;
        b.header()
            .thdr(5, 0, IMD_HEAD_CYL_MAP | IMD_HEAD_HEAD_MAP, 2, 2)
            .map({1, 2})
            .map({77, 77})  // cylinder map
            .map({1, 1});   // head map
        b.comp(0x02, 0xE5).unavailable();

        Mounted m(b);
        REQUIRE(m.st == IMDStatus::Ok);
        REQUIRE(m.img.lbaCount() == 2);

        IMDSectorInfo si;
        REQUIRE(m.img.sectorInfo(0, si) == IMDStatus::Ok);
        CHECK(si.cyl == 77);
        CHECK(si.head == 1);
        CHECK(si.size == 512);
        REQUIRE(m.img.sectorInfo(1, si) == IMDStatus::Ok);
        CHECK(si.unavailable);

        IMDTrackInfo ti;
        REQUIRE(m.img.trackInfo(0, ti) == IMDStatus::Ok);
        CHECK(ti.cyl == 0); // physical stays 0
        CHECK(ti.head == 0);
        CHECK(ti.has_cyl_map);
        CHECK(ti.has_head_map);
    }

    SUBCASE("head is masked to the low six bits")
    {
        // 0x41 = head map flag + head 1
        IMD b;
        b.header().thdr(5, 0, IMD_HEAD_HEAD_MAP | 0x01, 1, 0).map({1}).map({0});
        b.comp(0x02, 0xE5);
        Mounted m(b);
        REQUIRE(m.st == IMDStatus::Ok);
        IMDTrackInfo ti;
        REQUIRE(m.img.trackInfo(0, ti) == IMDStatus::Ok);
        CHECK(ti.head == 1);
    }
}

TEST_CASE("sector sizes")
{
    SUBCASE("size codes 0..6 map to 128<<code")
    {
        for (uint8_t code = 0; code <= 6; code++)
        {
            IMD b;
            b.header().thdr(5, 0, 0, 1, code).map({1}).comp(0x02, 0xE5);
            Mounted m(b);
            REQUIRE(m.st == IMDStatus::Ok);
            CHECK(m.img.sectorSize(0) == (uint16_t)(128 << code));
        }
    }

    SUBCASE("0xFF selects a per-sector little-endian size table")
    {
        IMD b;
        b.header().thdr(5, 0, 0, 2, IMD_SIZE_CODE_VARIABLE).map({1, 2});
        b.sizes({128, 256});
        b.full(0x01, 128, 0xAA);
        b.comp(0x02, 0xBB);

        Mounted m(b);
        REQUIRE(m.st == IMDStatus::Ok);
        CHECK(m.img.sectorSize(0) == 128);
        CHECK(m.img.sectorSize(1) == 256);
        CHECK(m.img.maxSectorSize() == 256);
        CHECK_FALSE(m.img.uniformSectorSize());
        CHECK(m.img.linearSize() == 384);

        IMDStatus st;
        CHECK(all_equal(read_lba(m.img, 1, st), 0xBB));
        CHECK(st == IMDStatus::Ok);
    }

    SUBCASE("a uniform image reports uniform")
    {
        Mounted m(IMD().header().simple_track(0, 0, 0, 26, 0xE5));
        REQUIRE(m.st == IMDStatus::Ok);
        CHECK(m.img.uniformSectorSize());
        CHECK(m.img.maxSectorSize() == 128);
    }

    SUBCASE("a buffer smaller than the sector is refused")
    {
        Mounted m(IMD().header().simple_track(0, 0, 0, 1, 0xE5));
        REQUIRE(m.st == IMDStatus::Ok);
        uint8_t small[64];
        CHECK(m.img.readSector(0, small, sizeof(small), nullptr) == IMDStatus::BufferTooSmall);
    }

    SUBCASE("out_len may be null")
    {
        Mounted m(IMD().header().simple_track(0, 0, 0, 1, 0xE5));
        REQUIRE(m.st == IMDStatus::Ok);
        uint8_t buf[128];
        CHECK(m.img.readSector(0, buf, sizeof(buf), nullptr) == IMDStatus::Ok);
    }
}

TEST_CASE("sector ordering")
{
    SUBCASE("interleaved ids are deinterleaved into LBA order")
    {
        IMD b;
        b.header().thdr(5, 0, 0, 5, 0).map({1, 3, 5, 2, 4});
        // fill encodes the id so the mapping is checkable
        b.comp(0x02, 0x10).comp(0x02, 0x30).comp(0x02, 0x50);
        b.comp(0x02, 0x20).comp(0x02, 0x40);

        Mounted m(b);
        REQUIRE(m.st == IMDStatus::Ok);
        REQUIRE(m.img.lbaCount() == 5);

        for (uint32_t i = 0; i < 5; i++)
        {
            IMDSectorInfo si;
            REQUIRE(m.img.sectorInfo(i, si) == IMDStatus::Ok);
            CHECK(si.id == i + 1);
            IMDStatus st;
            CHECK(all_equal(read_lba(m.img, i, st), (uint8_t)((i + 1) * 0x10)));
        }
    }

    SUBCASE("zero-based ids work without special casing")
    {
        IMD b;
        b.header().thdr(5, 0, 0, 2, 0).map({0, 1});
        b.comp(0x02, 0xA0).comp(0x02, 0xA1);
        Mounted m(b);
        REQUIRE(m.st == IMDStatus::Ok);
        IMDSectorInfo si;
        REQUIRE(m.img.sectorInfo(0, si) == IMDStatus::Ok);
        CHECK(si.id == 0);
        REQUIRE(m.img.sectorInfo(1, si) == IMDStatus::Ok);
        CHECK(si.id == 1);
    }

    SUBCASE("duplicate ids keep their on-disk order")
    {
        IMD b;
        b.header().thdr(5, 0, 0, 3, 0).map({1, 1, 2});
        b.comp(0x02, 0xAA).comp(0x02, 0xBB).comp(0x02, 0xCC);
        Mounted m(b);
        REQUIRE(m.st == IMDStatus::Ok);
        IMDStatus st;
        CHECK(all_equal(read_lba(m.img, 0, st), 0xAA));
        CHECK(all_equal(read_lba(m.img, 1, st), 0xBB));
        CHECK(all_equal(read_lba(m.img, 2, st), 0xCC));
    }

    SUBCASE("find_lba locates by physical track and sector id")
    {
        IMD b;
        b.header().simple_track(0, 0, 0, 4, 0x11).simple_track(0, 1, 0, 4, 0x22);
        Mounted m(b);
        REQUIRE(m.st == IMDStatus::Ok);

        uint32_t lba = 0;
        REQUIRE(m.img.findLBA(1, 0, 3, lba) == IMDStatus::Ok);
        CHECK(lba == 6);
        IMDStatus st;
        CHECK(all_equal(read_lba(m.img, lba, st), 0x22));
        CHECK(m.img.findLBA(9, 0, 1, lba) != IMDStatus::Ok);
    }
}

TEST_CASE("mixed recording modes in one image")
{
    IMD b;
    b.header().simple_track(0, 0, 0, 26, 0xE5);  // FM track 0
    b.simple_track(5, 1, 0, 26, 0xE5);           // MFM elsewhere

    Mounted m(b);
    REQUIRE(m.st == IMDStatus::Ok);
    REQUIRE(m.img.trackCount() == 2);

    IMDTrackInfo t0, t1;
    REQUIRE(m.img.trackInfo(0, t0) == IMDStatus::Ok);
    REQUIRE(m.img.trackInfo(1, t1) == IMDStatus::Ok);

    CHECK(t0.mode == 0);
    CHECK_FALSE(IMDImage::modeIsMFM(t0.mode));
    CHECK(IMDImage::modeRateKBPS(t0.mode) == 500);

    CHECK(t1.mode == 5);
    CHECK(IMDImage::modeIsMFM(t1.mode));
    CHECK(IMDImage::modeRateKBPS(t1.mode) == 250);
}

TEST_CASE("mode helpers")
{
    CHECK(IMDImage::modeRateKBPS(1) == 300);
    CHECK(IMDImage::modeRateKBPS(4) == 300);
    CHECK(IMDImage::modeIsMFM(3));
    CHECK_FALSE(IMDImage::modeIsMFM(2));
    CHECK(IMDImage::modeIsValid(5));
    CHECK_FALSE(IMDImage::modeIsValid(7));
    // libdsk 1Mbps extension: read-tolerated
    CHECK(IMDImage::modeIsValid(6));
    CHECK(IMDImage::modeIsValid(9));
    CHECK(IMDImage::modeIsMFM(9));
    CHECK_FALSE(IMDImage::modeIsMFM(6));
}

TEST_CASE("in-place writes")
{
    SUBCASE("a read-only mount refuses writes")
    {
        Mounted m(IMD().header().simple_track(0, 0, 0, 1, 0xE5), false);
        REQUIRE(m.st == IMDStatus::Ok);
        std::vector<uint8_t> d(128, 0x5A);
        CHECK(m.img.writeSector(0, d.data(), 128) == IMDStatus::ReadOnly);
    }

    SUBCASE("a full record takes any data and clears the error flag")
    {
        IMD b;
        b.header().thdr(5, 0, 0, 1, 0).map({1}).full(0x07, 128, 0x00); // deleted + error
        Mounted m(b, true);
        REQUIRE(m.st == IMDStatus::Ok);

        std::vector<uint8_t> d(128);
        for (size_t i = 0; i < d.size(); i++)
            d[i] = (uint8_t)i;
        REQUIRE(m.img.writeSector(0, d.data(), 128) == IMDStatus::Ok);

        IMDSectorInfo si;
        REQUIRE(m.img.sectorInfo(0, si) == IMDStatus::Ok);
        CHECK(si.rec_type == 0x03); // error cleared, deleted mark kept
        CHECK(si.deleted);
        CHECK_FALSE(si.had_error);

        IMDStatus st;
        CHECK(read_lba(m.img, 0, st) == d);
        CHECK(st == IMDStatus::Ok);
    }

    SUBCASE("a compressed record takes a uniform buffer")
    {
        IMD b;
        b.header().thdr(5, 0, 0, 1, 0).map({1}).comp(0x08, 0xE5); // compressed+deleted+error
        Mounted m(b, true);
        REQUIRE(m.st == IMDStatus::Ok);

        std::vector<uint8_t> d(128, 0x5A);
        REQUIRE(m.img.writeSector(0, d.data(), 128) == IMDStatus::Ok);

        IMDSectorInfo si;
        REQUIRE(m.img.sectorInfo(0, si) == IMDStatus::Ok);
        CHECK(si.rec_type == 0x04); // still compressed, still deleted, no error
        CHECK(si.compressed);
        CHECK(si.deleted);
        CHECK_FALSE(si.had_error);

        IMDStatus st;
        CHECK(all_equal(read_lba(m.img, 0, st), 0x5A));

        // The file must not have grown: the record is still one fill byte
        CHECK(m.file_size() == (long)b.out.size());
    }

    SUBCASE("a non-uniform buffer expands every compressed record")
    {
        IMD image;
        image.header();
        image.thdr(5, 0, 0, 2, 0).map({2, 1});
        image.full(0x01, 128, 0xAA);  // id 2
        image.comp(0x04, 0xBB);       // id 1: compressed, deleted
        image.thdr(5, 1, 0, 3, 0).map({1, 2, 3});
        image.comp(0x02, 0xCC);
        image.unavailable();
        image.comp(0x06, 0xDD);       // compressed, data error
        Mounted mounted(image, true);
        REQUIRE(mounted.st == IMDStatus::Ok);

        std::vector<uint8_t> data(128, 0x5A);
        data[64] = 0x01;
        REQUIRE(mounted.img.writeSector(4, data.data(), 128) == IMDStatus::Ok);

        // Three one-byte fills became 128-byte sectors
        CHECK(mounted.file_size() == (long)image.out.size() + 3 * 127);

        auto check = [&](IMDImage &img) {
            const uint8_t types[] = {0x03, 0x01, 0x01, 0x00, 0x01};
            for (uint32_t lba = 0; lba < 5; lba++)
            {
                IMDSectorInfo info;
                REQUIRE(img.sectorInfo(lba, info) == IMDStatus::Ok);
                CHECK(info.rec_type == types[lba]);
            }
            IMDStatus status;
            CHECK(all_equal(read_lba(img, 0, status), 0xBB));
            CHECK(all_equal(read_lba(img, 1, status), 0xAA));
            CHECK(all_equal(read_lba(img, 2, status), 0xCC));
            read_lba(img, 3, status);
            CHECK(status == IMDStatus::Unavailable);
            CHECK(read_lba(img, 4, status) == data);
        };
        check(mounted.img);

        // The file on disk parses to the same image
        std::fstream           again_file(IMD_TEST_FILE, std::ios::in | std::ios::binary);
        IMDImage again(&again_file);
        REQUIRE(again.open(false) == IMDStatus::Ok);
        CHECK(again.lbaCount() == 5);
        CHECK(again.trailingGarbage() == 0);
        check(again);
    }

    SUBCASE("expansion carries trailing padding along")
    {
        IMD image;
        image.header().thdr(5, 0, 0, 2, 0).map({1, 2});
        image.comp(0x02, 0xAA).comp(0x02, 0xBB);
        image.pad(100, 0x1A);
        Mounted mounted(image, true);
        REQUIRE(mounted.st == IMDStatus::Ok);
        REQUIRE(mounted.img.trailingGarbage() == 100);

        std::vector<uint8_t> data(128, 0x5A);
        data[0] = 0x01;
        REQUIRE(mounted.img.writeSector(0, data.data(), 128) == IMDStatus::Ok);
        CHECK(mounted.file_size() == (long)image.out.size() + 2 * 127);

        std::fstream           again_file(IMD_TEST_FILE, std::ios::in | std::ios::binary);
        IMDImage again(&again_file);
        REQUIRE(again.open(false) == IMDStatus::Ok);
        CHECK(again.trailingGarbage() == 100);
        IMDStatus status;
        CHECK(read_lba(again, 0, status) == data);
        CHECK(all_equal(read_lba(again, 1, status), 0xBB));
    }

    SUBCASE("an unavailable record cannot absorb a write")
    {
        IMD b;
        b.header().thdr(5, 0, 0, 1, 0).map({1}).unavailable();
        Mounted m(b, true);
        REQUIRE(m.st == IMDStatus::Ok);
        std::vector<uint8_t> d(128, 0x5A);
        CHECK(m.img.writeSector(0, d.data(), 128) == IMDStatus::WriteRefused);
    }

    SUBCASE("the write length must match the sector exactly")
    {
        Mounted m(IMD().header().simple_track(0, 0, 0, 1, 0xE5), true);
        REQUIRE(m.st == IMDStatus::Ok);
        std::vector<uint8_t> d(64, 0x5A);
        CHECK(m.img.writeSector(0, d.data(), 64) == IMDStatus::WriteRefused);
    }

    SUBCASE("writing past the end is refused")
    {
        Mounted m(IMD().header().simple_track(0, 0, 0, 1, 0xE5), true);
        REQUIRE(m.st == IMDStatus::Ok);
        std::vector<uint8_t> d(128, 0x5A);
        CHECK(m.img.writeSector(99, d.data(), 128) == IMDStatus::NoSuchSector);
    }
}

TEST_CASE("linear byte view")
{
    SUBCASE("reads span a sector boundary")
    {
        IMD b;
        b.header().thdr(5, 0, 0, 2, 0).map({1, 2});
        b.comp(0x02, 0xAA).comp(0x02, 0xBB);

        Mounted m(b);
        REQUIRE(m.st == IMDStatus::Ok);
        CHECK(m.img.linearSize() == 256);

        uint8_t buf[64];
        REQUIRE(m.img.readLinear(96, buf, sizeof(buf)) == IMDStatus::Ok);
        for (int i = 0; i < 32; i++)
            CHECK(buf[i] == 0xAA);
        for (int i = 32; i < 64; i++)
            CHECK(buf[i] == 0xBB);
    }

    SUBCASE("reads past the end are refused")
    {
        Mounted m(IMD().header().simple_track(0, 0, 0, 2, 0xE5));
        REQUIRE(m.st == IMDStatus::Ok);
        uint8_t buf[64];
        CHECK(m.img.readLinear(250, buf, sizeof(buf)) == IMDStatus::NoSuchSector);
    }

    SUBCASE("writes span a sector boundary")
    {
        IMD b;
        b.header().thdr(5, 0, 0, 2, 0).map({1, 2});
        b.full(0x01, 128, 0xAA).full(0x01, 128, 0xBB);

        Mounted m(b, true);
        REQUIRE(m.st == IMDStatus::Ok);

        std::vector<uint8_t> d(64, 0x5A);
        REQUIRE(m.img.writeLinear(96, d.data(), (uint32_t)d.size()) == IMDStatus::Ok);

        std::vector<uint8_t> back(256);
        REQUIRE(m.img.readLinear(0, back.data(), 256) == IMDStatus::Ok);
        for (int i = 0; i < 96; i++)
            CHECK(back[i] == 0xAA);
        for (int i = 96; i < 160; i++)
            CHECK(back[i] == 0x5A);
        for (int i = 160; i < 256; i++)
            CHECK(back[i] == 0xBB);
    }

    SUBCASE("a refusal anywhere in the span leaves every sector untouched")
    {
        IMD b;
        b.header().thdr(5, 0, 0, 2, 0).map({1, 2});
        b.full(0x01, 128, 0xAA);  // would accept anything
        b.unavailable();          // can take nothing

        Mounted m(b, true);
        REQUIRE(m.st == IMDStatus::Ok);

        // Overwrites the tail of sector 0 and the head of sector 1
        std::vector<uint8_t> d(64, 0x5A);
        CHECK(m.img.writeLinear(96, d.data(), (uint32_t)d.size()) == IMDStatus::WriteRefused);

        std::vector<uint8_t> back(128);
        REQUIRE(m.img.readLinear(0, back.data(), 128) == IMDStatus::Ok);
        for (int idx = 0; idx < 128; idx++)
            CHECK(back[idx] == 0xAA);
    }

    SUBCASE("a span into a compressed sector expands it")
    {
        IMD image;
        image.header().thdr(5, 0, 0, 2, 0).map({1, 2});
        image.full(0x01, 128, 0xAA);
        image.comp(0x02, 0xBB);

        Mounted mounted(image, true);
        REQUIRE(mounted.st == IMDStatus::Ok);

        std::vector<uint8_t> data(64, 0x5A);
        REQUIRE(mounted.img.writeLinear(96, data.data(), (uint32_t)data.size()) == IMDStatus::Ok);
        CHECK(mounted.file_size() == (long)image.out.size() + 127);

        std::vector<uint8_t> back(256);
        REQUIRE(mounted.img.readLinear(0, back.data(), 256) == IMDStatus::Ok);
        for (int idx = 0; idx < 96; idx++)
            CHECK(back[idx] == 0xAA);
        for (int idx = 96; idx < 160; idx++)
            CHECK(back[idx] == 0x5A);
        for (int idx = 160; idx < 256; idx++)
            CHECK(back[idx] == 0xBB);
    }

    SUBCASE("a uniform overwrite of a compressed sector is allowed")
    {
        IMD b;
        b.header().thdr(5, 0, 0, 1, 0).map({1}).comp(0x02, 0xBB);
        Mounted m(b, true);
        REQUIRE(m.st == IMDStatus::Ok);

        std::vector<uint8_t> d(128, 0xBB);
        CHECK(m.img.writeLinear(0, d.data(), 128) == IMDStatus::Ok);
    }

    SUBCASE("read-only mounts refuse linear writes")
    {
        Mounted m(IMD().header().simple_track(0, 0, 0, 2, 0xE5), false);
        REQUIRE(m.st == IMDStatus::Ok);
        uint8_t d[16] = {0};
        CHECK(m.img.writeLinear(0, d, sizeof(d)) == IMDStatus::ReadOnly);
    }
}

TEST_CASE("8 inch SSSD CP/M image - the most common IMD in existence")
{
    // 77 cylinders, 1 head, 26 sectors of 128 bytes, 500 kbps FM
    IMD b;
    b.header();
    for (uint8_t c = 0; c < 77; c++)
        b.simple_track(0, c, 0, 26, 0xE5);

    Mounted m(b);
    REQUIRE(m.st == IMDStatus::Ok);
    CHECK(m.img.trackCount() == 77);
    CHECK(m.img.lbaCount() == 2002);
    CHECK(m.img.linearSize() == 2002 * 128); // 256,256 bytes
    CHECK(m.img.uniformSectorSize());
    CHECK(m.img.trailingGarbage() == 0);

    IMDTrackInfo ti;
    REQUIRE(m.img.trackInfo(76, ti) == IMDStatus::Ok);
    CHECK(ti.cyl == 76);
    CHECK(ti.first_lba == 76 * 26);

    IMDStatus st;
    CHECK(all_equal(read_lba(m.img, 2001, st), 0xE5));
    CHECK(st == IMDStatus::Ok);
}

TEST_CASE("sector count is capped")
{
    // 258 tracks of 255 sectors overruns the uint16_t sector addressing that
    // the platform MediaType classes use.
    IMD b;
    b.header();
    for (int t = 0; t < 258; t++)
    {
        b.thdr(5, (uint8_t)(t & 0xFF), 0, 255, 0);
        std::vector<uint8_t> ids;
        for (int i = 0; i < 255; i++)
            ids.push_back((uint8_t)i);
        b.map(ids);
        for (int i = 0; i < 255; i++)
            b.comp(0x02, 0xE5);
    }

    Mounted m(b);
    CHECK(m.st == IMDStatus::TooLarge);
    CHECK_FALSE(m.img.isOpen());
}

TEST_CASE("extension sniffing")
{
    CHECK(IMDImage::looksLikeExtension("disk.imd"));
    CHECK(IMDImage::looksLikeExtension("DISK.IMD"));
    CHECK(IMDImage::looksLikeExtension("/path/to/cpm.IMD"));
    CHECK_FALSE(IMDImage::looksLikeExtension("disk.img"));
    CHECK_FALSE(IMDImage::looksLikeExtension("imd"));
    CHECK_FALSE(IMDImage::looksLikeExtension(".imd"));
    CHECK_FALSE(IMDImage::looksLikeExtension(nullptr));
}

TEST_CASE("close resets the image")
{
    Mounted m(IMD().header().simple_track(0, 0, 0, 26, 0xE5));
    REQUIRE(m.st == IMDStatus::Ok);
    REQUIRE(m.img.lbaCount() == 26);

    m.img.close();
    CHECK_FALSE(m.img.isOpen());
    CHECK(m.img.lbaCount() == 0);
    CHECK(m.img.trackCount() == 0);
    CHECK(m.img.linearSize() == 0);

    uint8_t buf[128];
    CHECK(m.img.readSector(0, buf, sizeof(buf), nullptr) == IMDStatus::IoError);
}

TEST_CASE("status strings are available for every code")
{
    CHECK(std::string(IMDImage::statusString(IMDStatus::Ok)) == "Ok");
    CHECK(std::string(IMDImage::statusString(IMDStatus::WriteRefused)) == "WriteRefused");
    CHECK(std::string(IMDImage::statusString(IMDStatus::BadRecordType)) == "BadRecordType");
}

namespace
{

// Neither a stream nor a FILE: anything with fnFile's methods is accepted
struct MemFile
{
    std::vector<uint8_t> bytes;
    long                 pos = 0;

    int seek(long off, int whence)
    {
        long base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? pos : (long)bytes.size();
        if (base + off < 0)
            return -1;
        pos = base + off;
        return 0;
    }
    long tell() { return pos; }
    size_t read(void *dst, size_t size, size_t count)
    {
        size_t want = size * count;
        size_t left = pos < (long)bytes.size() ? bytes.size() - (size_t)pos : 0;
        size_t have = std::min(want, left);
        memcpy(dst, bytes.data() + pos, have);
        pos += (long)have;
        return have / size;
    }
    size_t write(const void *src, size_t size, size_t count)
    {
        size_t len = size * count;
        if (bytes.size() < (size_t)pos + len)
            bytes.resize((size_t)pos + len);
        memcpy(bytes.data() + pos, src, len);
        pos += (long)len;
        return count;
    }
    int flush() { return 0; }
};

// One compressed and one full sector; a non-uniform write grows the compressed one
IMD two_sectors()
{
    IMD image;
    image.header().thdr(5, 0, 0, 2, 0).map({1, 2});
    image.comp(0x02, 0xAA).full(0x01, 128, 0xBB);
    return image;
}

template <typename File> void read_and_grow(File *file, long (*size_of)(File *))
{
    IMDImage img(file);
    REQUIRE(img.open(true) == IMDStatus::Ok);
    REQUIRE(img.lbaCount() == 2);

    std::vector<uint8_t> buf(128);
    uint16_t             got = 0;
    REQUIRE(img.readSector(0, buf.data(), (uint32_t)buf.size(), &got) == IMDStatus::Ok);
    CHECK(all_equal(buf, 0xAA));

    std::vector<uint8_t> data(128, 0x5A);
    data[7] = 0x01;
    REQUIRE(img.writeSector(0, data.data(), 128) == IMDStatus::Ok);
    CHECK(size_of(file) == (long)two_sectors().out.size() + 127);

    IMDImage again(file);
    REQUIRE(again.open(false) == IMDStatus::Ok);
    REQUIRE(again.readSector(0, buf.data(), (uint32_t)buf.size(), &got) == IMDStatus::Ok);
    CHECK(buf == data);
    REQUIRE(again.readSector(1, buf.data(), (uint32_t)buf.size(), &got) == IMDStatus::Ok);
    CHECK(all_equal(buf, 0xBB));
}

} // namespace

TEST_CASE("a std::FILE handle")
{
    IMD   image = two_sectors();
    FILE *file = fopen(IMD_TEST_FILE, "w+b");
    REQUIRE(file != nullptr);
    REQUIRE(fwrite(image.out.data(), 1, image.out.size(), file) == image.out.size());

    read_and_grow<std::FILE>(file, [](std::FILE *handle) {
        fflush(handle);
        fseek(handle, 0, SEEK_END);
        return ftell(handle);
    });

    fclose(file);
    remove(IMD_TEST_FILE);
}

TEST_CASE("a handle with fnFile's methods")
{
    MemFile file;
    file.bytes = two_sectors().out;
    read_and_grow<MemFile>(&file, [](MemFile *handle) { return (long)handle->bytes.size(); });
}

TEST_CASE("building an image")
{
    MemFile file;
    std::vector<uint8_t> small(128, 0x11), mid(256, 0x22), large(512, 0x33);

    IMDImage img(&file);
    REQUIRE(img.writeHeader(0, "built by a test\r\n") == IMDStatus::Ok);
    // An FM track of 128-byte sectors, one never readable, then a 256/512 MFM track
    REQUIRE(img.writeTrack(0, 0, 0, {{1, 128, small.data()}, {2, 128, nullptr}})
            == IMDStatus::Ok);
    REQUIRE(img.writeTrack(3, 1, 0, {{1, 256, mid.data()}, {2, 512, large.data()}})
            == IMDStatus::Ok);
    REQUIRE(img.writeBlankTrack(3, 2, 0, 3, 2, 0xE5) == IMDStatus::Ok);

    // A second header would land after the tracks
    CHECK(img.writeHeader(0) == IMDStatus::WriteRefused);

    REQUIRE(img.open(true) == IMDStatus::Ok);
    CHECK(std::string(img.comment()) == "built by a test\r\n");
    REQUIRE(img.trackCount() == 3);
    REQUIRE(img.lbaCount() == 7);
    CHECK_FALSE(img.uniformSectorSize());

    IMDTrackInfo track;
    REQUIRE(img.trackInfo(0, track) == IMDStatus::Ok);
    CHECK(track.size_code == 0);
    REQUIRE(img.trackInfo(1, track) == IMDStatus::Ok);
    CHECK(track.size_code == IMD_SIZE_CODE_VARIABLE);
    CHECK(IMDImage::modeIsMFM(track.mode));

    IMDSectorInfo info;
    REQUIRE(img.sectorInfo(1, info) == IMDStatus::Ok);
    CHECK(info.unavailable);

    IMDStatus status;
    CHECK(read_lba(img, 0, status) == small);
    CHECK(read_lba(img, 2, status) == mid);
    CHECK(read_lba(img, 3, status) == large);
    for (uint32_t lba = 4; lba < 7; lba++)
    {
        CHECK(img.sectorSize(lba) == 512);
        CHECK(all_equal(read_lba(img, lba, status), 0xE5));
    }

    // Blank sectors are full records, so any data goes in without growing the file
    size_t before = file.bytes.size();
    std::vector<uint8_t> data(512, 0x44);
    data[0] = 0x01;
    REQUIRE(img.writeSector(5, data.data(), 512) == IMDStatus::Ok);
    CHECK(file.bytes.size() == before);
    CHECK(read_lba(img, 5, status) == data);
}
