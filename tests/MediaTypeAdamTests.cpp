#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdio>
#include <cstring>

#include "mediaTypeDDP.h"
#include "mediaTypeDSK.h"
#include "mediaTypeROM.h"

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
