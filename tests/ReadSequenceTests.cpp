#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "device/NDevice/ReadSequence.h"

#include <string>

// NET_SET_SEQUENCE on every bus: a retried READ repeats its length and
// sequence number, which bit 15 of the length field carries.

static ByteBuffer bytes(const std::string &s)
{
    return ByteBuffer(s.begin(), s.end());
}

static const error_is_true READ_OK(false);
static const error_is_true READ_FAILED(true);

TEST_CASE("off, bit 15 is part of the length and nothing is a retry")
{
    ReadSequence seq;
    CHECK(seq.length(0x8002) == 0x8002);
    seq.remember(2, seq.sequence(0x0002), bytes("AB"), READ_OK);
    CHECK_FALSE(seq.is_retry(2, seq.sequence(0x0002)));
}

TEST_CASE("on, the length field splits into length and sequence number")
{
    ReadSequence seq;
    seq.set(true);
    CHECK(seq.length(0x8002) == 2);
    CHECK(seq.sequence(0x8002) == 1);
    CHECK(seq.length(0x0002) == 2);
    CHECK(seq.sequence(0x0002) == 0);
}

TEST_CASE("on, only the same sequence number and length is a retry")
{
    ReadSequence seq;
    seq.set(true);
    CHECK_FALSE(seq.is_retry(2, 0)); // nothing read yet

    seq.remember(2, 0, bytes("AB"), READ_OK);
    CHECK(seq.is_retry(2, 0));
    CHECK_FALSE(seq.is_retry(2, 1)); // next READ
    CHECK_FALSE(seq.is_retry(3, 0)); // same number, new length: a new READ
}

TEST_CASE("a wider sequence number works the same way")
{
    ReadSequence seq;
    seq.set(true);
    seq.remember(2, 5, bytes("AB"), READ_OK);
    CHECK(seq.is_retry(2, 5));
    CHECK_FALSE(seq.is_retry(2, 6));
}

TEST_CASE("a retry gets the last chunk and its result again")
{
    ReadSequence seq;
    seq.set(true);
    ByteBuffer chunk;

    SUBCASE("a chunk shorter than asked for")
    {
        seq.remember(16, 0, bytes("ABC"), READ_OK);
        CHECK(seq.replay(chunk).is_success());
        CHECK(chunk == bytes("ABC"));
    }

    SUBCASE("a READ that failed")
    {
        seq.remember(4, 0, ByteBuffer(), READ_FAILED);
        chunk = bytes("junk");
        CHECK(seq.replay(chunk).is_error());
        CHECK(chunk.empty());
    }
}

TEST_CASE("turning the mode off or on again forgets the last READ")
{
    ReadSequence seq;
    seq.set(true);
    seq.remember(2, 0, bytes("AB"), READ_OK);

    seq.set(false); // what OPEN and CLOSE do
    CHECK(seq.length(0x8002) == 0x8002);
    CHECK_FALSE(seq.is_retry(2, 0));

    seq.set(true);
    CHECK_FALSE(seq.is_retry(2, 0));
}
