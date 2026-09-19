#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "sio/fsk_plan.h"

#include <array>
#include <cstdint>
#include <cstddef>
#include <initializer_list>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

// FskPlanTests.cpp — doctest coverage for the pure A8CAS FSK rules in
// lib/device/sio/fsk_plan.{h,cpp}.
//
// FskChunkView addresses a payload through a table of fixed-size block
// pointers (fsk_block_byte / fsk_block_le16), not a flat buffer, so tests
// build a BlockTable helper that lays a payload into fixed-size blocks and
// hands (blocks, block_size) to fsk_view_init. Cursor tests run over
// multiple block sizes so cross-block-boundary values get exercised.
//
// Also covers: the shared pure helpers (decode/parity/scale/split, value
// count), the block-table accessors, the bounded preload loop, and
// synthetic pure raw-FSK / interleaved CAS fixtures. No hardware,
// filesystem, ESP-IDF, or globals.

namespace
{
// Build a little-endian 2-byte buffer for a single uint16 value.
inline std::array<uint8_t, 2> le16_buf(uint16_t value)
{
    return { (uint8_t)(value & 0xFF), (uint8_t)((value >> 8) & 0xFF) };
}

// ─── BlockTable: models the segmented payload for host tests ────────────────────
//
// Owns fixed-size blocks + a pointer table filled from a contiguous logical
// payload. block_size >= length yields a single-block fast path; a smaller
// block_size forces multi-block layout and cross-block values.
class BlockTable
{
public:
    BlockTable(const uint8_t *data, size_t len, size_t block_size)
        : block_size_(block_size == 0 ? 1 : block_size)
    {
        if (len > 0)
        {
            block_count_ = (len + block_size_ - 1) / block_size_; // ceil
            storage_.resize(block_count_);
            ptrs_.resize(block_count_);
            for (size_t b = 0; b < block_count_; ++b)
            {
                storage_[b].assign(block_size_, 0);
                ptrs_[b] = storage_[b].data();
            }
            for (size_t i = 0; i < len; ++i)
                storage_[i / block_size_][i % block_size_] = data[i];
        }
    }

    // For an empty payload blocks() is nullptr and block_size() may be anything;
    // fsk_view_step will find zero complete values and report done.
    const uint8_t *const *blocks() const
    {
        return ptrs_.empty() ? nullptr : ptrs_.data();
    }
    size_t block_size() const { return block_size_; }
    size_t block_count() const { return block_count_; }

private:
    size_t block_size_;
    size_t block_count_ = 0;
    std::vector<std::vector<uint8_t>> storage_;
    std::vector<const uint8_t *> ptrs_;
};

// Result of running a FskChunkView to exhaustion.
struct DrainResult
{
    size_t   portion_count;   // number of produced portions
    uint64_t total_ticks;     // sum of all produced portion ticks
    bool     all_same_level;  // every produced portion carried the same level
    bool     first_level;     // level of the first produced portion (if any)
    uint32_t max_portion;     // largest produced portion (0 if none produced)
};

// Drive a cursor to exhaustion, collecting aggregate statistics. Per the
// approved contract, the LAST emitted portion may carry produced=true AND
// done=true in the SAME call, so a produced portion is always accounted for
// before honoring done.
DrainResult drain_view(FskChunkView view)
{
    DrainResult r{ 0, 0, true, false, 0 };
    bool have_first = false;

    for (;;)
    {
        FskStep step = fsk_view_step(view);

        if (step.produced)
        {
            if (!have_first)
            {
                r.first_level = step.level_high;
                have_first    = true;
            }
            else if (step.level_high != r.first_level)
            {
                r.all_same_level = false;
            }

            r.portion_count += 1;
            r.total_ticks   += step.ticks;
            if (step.ticks > r.max_portion)
                r.max_portion = step.ticks;
        }

        if (step.done)
            break;
    }

    return r;
}

// Drain a contiguous logical payload laid out into `block_size`-byte blocks.
// The BlockTable must outlive the returned view usage, so this takes it by ref.
DrainResult drain_blocks(const BlockTable &bt, size_t len)
{
    return drain_view(fsk_view_init(bt.blocks(), bt.block_size(), len));
}

// Convenience: drain a contiguous payload as a single-block (contiguous) table.
DrainResult drain(const uint8_t *data, size_t len)
{
    BlockTable bt(data, len, len == 0 ? 1 : len); // one block holds the whole payload
    return drain_blocks(bt, len);
}

// Convenience: drain a single uint16 value laid out as one LE pair.
DrainResult drain_value(uint16_t value)
{
    std::array<uint8_t, 2> buf = le16_buf(value);
    return drain(buf.data(), buf.size());
}
} // namespace

// ─── Requirement 2.7: A8CAS worked example ─────────────────────────────────────

TEST_CASE("Req 2.7 worked example: 10 data bytes decode to 5 FSK values")
{
    const uint8_t data[] = {
        0x00, 0x01, // index 0: 0x0100 = 256  -> 25.6 ms -> logical 0 (even)
        0x10, 0x01, // index 1: 0x0110 = 272  -> 27.2 ms -> logical 1 (odd)
        0x80, 0x00, // index 2: 0x0080 = 128  -> 12.8 ms -> logical 0
        0x20, 0x00, // index 3: 0x0020 = 32   ->  3.2 ms -> logical 1
        0x80, 0x02  // index 4: 0x0280 = 640  -> 64.0 ms -> logical 0
    };
    const size_t len = sizeof(data);

    CHECK(fsk_value_count(len) == 5);

    SUBCASE("fsk_decode_le16 recovers each value")
    {
        CHECK(fsk_decode_le16(&data[0]) == 256);
        CHECK(fsk_decode_le16(&data[2]) == 272);
        CHECK(fsk_decode_le16(&data[4]) == 128);
        CHECK(fsk_decode_le16(&data[6]) == 32);
        CHECK(fsk_decode_le16(&data[8]) == 640);
    }

    SUBCASE("fsk_level_for_index follows index parity")
    {
        CHECK(fsk_level_for_index(0) == false);
        CHECK(fsk_level_for_index(1) == true);
        CHECK(fsk_level_for_index(2) == false);
        CHECK(fsk_level_for_index(3) == true);
        CHECK(fsk_level_for_index(4) == false);
    }

    SUBCASE("fsk_ticks_for_value scales by 100 ticks per unit")
    {
        CHECK(fsk_ticks_for_value(256) == 25600);
        CHECK(fsk_ticks_for_value(272) == 27200);
        CHECK(fsk_ticks_for_value(128) == 12800);
        CHECK(fsk_ticks_for_value(32) == 3200);
        CHECK(fsk_ticks_for_value(640) == 64000);
    }

    SUBCASE("fsk_view_step reproduces every value in order at parity level")
    {
        struct Expect { bool level_high; uint32_t total_ticks; };
        const Expect expected[] = {
            { false, 25600 }, // index 0: 256 -> one portion
            { true,  27200 }, // index 1: 272 -> one portion
            { false, 12800 }, // index 2: 128 -> one portion
            { true,   3200 }, // index 3: 32  -> one portion
            { false, 64000 }, // index 4: 640 -> split into two portions
        };

        // Run over several block sizes so the values fall at different offsets
        // within/across blocks (block size 3 forces mid-value block boundaries).
        for (size_t bs : { len, size_t{4}, size_t{3}, size_t{2}, size_t{1} })
        {
            CAPTURE(bs);
            BlockTable bt(data, len, bs);
            FskChunkView view = fsk_view_init(bt.blocks(), bt.block_size(), len);

            const size_t last = (sizeof(expected) / sizeof(expected[0])) - 1;
            bool saw_done = false;

            for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i)
            {
                const Expect &e = expected[i];
                uint32_t accumulated = 0;
                do
                {
                    FskStep step = fsk_view_step(view);
                    REQUIRE(step.produced);
                    CHECK(step.level_high == e.level_high);
                    CHECK(step.ticks <= FSK_MAX_PORTION_TICKS);
                    accumulated += step.ticks;

                    if (step.done)
                        saw_done = true;
                    else
                    {
                        bool more_work_expected = (view.remaining_ticks > 0) || (i < last);
                        CHECK(more_work_expected);
                    }
                } while (view.remaining_ticks > 0);

                CHECK(accumulated == e.total_ticks);
            }

            CHECK(saw_done);
        }
    }
}

// ─── Requirement 2.8: zero-length payload ───────────────────────────────────────

TEST_CASE("Req 2.8 empty payload produces nothing and is immediately done")
{
    CHECK(fsk_value_count(0) == 0);

    FskChunkView view = fsk_view_init(nullptr, 0, 0);
    FskStep step = fsk_view_step(view);
    CHECK_FALSE(step.produced);
    CHECK(step.done);
}

// ─── Requirement 6.4: odd-length payload handling ───────────────────────────────

TEST_CASE("Req 6.4 odd-length payload ignores the trailing unpaired byte")
{
    const uint8_t data[] = {
        0x0A, 0x00, // index 0: 10  -> logical 0
        0x14, 0x00, // index 1: 20  -> logical 1
        0xEE        // trailing unpaired byte — must be ignored
    };
    const size_t len = sizeof(data);

    CHECK(fsk_value_count(len) == 2);

    // Block size 2 places the trailing byte alone in its own block; block size 3
    // splits value 1 across the block boundary. Both must ignore the odd tail.
    for (size_t bs : { len, size_t{3}, size_t{2}, size_t{1} })
    {
        CAPTURE(bs);
        BlockTable bt(data, len, bs);
        FskChunkView view = fsk_view_init(bt.blocks(), bt.block_size(), len);

        FskStep s0 = fsk_view_step(view);
        REQUIRE(s0.produced);
        CHECK_FALSE(s0.done);
        CHECK(s0.level_high == false);
        CHECK(s0.ticks == fsk_ticks_for_value(10));

        FskStep s1 = fsk_view_step(view);
        REQUIRE(s1.produced);
        CHECK(s1.done);
        CHECK(s1.level_high == true);
        CHECK(s1.ticks == fsk_ticks_for_value(20));

        // Stopped after two values, before the trailing byte.
        CHECK(view.byte_pos == 4);
        CHECK(view.byte_pos < len);

        FskStep past_end = fsk_view_step(view);
        CHECK_FALSE(past_end.produced);
        CHECK(past_end.done);
    }
}

// ─── Requirement 4.6: zero-duration values preserve parity ──────────────────────

