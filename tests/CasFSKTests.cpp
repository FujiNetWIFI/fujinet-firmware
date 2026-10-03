#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "media/atari/casFSK.h"

// ------------------------------------------------------------------------------------------------
// A tiny in-memory A8CAS byte buffer + FSKReadFn adapter, so fsk_scan_run can be driven directly
// without a real file. Mirrors the shape of real corpus files inspected during PR-2's design
// (C:\Temp\TT): an 8-byte chunk header (4-byte type, LE16 length, LE16 irg) followed by `length`
// bytes of payload.
// ------------------------------------------------------------------------------------------------

namespace
{

struct FakeCas
{
    std::vector<uint8_t> bytes;

    void add_chunk(const char type[4], uint16_t irg, const std::vector<uint16_t> &values)
    {
        const size_t base = bytes.size();
        bytes.resize(base + 8 + values.size() * 2);
        std::memcpy(&bytes[base], type, 4);
        bytes[base + 4] = static_cast<uint8_t>(values.size() * 2 & 0xFF);
        bytes[base + 5] = static_cast<uint8_t>((values.size() * 2 >> 8) & 0xFF);
        bytes[base + 6] = static_cast<uint8_t>(irg & 0xFF);
        bytes[base + 7] = static_cast<uint8_t>((irg >> 8) & 0xFF);
        for (size_t i = 0; i < values.size(); i++)
        {
            bytes[base + 8 + i * 2]     = static_cast<uint8_t>(values[i] & 0xFF);
            bytes[base + 8 + i * 2 + 1] = static_cast<uint8_t>((values[i] >> 8) & 0xFF);
        }
    }

    // Appends a chunk header only (no payload bytes actually present), for truncation tests.
    void add_truncated_header(const char type[4], uint16_t length, uint16_t irg)
    {
        const size_t base = bytes.size();
        bytes.resize(base + 8);
        std::memcpy(&bytes[base], type, 4);
        bytes[base + 4] = static_cast<uint8_t>(length & 0xFF);
        bytes[base + 5] = static_cast<uint8_t>((length >> 8) & 0xFF);
        bytes[base + 6] = static_cast<uint8_t>(irg & 0xFF);
        bytes[base + 7] = static_cast<uint8_t>((irg >> 8) & 0xFF);
    }
};

size_t fake_cas_read(void *ctx, size_t offset, uint8_t *dst, size_t n)
{
    const FakeCas *cas = static_cast<const FakeCas *>(ctx);
    if (offset >= cas->bytes.size())
        return 0;
    const size_t avail = cas->bytes.size() - offset;
    const size_t take = n < avail ? n : avail;
    std::memcpy(dst, &cas->bytes[offset], take);
    return take;
}

} // namespace

// ------------------------------------------------------------------------------------------------
// Pure value/level/tick helpers
// ------------------------------------------------------------------------------------------------

TEST_CASE("fsk_is_chunk_type recognizes exactly the 'fsk ' tag")
{
    const uint8_t fsk[4]  = {'f', 's', 'k', ' '};
    const uint8_t data[4] = {'d', 'a', 't', 'a'};
    const uint8_t baud[4] = {'b', 'a', 'u', 'd'};
    const uint8_t fsk2[4] = {'f', 's', 'k', 'x'}; // close but not it
    CHECK(fsk_is_chunk_type(fsk));
    CHECK_FALSE(fsk_is_chunk_type(data));
    CHECK_FALSE(fsk_is_chunk_type(baud));
    CHECK_FALSE(fsk_is_chunk_type(fsk2));
}

TEST_CASE("fsk_decode_le16 reads little-endian")
{
    const uint8_t p1[2] = {0x34, 0x12};
    CHECK(fsk_decode_le16(p1) == 0x1234);
    const uint8_t p2[2] = {0xFF, 0xFF};
    CHECK(fsk_decode_le16(p2) == 0xFFFF);
    const uint8_t p3[2] = {0x00, 0x00};
    CHECK(fsk_decode_le16(p3) == 0);
}

