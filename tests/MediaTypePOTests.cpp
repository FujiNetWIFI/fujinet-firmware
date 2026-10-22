#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdio>
#include <cstring>
#include <vector>

#include "mediaTypePO.h"

// Only the high-score write path opens through the host; these tests never reach it.
fnFile *fujiHost::fnfile_open(const char *, char *, int, const char *) { return nullptr; }

namespace
{

// The build forces FNIO_IS_STDIO for this target, so fnFile is std::FILE.
#define PO_TEST_FILE "mediatypepo_test.tmp"

uint32_t le(const std::vector<uint8_t> &b, size_t off, size_t n)
{
    uint32_t v = 0;
    for (size_t i = 0; i < n; i++)
        v |= (uint32_t)b[off + i] << (8 * i);
    return v;
}

} // namespace

TEST_CASE("write_2mg_header lays out the 64-byte 2MG header")
{
    fnFile *fp = fopen(PO_TEST_FILE, "w+b");
    REQUIRE(fp != nullptr);
    REQUIRE(MediaTypePO::write_2mg_header(fp, 280).is_success());
    fseek(fp, 0, SEEK_END);
    std::vector<uint8_t> h(ftell(fp));
    rewind(fp);
    REQUIRE(fread(h.data(), 1, h.size(), fp) == h.size());
    fclose(fp);
    remove(PO_TEST_FILE);

    REQUIRE(h.size() == 64);
    CHECK(memcmp(&h[0x00], "2IMG", 4) == 0);
    CHECK(memcmp(&h[0x04], "FUJI", 4) == 0);
    CHECK(le(h, 0x08, 2) == 64);        // header size
    CHECK(le(h, 0x0A, 2) == 1);         // version
    CHECK(le(h, 0x0C, 4) == 1);         // ProDOS order
    CHECK(le(h, 0x10, 4) == 0);         // flags
    CHECK(le(h, 0x14, 4) == 280);       // blocks
    CHECK(le(h, 0x18, 4) == 64);        // data offset
    CHECK(le(h, 0x1C, 4) == 280 * 512); // data length
    for (size_t i = 0x20; i < 0x40; i++)
        CHECK(h[i] == 0);               // no comment or creator data, reserved
}

TEST_CASE("a 2MG image mounts with its data right after the header")
{
    const uint16_t blocks = 16;
    fnFile *fp = fopen(PO_TEST_FILE, "w+b");
    REQUIRE(fp != nullptr);
    REQUIRE(MediaTypePO::write_2mg_header(fp, blocks).is_success());
    uint8_t blk[512];
    for (uint16_t b = 0; b < blocks; b++)
    {
        memset(blk, 0xA0 + b, sizeof(blk));
        REQUIRE(fwrite(blk, 1, sizeof(blk), fp) == sizeof(blk));
    }
    uint32_t size = (uint32_t)ftell(fp);
    rewind(fp);

    MediaTypePO media;
    CHECK(media.mount(fp, size) == MEDIATYPE_PO);
    for (uint16_t b : {(uint16_t)0, (uint16_t)(blocks - 1)})
    {
        CAPTURE(b);
        uint16_t count = sizeof(blk);
        memset(blk, 0, sizeof(blk));
        REQUIRE(media.read(b, &count, blk).is_success());
        CHECK(blk[0] == (uint8_t)(0xA0 + b));
        CHECK(blk[511] == (uint8_t)(0xA0 + b));
    }
    media.unmount();
    remove(PO_TEST_FILE);
}