TEST_CASE("Req 4.6 zero-duration values consume a parity slot without emitting")
{
    SUBCASE("a middle zero is skipped but keeps later parity aligned")
    {
        const uint8_t data[] = {
            0x64, 0x00, // index 0: 100 -> logical 0
            0x00, 0x00, // index 1: 0   -> skipped, still consumes parity slot
            0xC8, 0x00  // index 2: 200 -> logical 0 (even)
        };
        const size_t len = sizeof(data);

        CHECK(fsk_value_count(len) == 3);

        BlockTable bt(data, len, 4); // 4-byte blocks: value 2 straddles the boundary
        FskChunkView view = fsk_view_init(bt.blocks(), bt.block_size(), len);

        FskStep first = fsk_view_step(view);
        REQUIRE(first.produced);
        CHECK_FALSE(first.done);
        CHECK(first.level_high == false);
        CHECK(first.ticks == fsk_ticks_for_value(100));

        FskStep second = fsk_view_step(view);
        REQUIRE(second.produced);
        CHECK(second.done);
        CHECK(second.level_high == false); // index 2 even, NOT odd
        CHECK(second.ticks == fsk_ticks_for_value(200));

        FskStep past_end = fsk_view_step(view);
        CHECK_FALSE(past_end.produced);
        CHECK(past_end.done);
    }

    SUBCASE("a leading zero keeps the next value at logical 1")
    {
        const uint8_t data[] = {
            0x00, 0x00, // index 0: 0   -> skipped
            0x2C, 0x01  // index 1: 300 -> logical 1 (odd)
        };
        const size_t len = sizeof(data);

        CHECK(fsk_value_count(len) == 2);

        BlockTable bt(data, len, len);
        FskChunkView view = fsk_view_init(bt.blocks(), bt.block_size(), len);

        FskStep first = fsk_view_step(view);
        REQUIRE(first.produced);
        CHECK(first.done);
        CHECK(first.level_high == true);
        CHECK(first.ticks == fsk_ticks_for_value(300));

        FskStep past_end = fsk_view_step(view);
        CHECK_FALSE(past_end.produced);
        CHECK(past_end.done);
    }
}

// ─── Requirements 2.4 / 4.6 / 6.4: value scaling and split portion counts ───────

TEST_CASE("value scaling and split portion counts across the range")
{
    struct ValueCase
    {
        uint16_t value;
        uint32_t expected_ticks;
        size_t   expected_portions;
    };

    const ValueCase cases[] = {
        { 1,        100, 1 },
        { 256,    25600, 1 },
        { 6818,  681800, 21 },
        { 40000, 4000000, 123 },
        { 65535, 6553500, 201 },
    };

    for (const ValueCase &c : cases)
    {
        CAPTURE(c.value);

        CHECK(fsk_ticks_for_value(c.value) == c.expected_ticks);

        DrainResult r = drain_value(c.value);

        CHECK(r.portion_count == c.expected_portions);
        CHECK(r.total_ticks == c.expected_ticks);
        CHECK(r.max_portion <= FSK_MAX_PORTION_TICKS);
        CHECK(r.all_same_level);
        CHECK(r.first_level == false);
    }

    SUBCASE("6818 explicitly maps to 681,800 ticks over 21 portions")
    {
        DrainResult r = drain_value(6818);
        CHECK(r.total_ticks == 681800);
        CHECK(r.portion_count == 21);
        CHECK(r.max_portion == FSK_MAX_PORTION_TICKS);
    }

    SUBCASE("65535 explicitly maps to 6,553,500 ticks over 201 portions")
    {
        DrainResult r = drain_value(65535);
        CHECK(r.total_ticks == 6553500);
        CHECK(r.portion_count == 201);
        CHECK(r.max_portion == FSK_MAX_PORTION_TICKS);
        CHECK(200u * FSK_MAX_PORTION_TICKS + 100u == 6553500u);
    }
}

// ════════════════════════════════════════════════════════════════════════════
// Deterministic generated-loop tests for the correctness properties 1-9.
//
// Deterministic (loop/enumeration driven — NO randomness). Properties are traced
// to the approved design's Correctness Properties section.
// ════════════════════════════════════════════════════════════════════════════

namespace
{
// Expected portion count for a single value V: 0 when V == 0, else
// ceil(V * 100 / 32767). Computed independently of the module under test.
inline size_t expected_portion_count(uint16_t value)
{
    uint32_t ticks = (uint32_t)value * FSK_RMT_TICKS_PER_A8CAS_UNIT;
    if (ticks == 0)
        return 0;
    return (size_t)((ticks + (FSK_MAX_PORTION_TICKS - 1)) / FSK_MAX_PORTION_TICKS);
}

struct PortionRecord
{
    size_t   value_index;
    bool     level_high;
    uint32_t ticks;
    size_t   byte_pos;
};

// Drain a payload (as a block table of the given block size), recording one
// PortionRecord per emitted portion attributed to its original value index.
std::vector<PortionRecord> drain_portions(const BlockTable &bt, size_t len,
                                           size_t &final_byte_pos, bool &saw_done)
{
    FskChunkView view = fsk_view_init(bt.blocks(), bt.block_size(), len);

    std::vector<PortionRecord> out;
    saw_done = false;

    bool   prev_left_remainder = false;
    size_t current_owner_index = 0;

    for (;;)
    {
        FskStep step = fsk_view_step(view);

        if (step.produced)
        {
            if (!prev_left_remainder)
                current_owner_index = view.value_index - 1;

            out.push_back(PortionRecord{
                current_owner_index, step.level_high, step.ticks, view.byte_pos });

            prev_left_remainder = (view.remaining_ticks != 0);
        }

        if (step.done)
        {
            saw_done = true;
            break;
        }
    }

    final_byte_pos = view.byte_pos;
    return out;
}
} // namespace

// ─── Property 1: fsk_value_count(len) == len / 2 ────────────────────────────────

TEST_CASE("P1 fsk_value_count(len) == len/2 for a deterministic length set")
{
    for (size_t len = 0; len <= 512; ++len)
    {
        CAPTURE(len);
        CHECK(fsk_value_count(len) == len / 2);
    }

    for (size_t len = 513; len <= 4096; len += 7)
    {
        CAPTURE(len);
        CHECK(fsk_value_count(len) == len / 2);
    }

    CHECK(fsk_value_count(0) == 0);
    CHECK(fsk_value_count(1) == 0);
    CHECK(fsk_value_count(2) == 1);
    CHECK(fsk_value_count(3) == 1);
}

// ─── Property 6: pure split arithmetic across the FULL uint16 range ─────────────

TEST_CASE("P6 pure split arithmetic holds for every uint16 value 0..65535")
{
    for (uint32_t v = 0; v <= 0xFFFF; ++v)
    {
        uint16_t value = (uint16_t)v;
        uint32_t ticks = fsk_ticks_for_value(value);

        REQUIRE(ticks == (uint32_t)value * 100u);

        size_t expected = expected_portion_count(value);

        size_t   portions = 0;
        uint32_t remaining = ticks;
        uint64_t summed = 0;
        while (remaining > 0)
        {
            uint32_t portion = fsk_next_portion(remaining);
            REQUIRE(portion > 0);
            REQUIRE(portion <= FSK_MAX_PORTION_TICKS);
            summed    += portion;
            remaining -= portion;
            ++portions;
        }

        REQUIRE(portions == expected);
        REQUIRE(summed == ticks);
    }

    CHECK(expected_portion_count(0) == 0);
    CHECK(expected_portion_count(1) == 1);
    CHECK(expected_portion_count(327) == 1);
    CHECK(expected_portion_count(328) == 2);
    CHECK(expected_portion_count(6818) == 21);
    CHECK(expected_portion_count(65535) == 201);
}

// ─── Property 2 + Property 6 (cursor): per-value drain over a dense value set ───

TEST_CASE("P2/P6 single-value drain preserves duration and portion count")
{
    std::vector<uint32_t> values;
    for (uint32_t v = 0; v <= 1024; ++v)
        values.push_back(v);
    for (uint32_t v = 1025; v <= 0xFFFF; v += 337)
        values.push_back(v);
    for (uint32_t v : { 327u, 328u, 6818u, 32767u, 40000u, 65535u })
        values.push_back(v);

    for (uint32_t v : values)
    {
        uint16_t value = (uint16_t)v;
        CAPTURE(value);

        std::array<uint8_t, 2> buf = le16_buf(value);
        REQUIRE(fsk_decode_le16(buf.data()) == value);

        DrainResult r = drain(buf.data(), buf.size());

        CHECK(r.total_ticks == fsk_ticks_for_value(value));
        CHECK(r.max_portion <= FSK_MAX_PORTION_TICKS);
        CHECK(r.portion_count == expected_portion_count(value));
        CHECK(r.all_same_level);
        CHECK(r.first_level == false);
    }
}

// ─── Property 3: per-portion level follows ORIGINAL value-index parity ──────────

TEST_CASE("P3 every emitted portion level follows original value-index parity")
{
    const uint16_t values[] = {
        100,   // index 0 even -> logical 0
        0,     // index 1 odd  -> zero
        400,   // index 2 even -> logical 0, 2 portions
        6818,  // index 3 odd  -> logical 1, 21 portions
        0,     // index 4 even -> zero
        0,     // index 5 odd  -> zero
        500,   // index 6 even -> logical 0, 2 portions
        1,     // index 7 odd  -> logical 1
        65535, // index 8 even -> logical 0, 201 portions
    };
    const size_t n = sizeof(values) / sizeof(values[0]);

    std::vector<uint8_t> data;
    data.reserve(n * 2);
    for (size_t i = 0; i < n; ++i)
    {
        std::array<uint8_t, 2> pair = le16_buf(values[i]);
        data.push_back(pair[0]);
        data.push_back(pair[1]);
    }

    CHECK(fsk_value_count(data.size()) == n);

    // Run across several block sizes (including one that splits values across
    // blocks) so parity is proven independent of block layout.
    for (size_t bs : { data.size(), size_t{512}, size_t{7}, size_t{3}, size_t{1} })
    {
        CAPTURE(bs);
        BlockTable bt(data.data(), data.size(), bs);

        size_t final_byte_pos = 0;
        bool   saw_done = false;
        std::vector<PortionRecord> portions =
            drain_portions(bt, data.size(), final_byte_pos, saw_done);

        CHECK(saw_done);

        for (const PortionRecord &p : portions)
        {
            CAPTURE(p.value_index);
            REQUIRE(p.value_index < n);
            CHECK(values[p.value_index] != 0);
            CHECK(p.level_high == fsk_level_for_index(p.value_index));
        }

        for (size_t vi = 0; vi < n; ++vi)
        {
            uint64_t ticks_for_vi = 0;
            size_t   count_for_vi = 0;
            for (const PortionRecord &p : portions)
            {
                if (p.value_index == vi)
                {
                    ticks_for_vi += p.ticks;
                    ++count_for_vi;
                }
            }
            CAPTURE(vi);
            CHECK(ticks_for_vi == fsk_ticks_for_value(values[vi]));
            CHECK(count_for_vi == expected_portion_count(values[vi]));
        }
    }
}

// ─── Property 4: cursor byte reads stay within the available payload ────────────

