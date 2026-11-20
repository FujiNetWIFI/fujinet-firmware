#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdio>
#include <cstring>

#include "mediaTypeDDP.h"
#include "mediaTypeDSK.h"
#include "mediaTypeROM.h"

// FileSystem links DirCache, which filters with this; these tests never list a directory.
bool util_wildcard_match(const char *, const char *) { return true; }

namespace
{

// The build forces FNIO_IS_STDIO for this target, so fnFile is std::FILE.
#define ADAM_TEST_FILE "mediatypeadam_test.tmp"

// A fresh temp file of `size` bytes
fnFile *image(uint32_t size)
{
    fnFile *fp = fopen(ADAM_TEST_FILE, "w+b");
    REQUIRE(fp != nullptr);
    uint8_t blk[1024] = {};
    for (uint32_t done = 0; done < size; done += sizeof(blk))
    {
        uint32_t n = size - done < sizeof(blk) ? size - done : sizeof(blk);
        REQUIRE(fwrite(blk, 1, n, fp) == n);
    }
    rewind(fp);
    return fp;
}

template <class M> void reads_stop_at_the_end(uint32_t size, uint32_t blocks)
{
    M media;
    REQUIRE(media.mount(image(size), size) != MEDIATYPE_UNKNOWN);
    REQUIRE(media.num_blocks() == blocks);
    uint16_t n = 0;
    CHECK(media.read(blocks - 1, &n).is_success());
    CHECK(media.read(blocks, &n).is_error());
    CHECK(media.read(UINT32_MAX, &n).is_error());
    media.unmount();
    remove(ADAM_TEST_FILE);
}

template <class M> void writes_stop_at_the_end(uint32_t size, uint32_t blocks)
{
    M media;
    REQUIRE(media.mount(image(size), size) != MEDIATYPE_UNKNOWN);
    CHECK(media.write(blocks - 1, false).is_success());
    CHECK(media.write(blocks, false).is_error());
    media.unmount();
    FILE *fp = fopen(ADAM_TEST_FILE, "rb");
    REQUIRE(fp != nullptr);
    fseek(fp, 0, SEEK_END);
    CHECK(ftell(fp) == (long)size); // nothing written past the end
    fclose(fp);
    remove(ADAM_TEST_FILE);
}

} // namespace

// As a 4 GiB image did once its size wrapped to 0 in 32 bits: a real file, a size of 0
TEST_CASE("a size under one block refuses the mount, and reads still fail")
{
    uint16_t n = 0;

    MediaTypeDDP ddp;
    CHECK(ddp.mount(image(64 * 1024), 0) == MEDIATYPE_UNKNOWN);
    CHECK(ddp.read(0, &n).is_error());
    CHECK(ddp.read(5, &n).is_error());
    ddp.unmount();
    remove(ADAM_TEST_FILE);

    MediaTypeDSK dsk;
    CHECK(dsk.mount(image(64 * 1024), 500) == MEDIATYPE_UNKNOWN);
    CHECK(dsk.read(0, &n).is_error());
    CHECK(dsk.read(5, &n).is_error());
    dsk.unmount();
    remove(ADAM_TEST_FILE);
}

TEST_CASE("reads stop at the last block")
{
    reads_stop_at_the_end<MediaTypeDDP>(16 * 1024, 16);
    reads_stop_at_the_end<MediaTypeDSK>(16 * 1024, 16);
    reads_stop_at_the_end<MediaTypeROM>(8 * 1024, 8 + 2); // two boot blocks ahead of the ROM
}

TEST_CASE("writes stop at the last block")
{
    writes_stop_at_the_end<MediaTypeDDP>(16 * 1024, 16);
    writes_stop_at_the_end<MediaTypeDSK>(16 * 1024, 16);
}

namespace
{

template <class M> void create_writes_an_eos_volume(uint32_t blocks)
{
    M media;
    fnFile *fp = fopen(ADAM_TEST_FILE, "w+b");
    REQUIRE(fp != nullptr);
    REQUIRE(media.create(fp, blocks).is_success());
    fflush(fp);
    fseek(fp, 0, SEEK_END);
    REQUIRE(ftell(fp) == (long)blocks * 1024);
    rewind(fp);

    REQUIRE(media.mount(fp, blocks * 1024) != MEDIATYPE_UNKNOWN);
    uint16_t n = 0;
    REQUIRE(media.read(0, &n).is_success());
    CHECK(media._media_blockbuff[0] == 0xF3);
    CHECK(media._media_blockbuff[1] == 0xC3);

    REQUIRE(media.read(1, &n).is_success());
    const uint8_t *dir = media._media_blockbuff;
    CHECK(dir[0] == 0x03);                  // empty volume name
    CHECK(dir[12] == 0x83);                 // volume, 3 directory blocks
    CHECK(dir[13] == 0x55);
    CHECK(dir[14] == 0xAA);
    CHECK(dir[15] == 0x00);
    CHECK(dir[16] == 0xFF);
    CHECK(dir[17] == (blocks & 0xFF));
    CHECK(dir[18] == (blocks >> 8));
    CHECK(dir[26 + 12] == 0x88);            // BOOT
    CHECK(dir[52 + 12] == 0xC8);            // DIRECTORY
    CHECK(dir[78 + 12] == 0x01);            // BLOCKS LEFT
    CHECK(dir[78 + 13] == 4);               // starts after the directory
    CHECK(dir[78 + 17] == ((blocks - 4) & 0xFF));
    CHECK(dir[78 + 18] == ((blocks - 4) >> 8));

    REQUIRE(media.read(3, &n).is_success());
    CHECK(media._media_blockbuff[12] == 0); // later directory blocks are empty
    media.unmount();
    remove(ADAM_TEST_FILE);
}

} // namespace

TEST_CASE("create writes an EOS volume")
{
    create_writes_an_eos_volume<MediaTypeDDP>(256);
    create_writes_an_eos_volume<MediaTypeDSK>(160);
    create_writes_an_eos_volume<MediaTypeDDP>(720);
}

TEST_CASE("create leaves an image too big for EOS blank")
{
    MediaTypeDDP media;
    fnFile *fp = fopen(ADAM_TEST_FILE, "w+b");
    REQUIRE(fp != nullptr);
    CHECK(media.create(fp, 0x10000).is_success());
    rewind(fp);
    uint8_t buf[2] = {1, 1};
    CHECK(fread(buf, 1, 2, fp) == 2);
    CHECK(buf[0] == 0);
    fclose(fp);
    remove(ADAM_TEST_FILE);
}
