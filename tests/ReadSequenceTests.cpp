#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "device/NDevice/ReadSequence.h"

#include <string>

// NET_SET_SEQUENCE on every bus whose READ carries a length: a retried READ
// repeats the whole length word, bit 15 included.

static ByteBuffer bytes(const std::string &s)
{
    return ByteBuffer(s.begin(), s.end());
}

TEST_CASE("off, bit 15 is part of the length and nothing is a retry")
{
    ReadSequence seq;
    CHECK(seq.length(0x8002) == 0x8002);
    seq.remember(0x0002, bytes("AB"), false);
    CHECK_FALSE(seq.is_retry(0x0002));
}

TEST_CASE("on, only the same bit and length is a retry")
{
    ReadSequence seq;
    seq.set(true);
    CHECK(seq.length(0x8002) == 2);
    CHECK_FALSE(seq.is_retry(0x0002)); // nothing read yet

    seq.remember(0x0002, bytes("AB"), false);
    CHECK(seq.is_retry(0x0002));
    CHECK_FALSE(seq.is_retry(0x8002)); // next READ
    CHECK_FALSE(seq.is_retry(0x0003)); // same bit, new length: a new READ
}

TEST_CASE("a retry gets the last chunk and its result again")
{
    ReadSequence seq;
    seq.set(true);
    ByteBuffer chunk;

    SUBCASE("a chunk shorter than asked for")
    {
        seq.remember(0x0010, bytes("ABC"), false);
        CHECK(seq.replay(chunk).is_success());
        CHECK(chunk == bytes("ABC"));
    }

    SUBCASE("a READ that failed")
    {
        seq.remember(0x0004, ByteBuffer(), true);
        chunk = bytes("junk");
        CHECK(seq.replay(chunk).is_error());
        CHECK(chunk.empty());
    }
}

TEST_CASE("turning the mode off or on again forgets the last READ")
{
    ReadSequence seq;
    seq.set(true);
    seq.remember(0x0002, bytes("AB"), false);

    seq.set(false); // what OPEN and CLOSE do
    CHECK(seq.length(0x8002) == 0x8002);
    CHECK_FALSE(seq.is_retry(0x0002));

    seq.set(true);
    CHECK_FALSE(seq.is_retry(0x0002));
}