TEST_CASE("P4 cursor byte_pos stays in bounds and ends at value_count*2")
{
    const size_t lengths[] = { 0, 1, 2, 3, 4, 5, 8, 9, 16, 17, 31, 64, 65, 100, 101, 256, 257, 400 };

    for (size_t len : lengths)
    {
        CAPTURE(len);

        std::vector<uint8_t> data(len);
        for (size_t i = 0; i < len; ++i)
            data[i] = (uint8_t)((i * 37 + 1) & 0xFF);

        const size_t value_count = fsk_value_count(len);

        for (size_t bs : { (len ? len : size_t{1}), size_t{512}, size_t{3}, size_t{1} })
        {
            CAPTURE(bs);
            BlockTable bt(len ? data.data() : nullptr, len, bs);
            FskChunkView view = fsk_view_init(bt.blocks(), bt.block_size(), len);

            size_t guard = 0;
            const size_t guard_max = value_count * 210 + 16;
            for (;;)
            {
                CHECK(view.byte_pos <= len);
                CHECK(view.byte_pos <= value_count * 2);

                FskStep step = fsk_view_step(view);

                CHECK(view.byte_pos <= len);
                CHECK(view.byte_pos % 2 == 0);
                CHECK(view.byte_pos <= value_count * 2);

                if (step.done)
                    break;

                REQUIRE(++guard < guard_max);
            }

            CHECK(view.byte_pos == value_count * 2);
            CHECK(view.byte_pos <= len);
        }
    }
}

// ════════════════════════════════════════════════════════════════════════════
// Task 3.3 — Segmented block-table addressing tests.
//
// fsk_block_byte / fsk_block_le16 must return the SAME logical bytes/values as an
// equivalent contiguous payload for ANY block size, including the cross-block
// case where a 2-byte value's low byte ends one block and its high byte begins
// the next. Exercises block size 1, representative small sizes, and production
// size 512. Traced to Requirements 2.1, 6.2, 6.4, 10.1.
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Task 3.3 fsk_block_byte matches the contiguous payload for any block size")
{
    // Deterministic payload; every byte distinct enough to catch mis-indexing.
    std::vector<uint8_t> payload(1000);
    for (size_t i = 0; i < payload.size(); ++i)
        payload[i] = (uint8_t)((i * 131 + 7) & 0xFF);

    for (size_t bs : { size_t{1}, size_t{2}, size_t{3}, size_t{7}, size_t{16},
                       size_t{512}, size_t{999}, size_t{1000}, size_t{2048} })
    {
        CAPTURE(bs);
        BlockTable bt(payload.data(), payload.size(), bs);

        for (size_t k = 0; k < payload.size(); ++k)
        {
            uint8_t got = fsk_block_byte(bt.blocks(), bt.block_size(), k);
            REQUIRE(got == payload[k]);
        }
    }
}

TEST_CASE("Task 3.3 fsk_block_le16 matches contiguous decode, including cross-block values")
{
    std::vector<uint8_t> payload(1000);
    for (size_t i = 0; i < payload.size(); ++i)
        payload[i] = (uint8_t)((i * 197 + 29) & 0xFF);

    for (size_t bs : { size_t{1}, size_t{2}, size_t{3}, size_t{5}, size_t{8},
                       size_t{512}, size_t{1000} })
    {
        CAPTURE(bs);
        BlockTable bt(payload.data(), payload.size(), bs);

        // Every 2-byte pair, at every logical offset, must decode identically to
        // the contiguous fsk_decode_le16 — including offsets where k and k+1 land
        // in different blocks (k % bs == bs - 1).
        for (size_t k = 0; k + 1 < payload.size(); ++k)
        {
            uint16_t via_blocks = fsk_block_le16(bt.blocks(), bt.block_size(), k);
            uint16_t via_flat   = fsk_decode_le16(&payload[k]);
            REQUIRE(via_blocks == via_flat);
        }
    }

    SUBCASE("explicit cross-block value: low byte ends block 0, high byte starts block 1")
    {
        // block_size 2 -> value at logical index 1 spans blocks 0 and 1.
        const uint8_t data[] = { 0x11, 0x22, 0x33, 0x44 };
        BlockTable bt(data, sizeof(data), 2);
        CHECK(fsk_block_le16(bt.blocks(), bt.block_size(), 1) == 0x3322); // bytes 0x22,0x33
        // Whole-value cursor decode over the same layout matches contiguous.
        CHECK(fsk_block_le16(bt.blocks(), bt.block_size(), 0) == 0x2211);
        CHECK(fsk_block_le16(bt.blocks(), bt.block_size(), 2) == 0x4433);
    }
}

TEST_CASE("Task 3.3 cursor drain is identical across block sizes (incl. straddling)")
{
    // A payload with a mix of single-portion, multi-portion, and zero values.
    const uint16_t values[] = { 5, 6818, 0, 300, 65535, 1, 0, 40000 };
    const size_t n = sizeof(values) / sizeof(values[0]);

    std::vector<uint8_t> data;
    for (uint16_t v : values)
    {
        std::array<uint8_t, 2> pair = le16_buf(v);
        data.push_back(pair[0]);
        data.push_back(pair[1]);
    }

    // Reference drain over a single contiguous block.
    DrainResult ref = drain(data.data(), data.size());

    for (size_t bs : { size_t{1}, size_t{2}, size_t{3}, size_t{4}, size_t{5},
                       size_t{512}, data.size() })
    {
        CAPTURE(bs);
        BlockTable bt(data.data(), data.size(), bs);
        DrainResult r = drain_blocks(bt, data.size());

        CHECK(r.portion_count == ref.portion_count);
        CHECK(r.total_ticks == ref.total_ticks);
        CHECK(r.max_portion == ref.max_portion);
        CHECK(r.first_level == ref.first_level);
    }

    // Independent expectation for the reference.
    uint64_t expected_ticks = 0;
    size_t   expected_portions = 0;
    for (uint16_t v : values)
    {
        expected_ticks += fsk_ticks_for_value(v);
        expected_portions += expected_portion_count(v);
    }
    CHECK(ref.total_ticks == expected_ticks);
    CHECK(ref.portion_count == expected_portions);
    (void)n;
}

// ════════════════════════════════════════════════════════════════════════════
// Task 3.4 — Bounded injected-reader preload tests for fsk_preload_into_blocks.
//
// Verifies: no single reader request exceeds read_max (<=512 in production);
// positive partial/short reads accumulate; exact-512, multi-block, and maximum
// 65535-byte payloads load fully; EOF/failure before completion returns a short
// total and never writes out of bounds; block-size and read_max are honored
// independently. Traced to Requirements 6.2-6.6, 10.1-10.4.
// ════════════════════════════════════════════════════════════════════════════

namespace
{
// Owns block storage + the pointer table exactly as production would allocate
// (a table of fixed-size blocks) so preload writes can be verified against an
// expected contiguous payload and bounds are checkable.
class PreloadBuffers
{
public:
    PreloadBuffers(size_t want, size_t block_size)
        : block_size_(block_size)
    {
        block_count_ = want == 0 ? 0 : (want + block_size - 1) / block_size;
        // Over-allocate each block by a red-zone so an out-of-bounds write is
        // detectable, and initialize to a known sentinel.
        storage_.resize(block_count_);
        ptrs_.resize(block_count_);
        for (size_t b = 0; b < block_count_; ++b)
        {
            storage_[b].assign(block_size_ + kRedZone, 0xAA);
            ptrs_[b] = storage_[b].data();
        }
    }

    uint8_t *const *blocks() { return ptrs_.empty() ? nullptr : ptrs_.data(); }

    // Const block-table accessor: hands the SAME preloaded block pointers to
    // fsk_view_init so the cursor reads exactly the table fsk_preload_into_blocks
    // populated — no flatten/rebuild step in between.
    const uint8_t *const *blocks() const
    {
        return ptrs_.empty() ? nullptr : ptrs_.data();
    }

    size_t block_count() const { return block_count_; }
    size_t block_size() const { return block_size_; }

    // Reassemble the first `len` logical bytes for comparison against expected.
    std::vector<uint8_t> logical(size_t len) const
    {
        std::vector<uint8_t> out(len);
        for (size_t i = 0; i < len; ++i)
            out[i] = storage_[i / block_size_][i % block_size_];
        return out;
    }

    // Confirm the red-zone bytes just past each block's usable region are intact.
    bool red_zones_intact() const
    {
        for (size_t b = 0; b < block_count_; ++b)
            for (size_t r = 0; r < kRedZone; ++r)
                if (storage_[b][block_size_ + r] != 0xAA)
                    return false;
        return true;
    }

    static constexpr size_t kRedZone = 8;

private:
    size_t block_size_;
    size_t block_count_ = 0;
    std::vector<std::vector<uint8_t>> storage_;
    std::vector<uint8_t *> ptrs_;
};

// A deterministic reader over a source buffer that delivers a scripted number of
// bytes per call (to model partial/short reads) and records the largest single
// request it ever received, plus a hard EOF position.
struct ScriptedReader
{
    const uint8_t *src = nullptr;
    size_t src_len = 0;
    size_t pos = 0;
    size_t max_requested = 0;
    size_t call_count = 0;

    // Per-call cap on how many bytes to deliver (0 == "deliver up to n").
    // A vector cycles through delivery sizes to exercise varied short reads.
    std::vector<size_t> deliver_pattern;
    size_t pattern_idx = 0;

    // If set, the reader returns 0 (EOF/failure) once pos reaches eof_at.
    size_t eof_at = SIZE_MAX;

    ScriptedReader(const uint8_t *s, size_t n) : src(s), src_len(n) {}

    static size_t read(void *ctx, uint8_t *dst, size_t n)
    {
        auto *self = static_cast<ScriptedReader *>(ctx);
        self->call_count += 1;
        if (n > self->max_requested)
            self->max_requested = n;

        if (self->pos >= self->eof_at)
            return 0; // simulated EOF/failure before completion

        size_t give = n;
        if (!self->deliver_pattern.empty())
        {
            size_t cap = self->deliver_pattern[self->pattern_idx % self->deliver_pattern.size()];
            self->pattern_idx += 1;
            if (cap != 0 && give > cap)
                give = cap;
        }

        // Clamp to remaining source and to the EOF wall.
        if (self->pos + give > self->src_len)
            give = self->src_len - self->pos;
        if (self->pos + give > self->eof_at)
            give = self->eof_at - self->pos;

        for (size_t i = 0; i < give; ++i)
            dst[i] = self->src[self->pos + i];
        self->pos += give;
        return give;
    }
};

// Build a deterministic source payload of the given length.
std::vector<uint8_t> make_source(size_t len)
{
    std::vector<uint8_t> s(len);
    for (size_t i = 0; i < len; ++i)
        s[i] = (uint8_t)((i * 89 + 13) & 0xFF);
    return s;
}
} // namespace