TEST_CASE("output level follows value-index parity")
{
    CHECK_FALSE(fsk_level_for_index(0));
    CHECK(fsk_level_for_index(1));
    CHECK_FALSE(fsk_level_for_index(2));
    CHECK(fsk_level_for_index(3));
    CHECK(fsk_level_for_index(252121)); // Zorro's last value index is odd
}

TEST_CASE("one A8CAS unit is 100 RMT ticks (1 MHz clock, 1/10 ms unit)")
{
    CHECK(fsk_ticks_for_value(0) == 0);
    CHECK(fsk_ticks_for_value(1) == 100);
    CHECK(fsk_ticks_for_value(10) == 1000);
    CHECK(fsk_ticks_for_value(65535) == 6553500);
}

TEST_CASE("a portion never exceeds the RMT 15-bit duration field")
{
    CHECK(fsk_next_portion(0) == 0);
    CHECK(fsk_next_portion(100) == 100);
    CHECK(fsk_next_portion(32767) == 32767);
    CHECK(fsk_next_portion(32768) == 32767);
    CHECK(fsk_next_portion(6553500) == 32767); // largest possible single value's first portion
}

// ------------------------------------------------------------------------------------------------
// fsk_scan_run: structure
// ------------------------------------------------------------------------------------------------

TEST_CASE("a single fsk chunk is a one-chunk run")
{
    FakeCas cas;
    cas.add_chunk("fsk ", 999, {100, 200, 300});
    FSKRunInfo run;
    CHECK(fsk_scan_run(fake_cas_read, &cas, cas.bytes.size(), 0, run));
    CHECK(run.chunk_count == 1);
    CHECK(run.payload_bytes == 6);
    CHECK(run.value_count == 3);
    CHECK(run.leading_irg_ms == 999);
    CHECK(run.next_offset == cas.bytes.size());
}

TEST_CASE("zero-IRG fsk chunks join into one run")
{
    FakeCas cas;
    cas.add_chunk("fsk ", 999, {1, 2, 3});
    cas.add_chunk("fsk ", 0, {4, 5});
    cas.add_chunk("fsk ", 0, {6});
    FSKRunInfo run;
    CHECK(fsk_scan_run(fake_cas_read, &cas, cas.bytes.size(), 0, run));
    CHECK(run.chunk_count == 3);
    CHECK(run.value_count == 6);
    CHECK(run.leading_irg_ms == 999); // only the first chunk's irg is the run's gap
    CHECK(run.next_offset == cas.bytes.size());
}

TEST_CASE("a non-zero IRG on a later fsk chunk starts a fresh run, not a join")
{
    FakeCas cas;
    cas.add_chunk("fsk ", 0, {1, 2});
    cas.add_chunk("fsk ", 500, {3, 4}); // fresh run: has its own gap
    FSKRunInfo run;
    CHECK(fsk_scan_run(fake_cas_read, &cas, cas.bytes.size(), 0, run));
    CHECK(run.chunk_count == 1);
    CHECK(run.value_count == 2);
    CHECK(run.next_offset == 8 + 4); // stops right after the first chunk
}

TEST_CASE("a run stops at the next non-fsk chunk, mixed tape shape")
{
    // Mirrors the real alien_ambush_telco.cas / *_TURBO_SOFTWARE_FUJINET.cas shape: a tiny
    // 2-value fsk tone chunk immediately followed by a data chunk.
    FakeCas cas;
    cas.add_chunk("fsk ", 0, {100, 200}); // the tiny tone marker
    cas.add_chunk("data", 254, {0xAAAA, 0xBBBB}); // stand-in payload bytes, contents irrelevant here
    FSKRunInfo run;
    CHECK(fsk_scan_run(fake_cas_read, &cas, cas.bytes.size(), 0, run));
    CHECK(run.chunk_count == 1);
    CHECK(run.payload_bytes == 4);
    CHECK(run.value_count == 2);
    CHECK(run.next_offset == 8 + 4); // right at the 'data' header, not past it
}

