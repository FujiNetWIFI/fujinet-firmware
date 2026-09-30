#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdint>

#include "media/atari/casTape.h"

// Record durations, worked out by hand from the wire format: 10 bits per byte, in whole milliseconds,
// each rounded up.

TEST_CASE("bytes take ten bits each at the baud")
{
    CHECK(cas_bits_duration_ms(132, 600) == 2200);
    CHECK(cas_bits_duration_ms(132, 1200) == 1100);
    CHECK(cas_bits_duration_ms(1, 10000) == 1);
    CHECK(cas_bits_duration_ms(0, 600) == 0);
}

TEST_CASE("a partial millisecond counts as a whole one")
{
    CHECK(cas_bits_duration_ms(132, 6580) == 201); // 200.6
    CHECK(cas_bits_duration_ms(1, 9600) == 2);     // 1.04
}

TEST_CASE("a baud of zero, which a malformed baud chunk can give, takes no time")
{
    CHECK(cas_bits_duration_ms(132, 0) == 0);
    CHECK(cas_data_record_ms(250, 132, 0) == 250);
}

TEST_CASE("a data record is its gap and then its bytes")
{
    CHECK(cas_data_record_ms(250, 132, 600) == 2450);
    CHECK(cas_data_record_ms(0, 132, 600) == 2200);
    CHECK(cas_data_record_ms(3000, 0, 600) == 3000);
    CHECK(cas_data_record_ms(0, 0, 600) == 0);
    CHECK(cas_data_record_ms(65535, 65535, 600) == 65535 + 1092250);
}

TEST_CASE("durations saturate instead of wrapping")
{
    CHECK(cas_bits_duration_ms(SIZE_MAX / 2, 1) == UINT32_MAX);
    CHECK(cas_data_record_ms(65535, SIZE_MAX / 2, 1) == UINT32_MAX);
}

TEST_CASE("a legacy raw block is its 132 wire bytes at 600 baud and then 300 ms")
{
    CHECK(CAS_LEGACY_WIRE_BYTES == 132);
    CHECK(CAS_DEFAULT_BAUD == 600);
    CHECK(CAS_LEGACY_BLOCK_MS == 2200 + 300);
}