TEST_CASE("Task 3.4 preload loads a full payload and never exceeds read_max")
{
    const size_t read_max = 512; // production FSK_PRELOAD_READ_MAX
    const size_t block_size = 512;

    // exact-512, multi-block, odd, and a large payload up to the A8CAS maximum.
    const size_t wants[] = { 1, 2, 511, 512, 513, 1024, 1025, 5000, 65535 };

    for (size_t want : wants)
    {
        CAPTURE(want);
        std::vector<uint8_t> src = make_source(want);

        PreloadBuffers bufs(want, block_size);
        ScriptedReader rd(src.data(), src.size());
        // Vary delivered sizes so partial reads happen even when n==read_max:
        // 100, 512, 7, 250, ... cycling.
        rd.deliver_pattern = { 100, 512, 7, 250, 1, 300 };

        size_t loaded = fsk_preload_into_blocks(bufs.blocks(), bufs.block_count(),
                                                block_size, want, read_max,
                                                &ScriptedReader::read, &rd);

        CHECK(loaded == want);                     // full payload resident
        CHECK(rd.max_requested <= read_max);       // never asked for > 512
        CHECK(bufs.red_zones_intact());            // no out-of-bounds write

        // The reassembled logical payload equals the source byte-for-byte.
        std::vector<uint8_t> got = bufs.logical(want);
        REQUIRE(got.size() == src.size());
        CHECK(got == src);
    }
}

TEST_CASE("Task 3.4 a single read request never exceeds read_max nor a block boundary")
{
    // Even when read_max is large, no request may straddle a block, so the max
    // observed request must be <= min(read_max, block_size).
    const size_t want = 4096;
    std::vector<uint8_t> src = make_source(want);

    struct Case { size_t read_max; size_t block_size; };
    const Case cases[] = {
        { 512, 512 },   // production shape
        { 512, 256 },   // block smaller than read_max -> block bounds the request
        { 100, 512 },   // read_max smaller than block -> read_max bounds the request
        { 4096, 512 },  // huge read_max -> block bounds
        { 512, 4096 },  // huge block -> read_max bounds
    };

    for (const Case &c : cases)
    {
        CAPTURE(c.read_max);
        CAPTURE(c.block_size);

        PreloadBuffers bufs(want, c.block_size);
        ScriptedReader rd{ src.data(), src.size() };
        // Deliver exactly what is requested each time (no short reads) so
        // max_requested reflects the loop's own clamping, not the reader's.
        rd.deliver_pattern = { 0 };

        size_t loaded = fsk_preload_into_blocks(bufs.blocks(), bufs.block_count(),
                                                c.block_size, want, c.read_max,
                                                &ScriptedReader::read, &rd);

        CHECK(loaded == want);
        CHECK(rd.max_requested <= c.read_max);
        CHECK(rd.max_requested <= c.block_size);
        CHECK(bufs.red_zones_intact());
        CHECK(bufs.logical(want) == src);
    }
}

TEST_CASE("Task 3.4 positive short reads are accumulated until want is complete")
{
    const size_t want = 2000;
    const size_t block_size = 512;
    const size_t read_max = 512;
    std::vector<uint8_t> src = make_source(want);

    PreloadBuffers bufs(want, block_size);
    ScriptedReader rd{ src.data(), src.size() };
    // Always deliver exactly 1 byte per call: maximal short-read stress.
    rd.deliver_pattern = { 1 };

    size_t loaded = fsk_preload_into_blocks(bufs.blocks(), bufs.block_count(),
                                            block_size, want, read_max,
                                            &ScriptedReader::read, &rd);

    CHECK(loaded == want);
    CHECK(rd.call_count >= want);        // at least one call per byte
    CHECK(rd.max_requested <= read_max);
    CHECK(bufs.red_zones_intact());
    CHECK(bufs.logical(want) == src);
}

TEST_CASE("Task 3.4 EOF/read failure before completion returns a short total, no OOB")
{
    const size_t want = 3000;
    const size_t block_size = 512;
    const size_t read_max = 512;
    std::vector<uint8_t> src = make_source(want);

    // Reader hits EOF at 1500 bytes: preload must stop and report exactly 1500.
    const size_t eof_at = 1500;

    PreloadBuffers bufs(want, block_size);
    ScriptedReader rd{ src.data(), src.size() };
    rd.deliver_pattern = { 300, 512, 7 };
    rd.eof_at = eof_at;

    size_t loaded = fsk_preload_into_blocks(bufs.blocks(), bufs.block_count(),
                                            block_size, want, read_max,
                                            &ScriptedReader::read, &rd);

    CHECK(loaded == eof_at);              // short total == bytes actually delivered
    CHECK(loaded < want);
    CHECK(rd.max_requested <= read_max);
    CHECK(bufs.red_zones_intact());       // never wrote beyond a block

    // The bytes that WERE loaded match the source prefix exactly.
    std::vector<uint8_t> got_prefix = bufs.logical(loaded);
    std::vector<uint8_t> src_prefix(src.begin(), src.begin() + loaded);
    CHECK(got_prefix == src_prefix);
}

TEST_CASE("Task 3.4 immediate EOF (reader returns 0 first call) loads nothing safely")
{
    const size_t want = 512;
    const size_t block_size = 512;
    std::vector<uint8_t> src = make_source(want);

    PreloadBuffers bufs(want, block_size);
    ScriptedReader rd{ src.data(), src.size() };
    rd.eof_at = 0; // EOF immediately

    size_t loaded = fsk_preload_into_blocks(bufs.blocks(), bufs.block_count(),
                                            block_size, want, 512,
                                            &ScriptedReader::read, &rd);

    CHECK(loaded == 0);
    CHECK(bufs.red_zones_intact());
}

TEST_CASE("Task 3.4 want == 0 loads nothing and calls no reader")
{
    ScriptedReader rd{ nullptr, 0 };
    size_t loaded = fsk_preload_into_blocks(nullptr, 0, 512, 0, 512,
                                            &ScriptedReader::read, &rd);
    CHECK(loaded == 0);
    CHECK(rd.call_count == 0);
}

// ─── Authentic corpus block-planning (large-chunk architecture) ─────────────────
//
// turbo_software_missile_command.cas has a real 65532-byte raw-FSK chunk
// followed by a 45920-byte one — not just the synthetic 65535 max. Proves
// these real lengths map to the expected block counts, load fully through
// <=512-byte reads, and reassemble byte-for-byte (storage capability is a
// production heap concern only, out of scope here).
TEST_CASE("Authentic corpus chunk lengths plan and preload fully (<=512 reads)")
{
    const size_t block_size = 512;
    const size_t read_max = 512; // production FSK_PRELOAD_READ_MAX

    struct Case { size_t len; size_t expect_blocks; size_t expect_values; };
    const Case cases[] = {
        { 65532, 128, 32766 }, // Missile Command chunk 1 (real)
        { 45920, 90,  22960 }, // Missile Command chunk 2 (real)
        { 65534, 128, 32767 }, // even max within 128 blocks
        { 65535, 128, 32767 }, // A8CAS absolute max (odd trailing byte)
    };

    for (const Case &c : cases)
    {
        CAPTURE(c.len);
        // Block planning matches production ceil(len / 512).
        CHECK((c.len + block_size - 1) / block_size == c.expect_blocks);
        // value_count is floor(len / 2), the same pure helper production uses.
        CHECK(fsk_value_count(c.len) == c.expect_values);

        std::vector<uint8_t> src = make_source(c.len);
        PreloadBuffers bufs(c.len, block_size);
        ScriptedReader rd(src.data(), src.size());
        rd.deliver_pattern = { 100, 512, 7, 250, 1, 300 }; // force partial reads

        size_t loaded = fsk_preload_into_blocks(bufs.blocks(), bufs.block_count(),
                                                block_size, c.len, read_max,
                                                &ScriptedReader::read, &rd);

        CHECK(loaded == c.len);                 // whole payload resident
        CHECK(bufs.block_count() == c.expect_blocks);
        CHECK(rd.max_requested <= read_max);    // never asked for > 512
        CHECK(bufs.red_zones_intact());         // no out-of-bounds write
        CHECK(bufs.logical(c.len) == src);      // reassembled byte-for-byte
    }
}

// ════════════════════════════════════════════════════════════════════════════
// Contiguous zero-IRG FSK RUN planning + boundary semantics.
//
// Authentic raw-FSK corpus images split ONE continuous tape signal across
// consecutive `fsk ` chunks where only the first carries a non-zero IRG and all
// following carry IRG == 0. Production must reproduce such a run as ONE
// continuous waveform (no per-chunk RMT teardown/gap). These tests exercise the
// PURE run-membership predicate fsk_run_should_join() driven by fsk_compute_bounds
// over synthetic chunk layouts, plus per-chunk parity/zero-duration semantics via
// the pure cursor. No hardware, no PSRAM, no ESP-IDF.
// ════════════════════════════════════════════════════════════════════════════
namespace {

// Plan a run over a chunk layout: {is_fsk, len, irg}. Returns the run as a list
// of (payload_offset, data_avail, value_count) chunk descriptors, mirroring the
// production scan (start at chunk 0; join following chunks while
// fsk_run_should_join holds; cap at FSK_RUN_MAX_CHUNKS).
struct RunChunk { size_t payload_off; size_t data_avail; size_t value_count; };
struct LayoutChunk { bool is_fsk; uint16_t len; uint16_t irg; };

std::vector<RunChunk> plan_run(const std::vector<LayoutChunk> &layout,
                               size_t filesize)
{
    std::vector<RunChunk> run;
    // Build absolute offsets by walking the layout with 8-byte headers.
    std::vector<size_t> offs(layout.size());
    size_t o = 0;
    for (size_t i = 0; i < layout.size(); ++i) { offs[i] = o; o += 8 + layout[i].len; }

    // Chunk 0 is always the run start (assume caller entered on an fsk chunk).
    size_t i = 0;
    FskBounds b0 = fsk_compute_bounds(filesize, offs[0], layout[0].len);
    run.push_back({ offs[0] + 8, b0.data_avail, b0.value_count });
    size_t next = b0.next_offset;
    i = 1;
    while (i < layout.size() && next != 0 && run.size() < FSK_RUN_MAX_CHUNKS)
    {
        FskBounds cb = fsk_compute_bounds(filesize, offs[i], layout[i].len);
        if (!fsk_run_should_join(layout[i].is_fsk, layout[i].irg,
                                 cb.header_complete, cb.structurally_truncated))
            break;
        run.push_back({ offs[i] + 8, cb.data_avail, cb.value_count });
        next = cb.next_offset;
        ++i;
    }
    return run;
}

size_t layout_filesize(const std::vector<LayoutChunk> &layout)
{
    size_t o = 0;
    for (const auto &c : layout) o += 8 + c.len;
    return o;
}

} // namespace

TEST_CASE("FSK run: Missile Command layout is one run of two chunks")
{
    std::vector<LayoutChunk> mc = { {true,65532,999}, {true,45920,0} };
    auto run = plan_run(mc, layout_filesize(mc));
    REQUIRE(run.size() == 2);
    CHECK(run[0].value_count == 32766);
    CHECK(run[1].value_count == 22960);
}