TEST_CASE("Zorro-shaped structural fixture: 8 joined chunks, one giant run")
{
    // Same chunk-count/join shape as the real turbo_software_zorro.cas inspected at
    // C:\Temp\TT during PR-2's design (8 joined `fsk ` chunks, single run). Payload sizes here
    // are small stand-ins -- this checks structure (join count, leading irg, stopping at EOF),
    // not the real file's exact byte/value totals.
    FakeCas cas;
    const std::vector<uint16_t> big_chunk(100, 42); // stand-in for a large joined chunk
    cas.add_chunk("fsk ", 999, big_chunk);
    for (int i = 0; i < 6; i++)
        cas.add_chunk("fsk ", 0, big_chunk);
    cas.add_chunk("fsk ", 0, {1, 2, 3}); // the shorter final chunk, as in the real file
    FSKRunInfo run;
    CHECK(fsk_scan_run(fake_cas_read, &cas, cas.bytes.size(), 0, run));
    CHECK(run.chunk_count == 8);
    CHECK(run.value_count == 100 * 7 + 3);
    CHECK(run.leading_irg_ms == 999);
    CHECK(run.next_offset == cas.bytes.size());
}

TEST_CASE("malformed/truncated headers fail safely, not partially")
{
    FSKRunInfo run;

    SUBCASE("header_offset is not an fsk chunk at all")
    {
        FakeCas cas;
        cas.add_chunk("data", 0, {1, 2});
        CHECK_FALSE(fsk_scan_run(fake_cas_read, &cas, cas.bytes.size(), 0, run));
    }

    SUBCASE("header claims more bytes than the file has")
    {
        FakeCas cas;
        cas.add_truncated_header("fsk ", /*length=*/100, /*irg=*/0); // 100 bytes claimed, 0 present
        CHECK_FALSE(fsk_scan_run(fake_cas_read, &cas, cas.bytes.size(), 0, run));
    }

    SUBCASE("header itself is truncated (fewer than 8 bytes available)")
    {
        FakeCas cas;
        cas.bytes = {'f', 's', 'k', ' ', 0x00}; // only 5 bytes, header needs 8
        CHECK_FALSE(fsk_scan_run(fake_cas_read, &cas, cas.bytes.size(), 0, run));
    }

    SUBCASE("odd chunk_length: not a whole number of LE16 values")
    {
        FakeCas cas;
        cas.bytes = {'f', 's', 'k', ' ', 0x03, 0x00, 0x00, 0x00, 1, 2, 3}; // length=3
        CHECK_FALSE(fsk_scan_run(fake_cas_read, &cas, cas.bytes.size(), 0, run));
    }

    SUBCASE("a join's continuation header is truncated")
    {
        FakeCas cas;
        cas.add_chunk("fsk ", 999, {1, 2});
        cas.add_truncated_header("fsk ", 100, 0); // continuation claims payload that isn't there
        CHECK_FALSE(fsk_scan_run(fake_cas_read, &cas, cas.bytes.size(), 0, run));
    }
}

TEST_CASE("a run longer than FSK_RUN_MAX_CHUNKS stops there, not past it")
{
    FakeCas cas;
    cas.add_chunk("fsk ", 999, {1});
    for (size_t i = 1; i < FSK_RUN_MAX_CHUNKS + 5; i++)
        cas.add_chunk("fsk ", 0, {1});
    FSKRunInfo run;
    CHECK(fsk_scan_run(fake_cas_read, &cas, cas.bytes.size(), 0, run));
    CHECK(run.chunk_count == FSK_RUN_MAX_CHUNKS);
    CHECK(run.next_offset < cas.bytes.size()); // more joinable chunks remain past the cap
}

// ------------------------------------------------------------------------------------------------
// Chunk check and join rule (shared by the preload scan and the progressive loader)
// ------------------------------------------------------------------------------------------------

