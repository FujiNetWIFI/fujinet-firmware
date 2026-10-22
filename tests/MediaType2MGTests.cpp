#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdio>
#include <cstring>

#include "mediaType2MG.h"

// Only the high-score write path opens through the host; these tests never reach it.
fnFile *fujiHost::fnfile_open(const char *, char *, int, const char *) { return nullptr; }

namespace
{

// The build forces FNIO_IS_STDIO for this target, so fnFile is std::FILE.
#define TWOMG_TEST_FILE "mediatype2mg_test.tmp"

const uint16_t BLOCKS = 16;

// A 64-byte 2MG header, with an optional high-score marker at offset 48
void put_header(fnFile *fp, const uint8_t *marker = nullptr)
{
    uint8_t h[64] = {'2', 'I', 'M', 'G', 'F', 'U', 'J', 'I', 64, 0, 1, 0, 1, 0, 0, 0};
    h[0x14] = BLOCKS;
    h[0x18] = 64;
    if (marker)
        memcpy(&h[48], marker, 6);
    REQUIRE(fwrite(h, 1, sizeof(h), fp) == sizeof(h));
}

// Writes the image to a fresh temp file and returns its size
uint32_t make_image(fnFile *fp, bool header, const uint8_t *marker = nullptr)
{
    if (header)
        put_header(fp, marker);
    uint8_t blk[512];
    for (uint16_t b = 0; b < BLOCKS; b++)
    {
        memset(blk, 0xA0 + b, sizeof(blk));
        REQUIRE(fwrite(blk, 1, sizeof(blk), fp) == sizeof(blk));
    }
    uint32_t size = (uint32_t)ftell(fp);
    rewind(fp);
    return size;
}

bool block_is(MediaType &m, uint16_t b)
{
    uint8_t blk[512] = {};
    uint16_t count = sizeof(blk);
    return m.read(b, &count, blk).is_success() && blk[0] == (uint8_t)(0xA0 + b)
        && blk[511] == (uint8_t)(0xA0 + b);
}

struct HighScore : MediaType2MG
{
    uint16_t lb() const { return _high_score_block_lb; }
    uint16_t ub() const { return _high_score_block_ub; }
};

} // namespace

TEST_CASE("an image starting with a 2MG header mounts as 2MG with its data after it")
{
    fnFile *fp = fopen(TWOMG_TEST_FILE, "w+b");
    REQUIRE(fp != nullptr);
    uint32_t size = make_image(fp, true);

    CHECK(MediaType::discover_po_mediatype(fp) == MEDIATYPE_2MG);
    MediaType2MG media;
    CHECK(media.mount(fp, size) == MEDIATYPE_2MG);
    CHECK(media.num_blocks == BLOCKS);
    CHECK(block_is(media, 0));
    CHECK(block_is(media, BLOCKS - 1));
    CHECK_FALSE(media.high_score_enabled);
    media.unmount();
    remove(TWOMG_TEST_FILE);
}

TEST_CASE("a plain ProDOS-order image stays PO and mounts from offset 0")
{
    fnFile *fp = fopen(TWOMG_TEST_FILE, "w+b");
    REQUIRE(fp != nullptr);
    uint32_t size = make_image(fp, false);

    CHECK(MediaType::discover_po_mediatype(fp) == MEDIATYPE_PO);
    MediaTypePO media;
    CHECK(media.mount(fp, size) == MEDIATYPE_PO);
    CHECK(media.num_blocks == BLOCKS);
    CHECK(block_is(media, 0));
    CHECK(block_is(media, BLOCKS - 1));
    media.unmount();
    remove(TWOMG_TEST_FILE);
}

TEST_CASE("directory entries still call a .2mg name PO")
{
    CHECK(MediaType::discover_mediatype("GAME.2MG") == MEDIATYPE_PO);
    CHECK(MediaType::discover_mediatype("game.po") == MEDIATYPE_PO);
}

TEST_CASE("a 2MG high-score marker gives the writable block range")
{
    const uint8_t range[6] = {'H', 'I', 3, 0, 5, 0};
    const uint8_t single[6] = {'H', 'I', 7, 0, 0, 0};

    for (auto marker : {range, single})
    {
        fnFile *fp = fopen(TWOMG_TEST_FILE, "w+b");
        REQUIRE(fp != nullptr);
        uint32_t size = make_image(fp, true, marker);
        HighScore media;
        CHECK(media.mount(fp, size) == MEDIATYPE_2MG);
        CHECK(media.high_score_enabled);
        CHECK(media.lb() == marker[2]);
        CHECK(media.ub() == (marker[4] ? marker[4] : marker[2]));
        media.unmount();
        remove(TWOMG_TEST_FILE);
    }
}