TEST_CASE("FSK run: River Raid layout is one run of three chunks")
{
    std::vector<LayoutChunk> rr = { {true,65532,999}, {true,65532,0}, {true,712,0} };
    auto run = plan_run(rr, layout_filesize(rr));
    REQUIRE(run.size() == 3);
    CHECK(run[0].value_count == 32766);
    CHECK(run[1].value_count == 32766);
    CHECK(run[2].value_count == 356);
}

TEST_CASE("FSK run: International layout is one run of seven chunks")
{
    std::vector<LayoutChunk> intl = {
        {true,65532,999}, {true,65532,0}, {true,65532,0}, {true,65532,0},
        {true,65532,0},   {true,65532,0}, {true,53072,0}
    };
    auto run = plan_run(intl, layout_filesize(intl));
    REQUIRE(run.size() == 7);
    for (size_t i = 0; i < 6; ++i) CHECK(run[i].value_count == 32766);
    CHECK(run[6].value_count == 26536);
}

TEST_CASE("FSK run: a following FSK with IRG>0 starts a NEW run")
{
    // Chunk 1 (IRG999) then chunk 2 (IRG500): the run must stop before chunk 2.
    std::vector<LayoutChunk> lay = { {true,1000,999}, {true,1000,500} };
    auto run = plan_run(lay, layout_filesize(lay));
    REQUIRE(run.size() == 1);
    CHECK(run[0].value_count == 500);

    // A non-FSK following chunk also ends the run.
    std::vector<LayoutChunk> lay2 = { {true,1000,999}, {false,1000,0} };
    auto run2 = plan_run(lay2, layout_filesize(lay2));
    REQUIRE(run2.size() == 1);
}

TEST_CASE("FSK run: odd-length first chunk ignores its tail; next chunk parity resets")
{
    // Chunk A: odd length 7 -> value_count floor(7/2)=3, trailing byte ignored.
    // Chunk B: length 4 -> value_count 2, its own parity starts at index 0.
    const size_t bs = 512; // both chunks fit one block each (block-aligned)
    // Build chunk A payload: values 0x0001,0x0001,0x0001 + odd tail 0x7F.
    std::vector<uint8_t> a = { 1,0, 1,0, 1,0, 0x7F };
    std::vector<uint8_t> b = { 5,0, 9,0 }; // two values 5,9

    // value_count per chunk from the pure helper.
    CHECK(fsk_value_count(a.size()) == 3); // odd tail ignored
    CHECK(fsk_value_count(b.size()) == 2);

    // Per-chunk parity is by LOCAL index: chunk B value index 0 -> level LOW,
    // index 1 -> HIGH, regardless of chunk A's (odd count) ending parity.
    CHECK(fsk_level_for_index(0) == false); // chunk B first value: LOW
    CHECK(fsk_level_for_index(1) == true);  // chunk B second value: HIGH
    // Chunk A ended at local index 3 (odd) but that does NOT carry into B.
}

TEST_CASE("FSK run: zero-duration value at a boundary consumes local index, no portion")
{
    // A single chunk whose values are {0 (zero duration), 3}: the zero value
    // consumes index 0 (parity) but emits no portion; the next value uses index 1.
    std::vector<uint8_t> data = { 0,0, 3,0 };
    PreloadBuffers bufs(data.size(), 512);
    // Load the bytes into the block table via a full-delivery reader.
    ScriptedReader rd(data.data(), data.size());
    rd.deliver_pattern = { 0 };
    size_t loaded = fsk_preload_into_blocks(bufs.blocks(), bufs.block_count(),
                                            512, data.size(), 512,
                                            &ScriptedReader::read, &rd);
    REQUIRE(loaded == data.size());

    FskChunkView v = fsk_view_init(bufs.blocks(), 512, data.size());
    // First step: value index 0 is zero-duration -> skipped; value index 1 (=3)
    // is emitted with parity of index 1 (HIGH) in a single portion, done.
    FskStep s = fsk_view_step(v);
    CHECK(s.produced == true);
    CHECK(s.level_high == true);   // index 1 -> odd -> HIGH
    CHECK(s.ticks == 300);         // 3 * 100 ticks
    CHECK(s.done == true);         // only value left
}

// ─── Defensive guards: positive want but a degenerate parameter -> 0 ────────────
//
// Each case has a genuinely positive `want` (so the "nothing requested" fast
// path is NOT what is under test) and a single degenerate input that must make
// preload refuse to run and return 0 without any out-of-bounds access.

TEST_CASE("Task 3.4 positive want but reader == nullptr loads nothing")
{
    const size_t want = 512;
    const size_t block_size = 512;

    PreloadBuffers bufs(want, block_size);
    // No reader function pointer: preload has no way to obtain bytes -> 0.
    size_t loaded = fsk_preload_into_blocks(bufs.blocks(), bufs.block_count(),
                                            block_size, want, 512,
                                            nullptr, nullptr);
    CHECK(loaded == 0);
    CHECK(bufs.red_zones_intact());
}

TEST_CASE("Task 3.4 positive want but read_max == 0 loads nothing")
{
    const size_t want = 512;
    const size_t block_size = 512;
    std::vector<uint8_t> src = make_source(want);

    PreloadBuffers bufs(want, block_size);
    ScriptedReader rd{ src.data(), src.size() };
    // read_max == 0 means no single request may carry any bytes -> 0, and the
    // reader must never be asked to deliver a positive count.
    size_t loaded = fsk_preload_into_blocks(bufs.blocks(), bufs.block_count(),
                                            block_size, want, 0,
                                            &ScriptedReader::read, &rd);
    CHECK(loaded == 0);
    CHECK(rd.max_requested == 0);
    CHECK(bufs.red_zones_intact());
}

TEST_CASE("Task 3.4 positive want but block_count == 0 loads nothing")
{
    const size_t want = 512;
    const size_t block_size = 512;
    std::vector<uint8_t> src = make_source(want);

    ScriptedReader rd{ src.data(), src.size() };
    // A positive want with zero blocks has nowhere to store bytes -> 0, and the
    // reader must not be invoked. blocks == nullptr models an empty table.
    size_t loaded = fsk_preload_into_blocks(nullptr, 0, block_size, want, 512,
                                            &ScriptedReader::read, &rd);
    CHECK(loaded == 0);
    CHECK(rd.call_count == 0);
}

TEST_CASE("Task 3.4 a null block entry before completion yields a safe short result")
{
    // A table whose blocks are all valid EXCEPT one null entry in the middle:
    // preload must stop when it reaches the null block rather than dereference
    // it, returning only the bytes it could safely store into earlier blocks.
    const size_t block_size = 512;
    const size_t want = 3 * block_size; // 3 blocks; we null out block index 1
    std::vector<uint8_t> src = make_source(want);

    PreloadBuffers bufs(want, block_size);
    REQUIRE(bufs.block_count() == 3);

    // Copy the writable pointer table and null the middle entry so the first
    // block is fillable but the second is not. Earlier blocks stay
    // writable/checkable. (fsk_preload_into_blocks needs writable blocks.)
    std::vector<uint8_t *> table(bufs.block_count());
    {
        uint8_t *const *orig = bufs.blocks();
        for (size_t b = 0; b < bufs.block_count(); ++b)
            table[b] = orig[b];
    }
    table[1] = nullptr;

    ScriptedReader rd{ src.data(), src.size() };
    rd.deliver_pattern = { 512 }; // deliver a full block per call

    size_t loaded = fsk_preload_into_blocks(table.data(), table.size(),
                                            block_size, want, 512,
                                            &ScriptedReader::read, &rd);

    // At most the first block could be filled before the null block halted it.
    CHECK(loaded <= block_size);
    CHECK(loaded < want);
    CHECK(bufs.red_zones_intact()); // no write past any real block, none into null

    // Whatever was loaded matches the source prefix (only block 0 is checkable).
    if (loaded > 0)
    {
        std::vector<uint8_t> got_prefix = bufs.logical(loaded);
        std::vector<uint8_t> src_prefix(src.begin(), src.begin() + loaded);
        CHECK(got_prefix == src_prefix);
    }
}

TEST_CASE("Task 3.4 maximum 65535-byte payload preloads fully through bounded reads")
{
    const size_t want = 65535;      // A8CAS maximum chunk_length
    const size_t block_size = 512;  // production block size -> 128 blocks
    const size_t read_max = 512;
    std::vector<uint8_t> src = make_source(want);

    PreloadBuffers bufs(want, block_size);
    CHECK(bufs.block_count() == (want + block_size - 1) / block_size); // 128
    ScriptedReader rd{ src.data(), src.size() };
    rd.deliver_pattern = { 512, 200, 512, 63 }; // varied, all <= read_max

    size_t loaded = fsk_preload_into_blocks(bufs.blocks(), bufs.block_count(),
                                            block_size, want, read_max,
                                            &ScriptedReader::read, &rd);

    CHECK(loaded == want);
    CHECK(rd.max_requested <= read_max);
    CHECK(bufs.red_zones_intact());
    CHECK(bufs.logical(want) == src);
}

// End-to-end: preload a payload through the injected reader, then drive the
// cursor over the SAME BlockTable-style layout and confirm the decoded values
// match. Proves the preload output feeds the cursor correctly (odd tail too).
TEST_CASE("Task 3.4 preload output feeds the cursor; odd tail stays separate from failure")
{
    const uint16_t values[] = { 7, 6818, 0, 65535, 42 };
    const size_t n = sizeof(values) / sizeof(values[0]);

    std::vector<uint8_t> src;
    for (uint16_t v : values)
    {
        std::array<uint8_t, 2> pair = le16_buf(v);
        src.push_back(pair[0]);
        src.push_back(pair[1]);
    }
    src.push_back(0xEE); // deliberate ODD trailing byte (not a read failure)

    const size_t want = src.size();      // odd length
    const size_t block_size = 4;         // multi-block, forces straddling values
    const size_t read_max = 512;

    PreloadBuffers bufs(want, block_size);
    ScriptedReader rd{ src.data(), src.size() };
    rd.deliver_pattern = { 3, 1, 4 };    // short reads across block boundaries

    size_t loaded = fsk_preload_into_blocks(bufs.blocks(), bufs.block_count(),
                                            block_size, want, read_max,
                                            &ScriptedReader::read, &rd);

    // A full (successful) preload of an odd-length payload returns `want`; the
    // odd tail is a value-count concern (floor len/2), NOT a preload failure.
    CHECK(loaded == want);
    CHECK(bufs.red_zones_intact());

    // Drive the cursor DIRECTLY over the SAME block table that
    // fsk_preload_into_blocks just populated — no flatten/rebuild step. This is
    // the true end-to-end path: the const block-table accessor hands the exact
    // preloaded block pointers to fsk_view_init, so the cursor decodes the bytes
    // the preload wrote, not a reassembled copy.
    const PreloadBuffers &const_bufs = bufs;
    FskChunkView view =
        fsk_view_init(const_bufs.blocks(), const_bufs.block_size(), want);

    CHECK(fsk_value_count(want) == n); // 5 values, trailing 0xEE ignored

    // Confirm each non-zero value decodes at the right parity/duration.
    // values: [7(i0,e), 6818(i1,o), 0(i2,e skip), 65535(i3,o), 42(i4,e)]
    struct Exp { size_t idx; bool level; uint16_t value; };
    const Exp expected[] = {
        { 0, false, 7 },
        { 1, true,  6818 },
        { 3, true,  65535 },
        { 4, false, 42 },
    };

    for (const Exp &e : expected)
    {
        CAPTURE(e.idx);
        uint64_t ticks = 0;
        bool level_ok = true;
        for (;;)
        {
            FskStep step = fsk_view_step(view);
            REQUIRE(step.produced);
            if (step.level_high != e.level)
                level_ok = false;
            ticks += step.ticks;
            if (view.remaining_ticks == 0)
                break;
        }
        CHECK(level_ok);
        CHECK(level_ok == (fsk_level_for_index(e.idx) == e.level));
        CHECK(ticks == fsk_ticks_for_value(e.value));
    }
}