TEST_CASE("fsk_check_chunk judges a complete header against the file")
{
    const uint8_t good[8] = {'f', 's', 'k', ' ', 4, 0, 0, 0};
    const FSKChunkHeader h = fsk_parse_header(good);
    CHECK(h.length == 4);
    CHECK(h.irg == 0);
    CHECK(fsk_check_chunk(h, 100, 100 + 8 + 4) == FSKChunkCheck::ok);
    CHECK(fsk_check_chunk(h, 100, 100 + 8 + 3) == FSKChunkCheck::malformed); // payload past the end

    const uint8_t odd[8] = {'f', 's', 'k', ' ', 3, 0, 0, 0};
    CHECK(fsk_check_chunk(fsk_parse_header(odd), 0, 1000) == FSKChunkCheck::malformed);

    const uint8_t data[8] = {'d', 'a', 't', 'a', 4, 0, 0, 0};
    CHECK(fsk_check_chunk(fsk_parse_header(data), 0, 1000) == FSKChunkCheck::not_fsk);
}

TEST_CASE("only a well-formed chunk without a gap of its own joins the run")
{
    const uint8_t gap[8] = {'f', 's', 'k', ' ', 4, 0, 5, 0};
    const uint8_t nogap[8] = {'f', 's', 'k', ' ', 4, 0, 0, 0};
    const FSKChunkHeader g = fsk_parse_header(gap);
    const FSKChunkHeader n = fsk_parse_header(nogap);
    CHECK(fsk_chunk_joins(FSKChunkCheck::ok, n));
    CHECK_FALSE(fsk_chunk_joins(FSKChunkCheck::ok, g));
    CHECK_FALSE(fsk_chunk_joins(FSKChunkCheck::malformed, n));
    CHECK_FALSE(fsk_chunk_joins(FSKChunkCheck::not_fsk, n));
}

TEST_CASE("an odd continuation with its own gap just ends the run, without one it invalidates it")
{
    FSKRunInfo run;

    FakeCas with_gap;
    with_gap.add_chunk("fsk ", 999, {1, 2});
    with_gap.add_truncated_header("fsk ", 3, 7);
    with_gap.bytes.insert(with_gap.bytes.end(), {1, 2, 3});
    REQUIRE(fsk_scan_run(fake_cas_read, &with_gap, with_gap.bytes.size(), 0, run));
    CHECK(run.chunk_count == 1);
    CHECK(run.next_offset == 8 + 4);

    FakeCas no_gap;
    no_gap.add_chunk("fsk ", 999, {1, 2});
    no_gap.add_truncated_header("fsk ", 3, 0);
    no_gap.bytes.insert(no_gap.bytes.end(), {1, 2, 3});
    CHECK_FALSE(fsk_scan_run(fake_cas_read, &no_gap, no_gap.bytes.size(), 0, run));
}

// ------------------------------------------------------------------------------------------------
// fsk_sum_waveform_ticks: duration
// ------------------------------------------------------------------------------------------------

TEST_CASE("waveform duration is the sum of each value's ticks")
{
    const uint8_t payload[] = {10, 0, 20, 0, 30, 0}; // LE16: 10, 20, 30
    CHECK(fsk_sum_waveform_ticks(payload, 3) == (10 + 20 + 30) * FSK_TICKS_PER_UNIT);
}

TEST_CASE("a zero-duration value contributes no ticks")
{
    const uint8_t payload[] = {0, 0, 10, 0}; // LE16: 0, 10
    CHECK(fsk_sum_waveform_ticks(payload, 2) == 10 * FSK_TICKS_PER_UNIT);
}

TEST_CASE("an empty run has zero waveform duration")
{
    CHECK(fsk_sum_waveform_ticks(nullptr, 0) == 0);
}

TEST_CASE("the largest possible run duration fits in a uint64_t with room to spare")
{
    // 252122 values (Zorro's real value count) all at the max value 65535 -- far larger than any
    // real tape, but checks the accumulator doesn't overflow well before uint64_t would.
    std::vector<uint8_t> payload(252122 * 2, 0xFF); // every LE16 value == 0xFFFF
    const uint64_t ticks = fsk_sum_waveform_ticks(payload.data(), 252122);
    CHECK(ticks == static_cast<uint64_t>(252122) * 65535ULL * FSK_TICKS_PER_UNIT);
}