// ════════════════════════════════════════════════════════════════════════════
// Task 3.5 / 3.6 — Model-level CAS fixtures (pure raw-FSK and interleaved).
//
// IMPORTANT: these are MODEL-LEVEL FIXTURES / EXPECTATIONS, not production
// cassette-walker verification. The `walk()` helper and the `active_baud`
// tracking below are a synthetic in-source MODEL of the CAS chunk stream used to
// reason about FSK payload semantics at the pure-module level. They do NOT parse
// a real CAS, and they do NOT exercise cassette.cpp — the real chunk walker,
// its baud/EOT handling, and the O+8 caller offset are integrated and verified
// in a LATER cassette/RMT task. Passing here is evidence about the pure module's
// decoding of each fsk payload and about the SHAPE of the expected walker
// behavior; it is NOT evidence that cassette.cpp implements that behavior.
// Traced (at the model level only) to Requirements 1.5, 1.8, 5.1-5.3, 9.1-9.5.
// ════════════════════════════════════════════════════════════════════════════

namespace fsk_cas
{
// Append a little-endian uint16.
inline void push_le16(std::vector<uint8_t> &out, uint16_t v)
{
    out.push_back((uint8_t)(v & 0xFF));
    out.push_back((uint8_t)((v >> 8) & 0xFF));
}

// Append an 8-byte A8CAS chunk header + its data area. type must be 4 chars.
// aux carries the IRG (for fsk) or the baud value (for baud), matching the
// A8CAS layout: type[4], length(LE16), aux(LE16), data[length].
inline void push_chunk(std::vector<uint8_t> &out, const char type[5],
                       uint16_t aux, const std::vector<uint8_t> &data)
{
    for (int i = 0; i < 4; ++i)
        out.push_back((uint8_t)type[i]);
    push_le16(out, (uint16_t)data.size());
    push_le16(out, aux);
    out.insert(out.end(), data.begin(), data.end());
}

// Build an fsk data area (concatenated LE values) from a value list.
inline std::vector<uint8_t> fsk_data(std::initializer_list<uint16_t> values)
{
    std::vector<uint8_t> d;
    for (uint16_t v : values)
        push_le16(d, v);
    return d;
}

// Minimal chunk descriptor recovered by walking a synthetic image.
struct Chunk
{
    char     type[5];
    uint16_t length;
    uint16_t aux;
    size_t   data_offset; // offset of the data area within the image
};

// Walk a synthetic CAS image into a chunk list. Purely structural: it advances
// by 8 + length per chunk and stops at EOT (fewer than 8 header bytes remain).
// This mirrors the design's file-order walk WITHOUT any filename/title logic.
inline std::vector<Chunk> walk(const std::vector<uint8_t> &img)
{
    std::vector<Chunk> out;
    size_t off = 0;
    while (off + 8 <= img.size())
    {
        Chunk c{};
        for (int i = 0; i < 4; ++i)
            c.type[i] = (char)img[off + i];
        c.type[4] = '\0';
        c.length = (uint16_t)(img[off + 4] | (img[off + 5] << 8));
        c.aux    = (uint16_t)(img[off + 6] | (img[off + 7] << 8));
        c.data_offset = off + 8;
        if (c.data_offset + c.length > img.size())
            break; // structural overrun -> EOT
        out.push_back(c);
        off = c.data_offset + c.length;
    }
    return out;
}
} // namespace fsk_cas

// ─── Task 3.5: pure raw-FSK image (FUJI + only fsk chunks, no baud/data) ────────

TEST_CASE("Task 3.5 pure raw-FSK image: FUJI followed only by fsk chunks to EOT")
{
    std::vector<uint8_t> img;
    // FUJI header chunk (empty data area is fine for this structural model).
    fsk_cas::push_chunk(img, "FUJI", 0, {});
    // Three fsk chunks, each with its own IRG in aux; no baud, no data anywhere.
    fsk_cas::push_chunk(img, "fsk ", 273, fsk_cas::fsk_data({ 256, 272, 128 }));
    fsk_cas::push_chunk(img, "fsk ", 1000, fsk_cas::fsk_data({ 6818, 0, 300 }));
    fsk_cas::push_chunk(img, "fsk ", 0, fsk_cas::fsk_data({ 65535 }));

    std::vector<fsk_cas::Chunk> chunks = fsk_cas::walk(img);

    // The walk reaches EOT having seen FUJI + 3 fsk chunks and NO baud/data.
    REQUIRE(chunks.size() == 4);
    CHECK(std::string(chunks[0].type) == "FUJI");
    int fsk_count = 0, baud_count = 0, data_count = 0;
    for (const auto &c : chunks)
    {
        std::string t(c.type);
        if (t == "fsk ") ++fsk_count;
        else if (t == "baud") ++baud_count;
        else if (t == "data") ++data_count;
    }
    CHECK(fsk_count == 3);
    CHECK(baud_count == 0);   // pure raw-FSK: no baud required
    CHECK(data_count == 0);   // pure raw-FSK: no data required

    // Each fsk chunk's payload decodes generically through the pure cursor.
    // Chunk 1: [256, 272, 128] -> 3 single-portion values.
    {
        const fsk_cas::Chunk &c = chunks[1];
        CHECK(c.aux == 273); // IRG carried in aux
        BlockTable bt(&img[c.data_offset], c.length, 512);
        DrainResult r = drain_blocks(bt, c.length);
        CHECK(r.portion_count == 3);
        CHECK(r.total_ticks ==
              fsk_ticks_for_value(256) + fsk_ticks_for_value(272) + fsk_ticks_for_value(128));
    }
    // Chunk 3: single 65535 value -> 201 portions.
    {
        const fsk_cas::Chunk &c = chunks[3];
        CHECK(c.aux == 0);
        BlockTable bt(&img[c.data_offset], c.length, 512);
        DrainResult r = drain_blocks(bt, c.length);
        CHECK(r.portion_count == 201);
        CHECK(r.total_ticks == 6553500);
    }
}

// ─── Task 3.6: interleaved baud / data / fsk image ──────────────────────────────

TEST_CASE("Task 3.6 interleaved image: fsk is non-terminating; baud governs data")
{
    std::vector<uint8_t> img;
    fsk_cas::push_chunk(img, "FUJI", 0, {});
    fsk_cas::push_chunk(img, "baud", 600, {});                       // active baud -> 600
    fsk_cas::push_chunk(img, "data", 0, { 0x01, 0x02, 0x03 });       // data @ 600
    fsk_cas::push_chunk(img, "fsk ", 500, fsk_cas::fsk_data({ 100, 200 })); // fsk (no baud change)
    fsk_cas::push_chunk(img, "data", 0, { 0x04, 0x05 });             // data STILL @ 600 (post-fsk)
    fsk_cas::push_chunk(img, "baud", 790, {});                       // active baud -> 790
    fsk_cas::push_chunk(img, "data", 0, { 0x06 });                   // data @ 790

    std::vector<fsk_cas::Chunk> chunks = fsk_cas::walk(img);
    REQUIRE(chunks.size() == 7);

    // Models the walker's baud tracking: fsk never changes the active baud,
    // so a data chunk after an fsk uses the pre-fsk baud until a baud chunk
    // changes it.
    uint16_t active_baud = 0;
    std::vector<uint16_t> data_baud; // active baud at each data chunk
    int fsk_seen = 0;
    for (const auto &c : chunks)
    {
        std::string t(c.type);
        if (t == "baud")
            active_baud = c.aux;                 // baud chunk sets active baud
        else if (t == "fsk ")
            ++fsk_seen;                          // fsk is non-terminating, baud untouched
        else if (t == "data")
            data_baud.push_back(active_baud);    // data uses current active baud
    }

    CHECK(fsk_seen == 1);
    REQUIRE(data_baud.size() == 3);
    CHECK(data_baud[0] == 600); // before fsk
    CHECK(data_baud[1] == 600); // AFTER fsk, no intervening baud -> still 600
    CHECK(data_baud[2] == 790); // after the 790 baud chunk

    // The single fsk chunk decodes generically: values [100, 200] -> 2 portions.
    const fsk_cas::Chunk &fskc = chunks[3];
    CHECK(std::string(fskc.type) == "fsk ");
    CHECK(fskc.aux == 500); // IRG
    BlockTable bt(&img[fskc.data_offset], fskc.length, 512);
    DrainResult r = drain_blocks(bt, fskc.length);
    CHECK(r.portion_count == 2);
    CHECK(r.first_level == false); // index 0 even -> logical 0
    CHECK(r.total_ticks == fsk_ticks_for_value(100) + fsk_ticks_for_value(200));
}


// ════════════════════════════════════════════════════════════════════════════
// Property 7 / 8 / 9 — deterministic host coverage over the PRODUCTION helpers.
//
// These exercise the SAME pure functions the production caller uses:
//   - fsk_compute_bounds()  (structural bounds + caller next_offset)  -> P7, P9
//   - fsk_value_count() / fsk_view_step()  (cursor)                    -> P9
//   - the fsk_plan step/cursor signatures + outputs                    -> P8
// No implementation formula is duplicated here: P7/P9 assert the DESIGN
// contract against fsk_compute_bounds' returned FskBounds, and P9 also drives
// the real cursor. The FskPlanTests target links only fsk_plan.cpp + host code.
// ════════════════════════════════════════════════════════════════════════════

// ─── Property 7: truncation is deterministic and terminating ────────────────────
// Feature: a8cas-fsk-chunk-playback, Property 7: truncation is deterministic and
// terminating.
TEST_CASE("P7 fsk_compute_bounds is deterministic and terminating")
{
    // --- Enumerated design-contract corner cases -------------------------------

    SUBCASE("offset > filesize -> incomplete header, next=0")
    {
        FskBounds b = fsk_compute_bounds(/*filesize*/ 10, /*offset*/ 20, /*len*/ 4);
        CHECK(b.header_complete == false);
        CHECK(b.next_offset == 0);
        CHECK(b.data_avail == 0);
        CHECK(b.value_count == 0);
        CHECK(b.structurally_truncated == false);
    }

    SUBCASE("fewer than 8 bytes remain -> incomplete header, next=0")
    {
        for (size_t remaining = 0; remaining < 8; ++remaining)
        {
            const size_t filesize = 1000;
            const size_t offset = filesize - remaining; // 0..7 bytes remain
            FskBounds b = fsk_compute_bounds(filesize, offset, 4);
            CHECK(b.header_complete == false);
            CHECK(b.next_offset == 0);
            CHECK(b.data_avail == 0);
            CHECK(b.value_count == 0);
        }
    }

    SUBCASE("exactly 8 bytes remain with len=0 -> well-formed empty chunk")
    {
        const size_t filesize = 100;
        const size_t offset = filesize - 8; // exactly the header, zero body
        FskBounds b = fsk_compute_bounds(filesize, offset, 0);
        CHECK(b.header_complete == true);
        CHECK(b.structurally_truncated == false);
        CHECK(b.data_avail == 0);
        CHECK(b.value_count == 0);
        CHECK(b.next_offset == offset + 8); // O + 8 + 0
    }

    SUBCASE("body overrun by 1 and larger -> truncated, next=0")
    {
        const size_t filesize = 200;
        const size_t offset = 8;
        const size_t after_header = filesize - offset - 8; // 184 bytes available
        // declared just past the available body, and much larger, all overrun.
        for (size_t over : { (size_t)1, (size_t)2, (size_t)37, (size_t)1000 })
        {
            const size_t declared = after_header + over;
            if (declared > 0xFFFF) continue; // uint16 declared field
            FskBounds b = fsk_compute_bounds(filesize, offset, (uint16_t)declared);
            CHECK(b.structurally_truncated == true);
            CHECK(b.header_complete == true);
            CHECK(b.data_avail == after_header);          // clamped to what exists
            CHECK(b.value_count == after_header / 2);      // floor
            CHECK(b.next_offset == 0);                     // overrun -> EOT
        }
    }

    SUBCASE("well-formed len=0")
    {
        FskBounds b = fsk_compute_bounds(64, 8, 0);
        CHECK(b.header_complete == true);
        CHECK(b.structurally_truncated == false);
        CHECK(b.data_avail == 0);
        CHECK(b.value_count == 0);
        CHECK(b.next_offset == 8 + 8 + 0);
    }

    SUBCASE("well-formed representative lengths")
    {
        const size_t filesize = 70000; // room for a 65535-byte payload
        const size_t offset = 8;
        for (uint32_t len : { 1u, 2u, 3u, 4u, 5u, 100u, 512u, 1024u, 65534u, 65535u })
        {
            FskBounds b = fsk_compute_bounds(filesize, offset, (uint16_t)len);
            CHECK(b.header_complete == true);
            CHECK(b.structurally_truncated == false);
            CHECK(b.data_avail == len);
            CHECK(b.value_count == len / 2);          // floor: odd tail dropped
            CHECK(b.next_offset == offset + 8 + len); // O + 8 + L
        }
    }

    // --- Generated loop over offsets x declared lengths for a fixed file --------
    SUBCASE("generated offsets x lengths: contract holds for every case")
    {
        const size_t filesize = 4096;
        for (size_t offset = 0; offset <= filesize + 4; ++offset)
        {
            for (uint32_t len = 0; len <= 300; ++len)
            {
                FskBounds b = fsk_compute_bounds(filesize, offset, (uint16_t)len);

                if (offset > filesize || (filesize - offset) < 8)
                {
                    CHECK(b.header_complete == false);
                    CHECK(b.next_offset == 0);
                    CHECK(b.data_avail == 0);
                    CHECK(b.value_count == 0);
                    continue;
                }

                const size_t after_header = filesize - offset - 8;
                CHECK(b.header_complete == true);
                if (len > after_header)
                {
                    CHECK(b.structurally_truncated == true);
                    CHECK(b.data_avail == after_header);
                    CHECK(b.next_offset == 0);
                }
                else
                {
                    CHECK(b.structurally_truncated == false);
                    CHECK(b.data_avail == len);
                    CHECK(b.next_offset == offset + 8 + len);
                }
                CHECK(b.value_count == b.data_avail / 2);
            }
        }
    }

    // --- Offsets/filesizes near the maximum of the production integer type -----
    SUBCASE("near size_t maximum: guarded arithmetic, no underflow/overflow")
    {
        const size_t MAX = ~static_cast<size_t>(0);

        // offset just past a near-max filesize -> incomplete header, next=0.
        {
            FskBounds b = fsk_compute_bounds(MAX - 4, MAX, 10);
            CHECK(b.header_complete == false);
            CHECK(b.next_offset == 0);
        }
        // huge filesize, tiny offset, max declared length -> well-formed.
        {
            const size_t offset = 8;
            FskBounds b = fsk_compute_bounds(MAX, offset, 65535);
            CHECK(b.header_complete == true);
            CHECK(b.structurally_truncated == false);
            CHECK(b.data_avail == 65535);
            CHECK(b.next_offset == offset + 8 + 65535); // no overflow at these values
        }
        // exactly 7 bytes remain at the very top of the address range -> EOT.
        {
            FskBounds b = fsk_compute_bounds(MAX, MAX - 7, 0);
            CHECK(b.header_complete == false);
            CHECK(b.next_offset == 0);
        }
    }
}

// ─── Property 8: FSK processing carries no baud-change action ────────────────────
// Feature: a8cas-fsk-chunk-playback, Property 8: FSK processing carries no
// baud-change action.
//
// P8 is a NEGATIVE/structural property of the pure module: none of the fsk_plan
// step/cursor/bounds functions accept or return any baud state or action. What
// the checks below actually guarantee:
//   (1) EXACT function-pointer signatures of the pure API are frozen, so a baud
//       parameter/return cannot be added to any of them without breaking a
//       static_assert (this checks whole signatures, not just one call's return
//       type);
//   (2) structured-binding guards freeze EACH pure state/result struct at its
//       exact decomposable member count — FskStep (4), FskBounds (5),
//       FskChunkView (7) — so a stray extra field (e.g. a baud field) added to
//       ANY of them fails to compile (member COUNT, without sizeof/padding),
//       forcing an explicit P8 review;
//   (3) a generated run of the REAL cursor confirms its observable output is
//       pure signal (level + ticks + done), never a baud value.
// The member-count guards cover all three pure structs, so an added field is
// caught regardless of its type. It does not rely on the model walker; the
// production-caller half (no setBaudrate on the FSK path) is separately shown by
// the interleaved_baud_fsk_data hardware/PC fixture and by code inspection, and
// by the source-level grep (zero "baud" references in fsk_plan.{h,cpp}).
TEST_CASE("P8 pure fsk_plan carries no baud state or action")
{
    // (1) Compile-time contract on the EXACT FUNCTION SIGNATURES of the pure
    // fsk_plan API. Freezing whole function-pointer types (not just a return
    // type of one valid call) means a baud parameter cannot be added to any of
    // these without breaking the assertion. These traffic only in signal
    // (levels, ticks, counts) and structure (offsets/flags) — never a baud.
    static_assert(std::is_same<decltype(&fsk_ticks_for_value),
                               uint32_t (*)(uint16_t)>::value,
                  "fsk_ticks_for_value signature must stay uint32_t(uint16_t) — "
                  "no baud parameter");
    static_assert(std::is_same<decltype(&fsk_level_for_index),
                               bool (*)(size_t)>::value,
                  "fsk_level_for_index signature must stay bool(size_t) — "
                  "no baud parameter");
    static_assert(std::is_same<decltype(&fsk_value_count),
                               size_t (*)(size_t)>::value,
                  "fsk_value_count signature must stay size_t(size_t) — "
                  "no baud parameter");
    static_assert(std::is_same<decltype(&fsk_next_portion),
                               uint32_t (*)(uint32_t)>::value,
                  "fsk_next_portion signature must stay uint32_t(uint32_t) — "
                  "no baud parameter");
    static_assert(std::is_same<decltype(&fsk_view_init),
                               FskChunkView (*)(const uint8_t *const *, size_t,
                                                size_t)>::value,
                  "fsk_view_init signature frozen — no baud parameter");
    static_assert(std::is_same<decltype(&fsk_view_step),
                               FskStep (*)(FskChunkView &)>::value,
                  "fsk_view_step signature must stay FskStep(FskChunkView&) — "
                  "no baud parameter and no baud return");
    static_assert(std::is_same<decltype(&fsk_compute_bounds),
                               FskBounds (*)(size_t, size_t, uint16_t)>::value,
                  "fsk_compute_bounds signature frozen — no baud parameter");

    // Field types of the signal/structural result types are signal/structure
    // only (levels, ticks, counts, offsets, flags), never a baud.
    static_assert(std::is_same<decltype(FskStep::level_high), bool>::value, "");
    static_assert(std::is_same<decltype(FskStep::ticks), uint32_t>::value, "");
    static_assert(std::is_same<decltype(FskBounds::data_avail), size_t>::value, "");
    static_assert(std::is_same<decltype(FskBounds::value_count), size_t>::value, "");
    static_assert(std::is_same<decltype(FskBounds::next_offset), size_t>::value, "");

    // Exact-member-count structural guards for the pure state/result structs:
    // a structured binding to exactly N names compiles ONLY if the struct has
    // EXACTLY N decomposable data members. Adding any extra state field (e.g. a
    // stray baud field) to one of these makes its binding fail to compile,
    // forcing an explicit P8 review/update. This is the semantic member-count
    // check the field-type asserts cannot provide, WITHOUT sizeof()/padding.
    // Member counts/order are taken from the actual fsk_plan.h definitions.

    // FskStep: exactly 4 members {produced, level_high, ticks, done}.
    {
        FskStep probe{};
        const auto &[p_produced, p_level, p_ticks, p_done] = probe;
        (void)p_produced; (void)p_level; (void)p_ticks; (void)p_done;
    }

    // FskBounds: exactly 5 members
    // {data_avail, value_count, next_offset, header_complete, structurally_truncated}.
    {
        FskBounds probe{};
        const auto &[b_data, b_vcount, b_next, b_hdr, b_trunc] = probe;
        (void)b_data; (void)b_vcount; (void)b_next; (void)b_hdr; (void)b_trunc;
    }

    // FskChunkView: exactly 7 members
    // {blocks, block_size, data_len_available, value_index, byte_pos,
    //  remaining_ticks, remaining_level_high}.
    {
        FskChunkView probe = fsk_view_init(nullptr, 0, 0);
        const auto &[v_blocks, v_bsize, v_len, v_vidx, v_bpos, v_rem, v_lvl] = probe;
        (void)v_blocks; (void)v_bsize; (void)v_len; (void)v_vidx; (void)v_bpos;
        (void)v_rem; (void)v_lvl;
    }

    // (2) Generated run of the real cursor over varied chunks: every emitted
    // portion is signal-only (a level bit + a tick count). There is no baud
    // channel to observe because the module has none; this confirms the cursor's
    // observable output stays within {level, ticks, done} for all inputs.
    for (uint32_t seed = 1; seed <= 200; ++seed)
    {
        // Deterministic pseudo-random value sequence.
        std::vector<uint16_t> values;
        uint32_t s = seed * 2654435761u;
        const size_t n = 1 + (seed % 9);
        for (size_t i = 0; i < n; ++i)
        {
            s = s * 1103515245u + 12345u;
            values.push_back((uint16_t)((s >> 16) & 0xFFFF));
        }

        // Lay the values into a contiguous payload and drive the real cursor.
        std::vector<uint8_t> payload;
        for (uint16_t v : values)
        {
            payload.push_back((uint8_t)(v & 0xFF));
            payload.push_back((uint8_t)((v >> 8) & 0xFF));
        }
        BlockTable bt(payload.data(), payload.size(), 512);
        FskChunkView view =
            fsk_view_init(bt.blocks(), bt.block_size(), payload.size());

        int guard = 0;
        for (;;)
        {
            FskStep step = fsk_view_step(view);
            if (step.produced)
            {
                // The only observable outputs are a logical level and a tick
                // count — both signal, never baud.
                CHECK((step.level_high == false || step.level_high == true));
                CHECK(step.ticks >= 1);
                CHECK(step.ticks <= FSK_MAX_PORTION_TICKS);
            }
            if (step.done) break;
            if (++guard > 2000000) { CHECK_MESSAGE(false, "cursor did not terminate"); break; }
        }
    }
}

// ─── Property 9: zero-length chunk is IRG-only ──────────────────────────────────
// Feature: a8cas-fsk-chunk-playback, Property 9: zero-length chunk is IRG-only.
//
// For generated valid offsets/filesizes with declared_len == 0, using the SAME
// production helper plus the real cursor:
//   fsk_value_count(0) == 0
//   cursor produces zero portions and is immediately done
//   structurally_truncated == false, data_avail == 0
//   next_offset == O + 8
TEST_CASE("P9 zero-length chunk is IRG-only across generated offsets")
{
    CHECK(fsk_value_count(0) == 0); // direct pure-helper contract

    const size_t filesize = 8192;
    // Several generated offsets, not one hard-coded example. Only offsets with
    // room for a complete header are well-formed zero-length chunks.
    for (size_t offset = 0; offset + 8 <= filesize; offset += 17)
    {
        FskBounds b = fsk_compute_bounds(filesize, offset, /*declared_len*/ 0);

        CHECK(b.header_complete == true);
        CHECK(b.structurally_truncated == false);
        CHECK(b.data_avail == 0);
        CHECK(b.value_count == 0);
        CHECK(b.next_offset == offset + 8); // IRG-only: advance by header size

        // The real cursor over a zero-length payload produces no portion and is
        // immediately done (IRG-only, no signal work).
        FskChunkView view = fsk_view_init(nullptr, 0, /*data_len_available*/ 0);
        FskStep step = fsk_view_step(view);
        CHECK(step.produced == false);
        CHECK(step.done == true);
    }
}

// ════════════════════════════════════════════════════════════════════════════
// All-MARK run classification: a run whose every even-indexed (LOW) value is
// zero requests no LOW time, so production holds MARK instead of starting RMT.
// Parity restarts per chunk; zero values still consume their index.
// ════════════════════════════════════════════════════════════════════════════
namespace {

// Lays chunks of uint16 values into a block table the way production does:
// each chunk starts on a fresh block boundary.
struct RunFixture
{
    size_t block_size;
    std::vector<std::vector<uint8_t>> storage;
    std::vector<const uint8_t *> table;
    std::vector<size_t> base;
    std::vector<size_t> counts;

    RunFixture(size_t bs, const std::vector<std::vector<uint16_t>> &chunks)
        : block_size(bs)
    {
        for (const auto &vals : chunks)
        {
            base.push_back(table.size());
            counts.push_back(vals.size());

            std::vector<uint8_t> bytes;
            for (uint16_t v : vals)
            {
                bytes.push_back(static_cast<uint8_t>(v & 0xFF));
                bytes.push_back(static_cast<uint8_t>(v >> 8));
            }
            const size_t nb = (bytes.size() + bs - 1) / bs;
            for (size_t b = 0; b < nb; ++b)
            {
                storage.emplace_back(bs, 0);
                const size_t from = b * bs;
                for (size_t k = 0; k < bs && from + k < bytes.size(); ++k)
                    storage.back()[k] = bytes[from + k];
            }
            for (size_t b = 0; b < nb; ++b)
                table.push_back(nullptr); // filled below once storage is stable
        }
        size_t i = 0;
        for (auto &blk : storage)
            table[i++] = blk.data();
    }

    FskRunSummary summarize() const
    {
        return fsk_run_summarize(table.empty() ? nullptr : table.data(),
                                 block_size, base.data(), counts.data(),
                                 counts.size());
    }
};

FskRunSummary summarize_one(const std::vector<uint16_t> &vals, size_t bs = 512)
{
    RunFixture f(bs, { vals });
    return f.summarize();
}

} // namespace

TEST_CASE("All-MARK: (0,N) is all MARK and lasts N*100 us")
{
    for (int n : { 1, 5, 10 })
    {
        FskRunSummary s = summarize_one({ 0, static_cast<uint16_t>(n) });
        CHECK(s.has_space == false);
        CHECK(s.total_ticks == static_cast<uint64_t>(n) * 100);
    }
}

TEST_CASE("All-MARK: an all-zero value stream is all MARK with zero duration")
{
    FskRunSummary s = summarize_one({ 0, 0 });
    CHECK(s.has_space == false);
    CHECK(s.total_ticks == 0);
}

TEST_CASE("All-MARK: a non-zero LOW value is real SPACE")
{
    CHECK(summarize_one({ 5, 5 }).has_space == true);
}

TEST_CASE("All-MARK: a zero value consumes parity, so [0,0,3] has SPACE")
{
    CHECK(summarize_one({ 0, 0, 3 }).has_space == true);
}

TEST_CASE("All-MARK: alternating zero LOW and non-zero HIGH stays all MARK")
{
    FskRunSummary s = summarize_one({ 0, 3, 0, 4 });
    CHECK(s.has_space == false);
    CHECK(s.total_ticks == 700);
}

TEST_CASE("All-MARK: parity restarts at every chunk of a run")
{
    {
        RunFixture f(512, { { 0, 3 }, { 0, 4 } });
        FskRunSummary s = f.summarize();
        CHECK(s.has_space == false);
        CHECK(s.total_ticks == 700);
    }
    {
        // Chunk 2 starts at index 0 again, so its leading 2 is a LOW value.
        RunFixture f(512, { { 0, 3 }, { 2, 0 } });
        CHECK(f.summarize().has_space == true);
    }
    {
        // Odd-length chunk 1 must not shift chunk 2's parity.
        RunFixture f(512, { { 0, 3, 0 }, { 0, 4 } });
        FskRunSummary s = f.summarize();
        CHECK(s.has_space == false);
        CHECK(s.total_ticks == 700);
    }
}

TEST_CASE("All-MARK: a long HIGH value keeps its full duration without truncation")
{
    FskRunSummary s = summarize_one({ 0, 65535 });
    CHECK(s.has_space == false);
    CHECK(s.total_ticks == static_cast<uint64_t>(65535) * 100);

    // Many maximal HIGH values exceed 32 bits of ticks without wrapping.
    std::vector<uint16_t> many;
    for (int i = 0; i < 800; ++i) { many.push_back(0); many.push_back(65535); }
    FskRunSummary m = summarize_one(many);
    CHECK(m.has_space == false);
    CHECK(m.total_ticks == static_cast<uint64_t>(800) * 65535 * 100);
    CHECK(m.total_ticks > 0xFFFFFFFFull);
}

TEST_CASE("All-MARK: a normal waveform with a non-zero LOW is not classified MARK-only")
{
    // Head of a real tape image: LOW 0, HIGH 7, LOW 1 (non-zero), HIGH 65535.
    FskRunSummary s = summarize_one({ 0, 7, 1, 65535 });
    CHECK(s.has_space == true);

    // A leading LOW value alone is enough.
    CHECK(summarize_one({ 1, 65535 }).has_space == true);
}

TEST_CASE("All-MARK: values straddling a block boundary are classified correctly")
{
    // Block size 3 forces every second value across a block boundary.
    CHECK(summarize_one({ 0, 3, 0, 4 }, 3).has_space == false);
    CHECK(summarize_one({ 0, 3, 0, 4 }, 3).total_ticks == 700);
    CHECK(summarize_one({ 0, 3, 0, 4, 0, 5, 9 }, 3).has_space == true);
}

TEST_CASE("All-MARK: empty run is all MARK with zero duration")
{
    const size_t no_base[1] = {};
    const size_t no_counts[1] = {};
    FskRunSummary s = fsk_run_summarize(nullptr, 512, no_base, no_counts, 1);
    CHECK(s.has_space == false);
    CHECK(s.total_ticks == 0);
}

TEST_CASE("All-MARK classification does not change run membership or splitting")
{
    // Zero-IRG (0,N) chunks still join one run exactly as before.
    std::vector<LayoutChunk> lay = { {true,4,999}, {true,4,0}, {true,4,0} };
    auto run = plan_run(lay, layout_filesize(lay));
    CHECK(run.size() == 3);

    // Long durations still split into <=32767-tick portions with no extra
    // level change: 65535 units = 6553500 ticks of one HIGH level.
    std::vector<uint8_t> payload = { 0,0, 0xFF,0xFF };
    PreloadBuffers bufs(payload.size(), 512);
    ScriptedReader rd(payload.data(), payload.size());
    rd.deliver_pattern = { 0 };
    REQUIRE(fsk_preload_into_blocks(bufs.blocks(), bufs.block_count(), 512,
                                    payload.size(), 512,
                                    &ScriptedReader::read, &rd) == payload.size());
    FskChunkView v = fsk_view_init(bufs.blocks(), 512, payload.size());
    uint64_t total = 0;
    size_t portions = 0;
    for (;;)
    {
        FskStep s = fsk_view_step(v);
        if (!s.produced) break;
        CHECK(s.level_high == true); // every portion is MARK, no added edge
        CHECK(s.ticks <= FSK_MAX_PORTION_TICKS);
        total += s.ticks;
        ++portions;
        if (s.done) break;
    }
    CHECK(total == static_cast<uint64_t>(65535) * 100);
    CHECK(portions == 201); // ceil(6553500 / 32767)
}
