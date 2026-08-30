#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "sio/fsk_plan.h"

#include <array>
#include <cstdint>
#include <cstddef>
#include <vector>

// FskPlanTests.cpp — doctest coverage for the pure A8CAS FSK rules in
// lib/device/sio/fsk_plan.{h,cpp}. These tests exercise the pure decode/parity/
// scale/split helpers and drive the host-only fsk_view_step cursor. They touch
// no hardware, I/O, or globals.
//
// Requirements traced here: 2.7 (worked example), 2.8 (empty payload),
// 4.6 (zero-duration parity + split), 6.4 (odd-length / floor pairing).
//
// NOTE: This file is registered into the build under task 2.4. Later tasks
// (2.2 generated-loop property tests, 2.3 synthetic fixtures) add more sections
// to this same file, so the TEST_CASE blocks below are kept self-contained.

namespace
{
// Build a little-endian 2-byte buffer for a single uint16 value.
inline std::array<uint8_t, 2> le16_buf(uint16_t value)
{
    return { (uint8_t)(value & 0xFF), (uint8_t)((value >> 8) & 0xFF) };
}

// Result of running a FskChunkView to exhaustion.
struct DrainResult
{
    size_t   portion_count;   // number of produced portions
    uint64_t total_ticks;     // sum of all produced portion ticks
    bool     all_same_level;  // every produced portion carried the same level
    bool     first_level;     // level of the first produced portion (if any)
    uint32_t max_portion;     // largest produced portion (0 if none produced)
};

// Drive a cursor over `data`/`len` until fsk_view_step reports done, collecting
// aggregate statistics. Per the approved contract, the LAST emitted portion may
// carry produced=true AND done=true in the SAME call, so a produced portion is
// always accounted for before honoring done.
DrainResult drain(const uint8_t *data, size_t len)
{
    FskChunkView view = fsk_view_init(data, len);

    DrainResult r{ 0, 0, true, false, 0 };
    bool have_first = false;

    for (;;)
    {
        FskStep step = fsk_view_step(view);

        // Account for a produced portion first — the terminal step may both
        // produce the final portion and report done in one call.
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
    // Data area bytes for the worked example. 10 bytes => 5 LE uint16 values.
    const uint8_t data[] = {
        0x00, 0x01, // index 0: 0x0100 = 256  -> 25.6 ms -> logical 0 (even)
        0x10, 0x01, // index 1: 0x0110 = 272  -> 27.2 ms -> logical 1 (odd)
        0x80, 0x00, // index 2: 0x0080 = 128  -> 12.8 ms -> logical 0
        0x20, 0x00, // index 3: 0x0020 = 32   ->  3.2 ms -> logical 1
        0x80, 0x02  // index 4: 0x0280 = 640  -> 64.0 ms -> logical 0
    };
    const size_t len = sizeof(data);

    // Ten bytes yield exactly five values (floor of len/2).
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
        CHECK(fsk_level_for_index(0) == false); // logical 0
        CHECK(fsk_level_for_index(1) == true);  // logical 1
        CHECK(fsk_level_for_index(2) == false); // logical 0
        CHECK(fsk_level_for_index(3) == true);  // logical 1
        CHECK(fsk_level_for_index(4) == false); // logical 0
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
        // Each value maps to `value * 100` ticks. The first four fit in a single
        // portion (<= 32767 ticks), but index 4 (640 -> 64,000 ticks) exceeds
        // FSK_MAX_PORTION_TICKS and is split across two same-level portions
        // (32767 + 31233 = 64000). Drive the whole buffer, grouping portions by
        // value, and confirm each value's total ticks and level.
        struct Expect { bool level_high; uint32_t total_ticks; };
        const Expect expected[] = {
            { false, 25600 }, // index 0: 256 -> one portion
            { true,  27200 }, // index 1: 272 -> one portion
            { false, 12800 }, // index 2: 128 -> one portion
            { true,   3200 }, // index 3: 32  -> one portion
            { false, 64000 }, // index 4: 640 -> split into two portions
        };

        FskChunkView view = fsk_view_init(data, len);

        const size_t last = (sizeof(expected) / sizeof(expected[0])) - 1;
        bool saw_done = false;

        for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i)
        {
            const Expect &e = expected[i];

            // Consume all portions that belong to this value: the value's ticks
            // remain "in flight" (view.remaining_ticks) until fully emitted.
            uint32_t accumulated = 0;
            do
            {
                FskStep step = fsk_view_step(view);
                REQUIRE(step.produced);
                CHECK(step.level_high == e.level_high);
                CHECK(step.ticks <= FSK_MAX_PORTION_TICKS);
                accumulated += step.ticks;

                // Approved contract: done becomes true on the LAST emitted
                // portion (last portion of the last value) in the SAME call.
                // Every earlier produced portion has more work remaining.
                if (step.done)
                {
                    saw_done = true;
                }
                else
                {
                    // Not-done is only valid mid-value or before the last value.
                    bool more_work_expected = (view.remaining_ticks > 0) || (i < last);
                    CHECK(more_work_expected);
                }
            } while (view.remaining_ticks > 0);

            CHECK(accumulated == e.total_ticks);
        }

        // The final emitted portion already carried done=true; no extra
        // non-producing terminal call is required.
        CHECK(saw_done);
    }
}

// ─── Requirement 2.8: zero-length payload ───────────────────────────────────────

TEST_CASE("Req 2.8 empty payload produces nothing and is immediately done")
{
    CHECK(fsk_value_count(0) == 0);

    FskChunkView view = fsk_view_init(nullptr, 0);
    FskStep step = fsk_view_step(view);
    CHECK_FALSE(step.produced);
    CHECK(step.done);
}

// ─── Requirement 6.4: odd-length payload handling ───────────────────────────────

TEST_CASE("Req 6.4 odd-length payload ignores the trailing unpaired byte")
{
    // Five bytes: two complete LE pairs plus one dangling byte that must never
    // be read. If the trailing byte were read, the sentinel 0xEE below would
    // corrupt a decoded value; the test proves it is ignored.
    const uint8_t data[] = {
        0x0A, 0x00, // index 0: 10  -> logical 0
        0x14, 0x00, // index 1: 20  -> logical 1
        0xEE        // trailing unpaired byte — must be ignored
    };
    const size_t len = sizeof(data);

    // Floor pairing: 5 bytes -> 2 values.
    CHECK(fsk_value_count(len) == 2);

    FskChunkView view = fsk_view_init(data, len);

    FskStep s0 = fsk_view_step(view);
    REQUIRE(s0.produced);
    CHECK_FALSE(s0.done);               // a second value still remains
    CHECK(s0.level_high == false);      // index 0 even -> logical 0
    CHECK(s0.ticks == fsk_ticks_for_value(10)); // 1000

    // The second (last) value's single portion carries done=true in the SAME
    // call per the approved contract; no extra non-producing terminal call.
    FskStep s1 = fsk_view_step(view);
    REQUIRE(s1.produced);
    CHECK(s1.done);                     // last portion of the last value
    CHECK(s1.level_high == true);       // index 1 odd -> logical 1
    CHECK(s1.ticks == fsk_ticks_for_value(20)); // 2000

    // The cursor stopped after two values, before the trailing byte: the
    // unpaired byte at index 4 (0xEE) was never read.
    CHECK(view.byte_pos == 4);
    CHECK(view.byte_pos < len);

    // A further step past exhaustion is a non-producing done.
    FskStep past_end = fsk_view_step(view);
    CHECK_FALSE(past_end.produced);
    CHECK(past_end.done);
}

// ─── Requirement 4.6: zero-duration values preserve parity ──────────────────────

TEST_CASE("Req 4.6 zero-duration values consume a parity slot without emitting")
{
    SUBCASE("a middle zero is skipped but keeps later parity aligned")
    {
        // Values [100, 0, 200]. The zero at index 1 emits nothing, but the
        // value at index 2 must still be logical 0 (even), proving the zero
        // consumed a parity slot rather than shifting parity onto the next
        // value.
        const uint8_t data[] = {
            0x64, 0x00, // index 0: 100 -> logical 0
            0x00, 0x00, // index 1: 0   -> skipped, still consumes parity slot
            0xC8, 0x00  // index 2: 200 -> logical 0 (even)
        };
        const size_t len = sizeof(data);

        CHECK(fsk_value_count(len) == 3);

        FskChunkView view = fsk_view_init(data, len);

        FskStep first = fsk_view_step(view);
        REQUIRE(first.produced);
        CHECK_FALSE(first.done);                          // value 200 still remains
        CHECK(first.level_high == false);                 // index 0 even
        CHECK(first.ticks == fsk_ticks_for_value(100));   // 10000

        // The zero at index 1 is skipped internally; the next produced portion
        // is value 200 at index 2, which must remain logical 0 (even parity).
        // It is the last value, so its single portion carries done=true in the
        // SAME call per the approved contract.
        FskStep second = fsk_view_step(view);
        REQUIRE(second.produced);
        CHECK(second.done);                               // last portion of last value
        CHECK(second.level_high == false);                // index 2 even, NOT odd
        CHECK(second.ticks == fsk_ticks_for_value(200));  // 20000

        // A further step past exhaustion is a non-producing done.
        FskStep past_end = fsk_view_step(view);
        CHECK_FALSE(past_end.produced);
        CHECK(past_end.done);
    }

    SUBCASE("a leading zero keeps the next value at logical 1")
    {
        // Values [0, 300]. Index 0 is zero (skipped, consumes parity), so the
        // first produced portion is index 1 -> logical 1.
        const uint8_t data[] = {
            0x00, 0x00, // index 0: 0   -> skipped
            0x2C, 0x01  // index 1: 300 -> logical 1 (odd)
        };
        const size_t len = sizeof(data);

        CHECK(fsk_value_count(len) == 2);

        FskChunkView view = fsk_view_init(data, len);

        // Index 1 (value 300) is the only and last value emitted, so its
        // single portion carries done=true in the SAME call.
        FskStep first = fsk_view_step(view);
        REQUIRE(first.produced);
        CHECK(first.done);                                // last portion of last value
        CHECK(first.level_high == true);                  // index 1 odd
        CHECK(first.ticks == fsk_ticks_for_value(300));   // 30000

        // A further step past exhaustion is a non-producing done.
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

    // Portion count = ceil(ticks / 32767); the final portion carries the
    // remainder. All portions except possibly the last are exactly 32767.
    const ValueCase cases[] = {
        { 1,        100, 1 },     // 1 -> 100 ticks -> 1 portion
        { 256,    25600, 1 },     // 256 -> 25,600 ticks -> 1 portion
        { 6818,  681800, 21 },    // 6818 -> 681,800 ticks -> 21 portions
        { 40000, 4000000, 123 },  // 40000 -> 4,000,000 ticks -> 123 portions
        { 65535, 6553500, 201 },  // 65535 -> 6,553,500 ticks -> 201 portions
    };

    for (const ValueCase &c : cases)
    {
        CAPTURE(c.value);

        // Pure scaling helper.
        CHECK(fsk_ticks_for_value(c.value) == c.expected_ticks);

        // Drive the cursor over a single-value buffer and count portions.
        DrainResult r = drain_value(c.value);

        CHECK(r.portion_count == c.expected_portions);
        CHECK(r.total_ticks == c.expected_ticks);   // duration preserved
        CHECK(r.max_portion <= FSK_MAX_PORTION_TICKS);
        CHECK(r.all_same_level);                     // one value -> one level
        CHECK(r.first_level == false);               // index 0 even -> logical 0
    }

    SUBCASE("6818 explicitly maps to 681,800 ticks over 21 portions")
    {
        DrainResult r = drain_value(6818);
        CHECK(r.total_ticks == 681800);
        CHECK(r.portion_count == 21);
        CHECK(r.max_portion == FSK_MAX_PORTION_TICKS); // at least one full portion
    }

    SUBCASE("65535 explicitly maps to 6,553,500 ticks over 201 portions")
    {
        // 200 portions of 32767 (= 6,553,400) plus a final 100-tick portion.
        DrainResult r = drain_value(65535);
        CHECK(r.total_ticks == 6553500);
        CHECK(r.portion_count == 201);
        CHECK(r.max_portion == FSK_MAX_PORTION_TICKS);
        CHECK(200u * FSK_MAX_PORTION_TICKS + 100u == 6553500u); // arithmetic sanity
    }
}

// ════════════════════════════════════════════════════════════════════════════
// Task 2.2 — Deterministic generated-loop tests for the correctness properties.
//
// These are deterministic (loop/enumeration driven — NO randomness): every input
// is either fully enumerated over the uint16 range or drawn from a fixed set.
// They reuse the anonymous-namespace helpers above (le16_buf, DrainResult, drain,
// drain_value) and add a few small local helpers where a per-portion view is
// needed that the aggregate `drain` cannot provide.
//
// Properties (traced to Requirements 2.1, 2.3, 2.4, 2.5, 4.1, 4.6, 6.2, 6.4):
//   P1  fsk_value_count(len) == len / 2
//   P2  summed emitted portion ticks == fsk_ticks_for_value(decoded value)
//   P3  every emitted portion level follows original value-index parity
//   P4  cursor byte reads stay within the available payload
//   P5  portions <= FSK_MAX_PORTION_TICKS, preserve total duration, correct count
// ════════════════════════════════════════════════════════════════════════════

namespace
{
// Expected portion count for a single value V using pure integer arithmetic:
// 0 portions when V == 0, else ceil(V * 100 / 32767). Computed independently of
// the module under test so the test is a real cross-check, not a tautology.
inline size_t expected_portion_count(uint16_t value)
{
    uint32_t ticks = (uint32_t)value * FSK_RMT_TICKS_PER_A8CAS_UNIT; // V * 100
    if (ticks == 0)
        return 0;
    return (size_t)((ticks + (FSK_MAX_PORTION_TICKS - 1)) / FSK_MAX_PORTION_TICKS);
}

// One emitted portion recorded while draining, with the ORIGINAL value index it
// belongs to. Used by Property 3 to check per-portion parity, and by Property 4
// to observe byte_pos monotonicity/bounds at every step.
struct PortionRecord
{
    size_t   value_index; // original FSK_Signal_Index the portion came from
    bool     level_high;  // emitted level of the portion
    uint32_t ticks;       // portion ticks
    size_t   byte_pos;    // view.byte_pos observed immediately after the step
};

// Drain a payload, recording one PortionRecord per emitted portion and grouping
// portions under the original value index they belong to. A value's index is the
// cursor value_index minus one after the value has been loaded (the cursor
// advances value_index when it loads a value; every portion of that value shares
// the same "just-loaded" index). We reconstruct the owning index by tracking the
// low-water value_index for each contiguous run of same-remaining portions.
//
// The reconstruction rule mirrors the cursor contract exactly:
//  - When a step begins a NEW value, view.value_index has already advanced past
//    it, so the owning index is (value_index - 1).
//  - When a step continues splitting the SAME value (the previous step left
//    remaining_ticks > 0), the owning index is unchanged from the previous step.
// We detect "new value" by observing that the previous step left no remainder.
std::vector<PortionRecord> drain_portions(const uint8_t *data, size_t len,
                                           size_t &final_byte_pos, bool &saw_done)
{
    FskChunkView view = fsk_view_init(data, len);

    std::vector<PortionRecord> out;
    saw_done = false;

    bool   prev_left_remainder = false; // did the previous produced step leave ticks?
    size_t current_owner_index = 0;

    for (;;)
    {
        FskStep step = fsk_view_step(view);

        if (step.produced)
        {
            if (!prev_left_remainder)
            {
                // This portion begins a freshly-loaded value; the cursor already
                // advanced value_index past it, so the owner is value_index - 1.
                current_owner_index = view.value_index - 1;
            }
            // else: continuation of the same value; owner index unchanged.

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
    // Small exhaustive prefix (0..512) catches every odd/even boundary, then a
    // deterministic stride sample up to a few thousand bytes.
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

    // Explicit boundary cases named in the task.
    CHECK(fsk_value_count(0) == 0);
    CHECK(fsk_value_count(1) == 0);
    CHECK(fsk_value_count(2) == 1);
    CHECK(fsk_value_count(3) == 1);
}

// ─── Property 5: pure split arithmetic across the FULL uint16 range ─────────────

TEST_CASE("P5 pure split arithmetic holds for every uint16 value 0..65535")
{
    // The split arithmetic (tick scaling + portion count) is cheap, so enumerate
    // the ENTIRE uint16 range here as the task requests ("full uint16 value range
    // for split arithmetic where practical"). This validates fsk_ticks_for_value
    // and the ceil-based portion count without draining a cursor per value.
    for (uint32_t v = 0; v <= 0xFFFF; ++v)
    {
        uint16_t value = (uint16_t)v;
        uint32_t ticks = fsk_ticks_for_value(value);

        // Tick scaling is exactly V * 100.
        REQUIRE(ticks == (uint32_t)value * 100u);

        // Portion count matches the independent ceil computation.
        size_t expected = expected_portion_count(value);

        // Reconstruct the per-portion sequence purely from fsk_next_portion to
        // confirm: each portion <= max, all portions sum to `ticks`, and the
        // count equals `expected`. This mirrors the production split loop.
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

    // Explicit boundary/tick-edge values called out by the task.
    CHECK(expected_portion_count(0) == 0);
    CHECK(expected_portion_count(1) == 1);      // 100 ticks
    CHECK(expected_portion_count(327) == 1);    // 32,700 <= 32767 -> 1 portion
    CHECK(expected_portion_count(328) == 2);    // 32,800 > 32767  -> 2 portions
    CHECK(expected_portion_count(6818) == 21);  // 681,800 -> 21 portions
    CHECK(expected_portion_count(65535) == 201);// 6,553,500 -> 201 portions
}

// ─── Property 2 + Property 5 (cursor): per-value drain over a dense value set ───

TEST_CASE("P2/P5 single-value drain preserves duration and portion count")
{
    // Draining a cursor per value is more expensive than the pure arithmetic
    // above, so use a dense-but-bounded deterministic set rather than all 65536:
    //  - every value 0..1024 (covers the 327/328 tick boundary and small counts)
    //  - a fixed stride across the rest of the range
    //  - the explicit boundary values named in the task.
    std::vector<uint32_t> values;
    for (uint32_t v = 0; v <= 1024; ++v)
        values.push_back(v);
    for (uint32_t v = 1025; v <= 0xFFFF; v += 337) // deterministic stride
        values.push_back(v);
    for (uint32_t v : { 327u, 328u, 6818u, 32767u, 40000u, 65535u })
        values.push_back(v);

    for (uint32_t v : values)
    {
        uint16_t value = (uint16_t)v;
        CAPTURE(value);

        std::array<uint8_t, 2> buf = le16_buf(value);

        // Sanity: decode round-trips through the LE pair.
        REQUIRE(fsk_decode_le16(buf.data()) == value);

        DrainResult r = drain(buf.data(), buf.size());

        // P2: summed emitted portion ticks == fsk_ticks_for_value(decoded).
        CHECK(r.total_ticks == fsk_ticks_for_value(value));

        // P5: every portion within the 15-bit cap, and the portion count matches
        // the independent ceil computation (0 for a zero-duration value).
        CHECK(r.max_portion <= FSK_MAX_PORTION_TICKS);
        CHECK(r.portion_count == expected_portion_count(value));

        // A single value yields a single logical level.
        CHECK(r.all_same_level);

        // Index 0 is even -> logical 0. (Zero-duration values produce no portion,
        // so first_level stays at its default false, which also matches.)
        CHECK(r.first_level == false);
    }
}

// ─── Property 3: per-portion level follows ORIGINAL value-index parity ──────────

TEST_CASE("P3 every emitted portion level follows original value-index parity")
{
    // A deterministic multi-value payload with zeros interspersed. Zero-duration
    // values emit nothing but MUST still consume a parity slot, so the parity of
    // every following value stays aligned to its original index. Values are
    // chosen so several of them split into multiple portions (all portions of a
    // value must share that value's index parity).
    const uint16_t values[] = {
        100,   // index 0 even -> logical 0, 1 portion
        0,     // index 1 odd  -> zero: emits nothing, still consumes parity
        400,   // index 2 even -> logical 0, 2 portions (40000 ticks)
        6818,  // index 3 odd  -> logical 1, 21 portions
        0,     // index 4 even -> zero: emits nothing, still consumes parity
        0,     // index 5 odd  -> zero: emits nothing, still consumes parity
        500,   // index 6 even -> logical 0, 2 portions (50000 ticks)
        1,     // index 7 odd  -> logical 1, 1 portion
        65535, // index 8 even -> logical 0, 201 portions
    };
    const size_t n = sizeof(values) / sizeof(values[0]);

    // Build the LE payload deterministically.
    std::vector<uint8_t> data;
    data.reserve(n * 2);
    for (size_t i = 0; i < n; ++i)
    {
        std::array<uint8_t, 2> pair = le16_buf(values[i]);
        data.push_back(pair[0]);
        data.push_back(pair[1]);
    }

    CHECK(fsk_value_count(data.size()) == n);

    size_t final_byte_pos = 0;
    bool   saw_done = false;
    std::vector<PortionRecord> portions =
        drain_portions(data.data(), data.size(), final_byte_pos, saw_done);

    CHECK(saw_done);

    // Every portion's level equals the parity of the ORIGINAL value index it came
    // from, and each portion is attributed to a non-zero-duration value.
    for (const PortionRecord &p : portions)
    {
        CAPTURE(p.value_index);
        REQUIRE(p.value_index < n);
        CHECK(values[p.value_index] != 0);                       // zeros emit nothing
        CHECK(p.level_high == fsk_level_for_index(p.value_index));
    }

    // Group ticks per original index and confirm each non-zero value's total
    // ticks and portion count, proving zeros neither emit nor shift parity.
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

    // Spot-check the parity expectation directly against the payload layout:
    // even original index -> logical 0, odd -> logical 1, regardless of splits
    // or interspersed zeros.
    bool saw_index_2 = false, saw_index_3 = false, saw_index_8 = false;
    for (const PortionRecord &p : portions)
    {
        if (p.value_index == 2) { saw_index_2 = true; CHECK(p.level_high == false); }
        if (p.value_index == 3) { saw_index_3 = true; CHECK(p.level_high == true);  }
        if (p.value_index == 8) { saw_index_8 = true; CHECK(p.level_high == false); }
    }
    CHECK(saw_index_2);
    CHECK(saw_index_3);
    CHECK(saw_index_8);
}

// ─── Property 4: cursor byte reads stay within the available payload ────────────

TEST_CASE("P4 cursor byte_pos stays in bounds and ends at value_count*2")
{
    // Exercise a deterministic set of payload lengths, including odd tails, zero,
    // and single-byte cases. For each, build a repeatable byte pattern, drain it,
    // and assert byte_pos never exceeds the available length at any step and lands
    // exactly on value_count*2 at the end (the trailing odd byte is never read).
    const size_t lengths[] = { 0, 1, 2, 3, 4, 5, 8, 9, 16, 17, 31, 64, 65, 100, 101, 256, 257, 400 };

    for (size_t len : lengths)
    {
        CAPTURE(len);

        // Deterministic, non-zero-biased byte pattern so most values are non-zero
        // (a purely zero payload would emit no portions and still be valid, but we
        // want portions to actually flow to exercise the split path too).
        std::vector<uint8_t> data(len);
        for (size_t i = 0; i < len; ++i)
            data[i] = (uint8_t)((i * 37 + 1) & 0xFF);

        const size_t value_count = fsk_value_count(len);

        // Step the cursor manually so byte_pos can be observed after EVERY step,
        // proving no internal read ever addressed a byte at or beyond `len`.
        FskChunkView view = fsk_view_init(len ? data.data() : nullptr, len);

        size_t guard = 0;
        const size_t guard_max = value_count * 210 + 16; // generous upper bound
        for (;;)
        {
            // Precondition for a legal 2-byte read on this step: byte_pos+1 < len.
            // The cursor must never leave byte_pos in a state where it would read
            // out of bounds on the next value load.
            CHECK(view.byte_pos <= len);
            CHECK(view.byte_pos <= value_count * 2);

            FskStep step = fsk_view_step(view);

            // After any step, byte_pos must remain within the payload and on an
            // even, value-aligned boundary.
            CHECK(view.byte_pos <= len);
            CHECK(view.byte_pos % 2 == 0);
            CHECK(view.byte_pos <= value_count * 2);

            if (step.done)
                break;

            REQUIRE(++guard < guard_max); // deterministic runaway guard
        }

        // The cursor consumed exactly the complete-pair region; any trailing odd
        // byte was never read.
        CHECK(view.byte_pos == value_count * 2);
        CHECK(view.byte_pos <= len);
    }
}

// ════════════════════════════════════════════════════════════════════════════
// Task 2.3 — Synthetic FSK payload fixtures.
//
// Reusable, named, in-source byte-array fixtures for the categories the task
// calls out: SHORT, LONG, ZERO, and ODD-TAIL. They are built entirely in source
// as raw generic FSK data areas — NO copyrighted `.cas` files, NO filenames, NO
// game/country/title-specific bytes. Per Requirements 9.4 and 9.8, every check
// below derives behavior ONLY from the generic chunk contents (the decoded
// values and their index parity), never from a fixture's name or identity: the
// fixture names are test-local labels, and the assertions would hold for ANY
// payload with the same bytes regardless of what it is called.
//
// These fixtures reuse the anonymous-namespace helpers defined at the top of the
// file (le16_buf, DrainResult, drain, drain_value) rather than re-implementing
// them, and the pure fsk_plan.h rules for their expectations. They neither
// weaken nor duplicate the 2.1 example tests or the 2.2 property tests; they add
// a distinct, category-oriented fixture layer with one consuming TEST_CASE per
// category driven through the cursor.
// ════════════════════════════════════════════════════════════════════════════

namespace fsk_fixtures
{
// Append one FSK_Signal_Value as a little-endian pair onto a byte vector.
inline void push_le16(std::vector<uint8_t> &out, uint16_t value)
{
    std::array<uint8_t, 2> pair = le16_buf(value);
    out.push_back(pair[0]);
    out.push_back(pair[1]);
}

// Build a generic FSK data area from an ordered list of values. This is the
// only "builder" the fixtures need; the payload is exactly the concatenation of
// LE pairs, matching how an A8CAS `fsk ` chunk data area is laid out.
inline std::vector<uint8_t> build_payload(std::initializer_list<uint16_t> values)
{
    std::vector<uint8_t> out;
    out.reserve(values.size() * 2);
    for (uint16_t v : values)
        push_le16(out, v);
    return out;
}

// ── SHORT fixture ───────────────────────────────────────────────────────────
// A small handful of ordinary, single-portion values. Chosen so each value's
// ticks (value * 100) stay well under FSK_MAX_PORTION_TICKS, so every value maps
// to exactly one portion. Generic durations, no special meaning.
inline std::vector<uint8_t> short_payload()
{
    // 4 values -> 8 bytes, all single-portion (<= 327 keeps ticks <= 32700).
    return build_payload({ 10, 25, 100, 200 });
}

// ── LONG fixture ──────────────────────────────────────────────────────────────
// Contains at least one value that splits into MANY same-level portions. 65535
// -> 6,553,500 ticks -> 201 portions. Surrounded by a couple of ordinary values
// so the long value sits at a known index (parity) inside a multi-value payload.
inline std::vector<uint8_t> long_payload()
{
    // index 0: 50    (1 portion, logical 0)
    // index 1: 65535 (201 portions, logical 1)  <- the long/splitting value
    // index 2: 300   (1 portion, logical 0)
    return build_payload({ 50, 65535, 300 });
}

// ── ZERO fixtures ─────────────────────────────────────────────────────────────
// Two distinct "zero" shapes the task calls out:
//   (a) an EMPTY payload (no bytes at all), and
//   (b) a payload CONTAINING zero-duration values interspersed with real ones.
// The empty payload is simply an empty byte vector.
inline std::vector<uint8_t> empty_payload()
{
    return std::vector<uint8_t>{};
}

// Zero-duration values at various indices. The zeros emit nothing but each still
// consumes a parity slot, so the non-zero values keep their original-index
// parity. Layout:
//   index 0: 0   (zero, logical 0 slot, emits nothing)
//   index 1: 150 (logical 1)
//   index 2: 0   (zero, logical 0 slot, emits nothing)
//   index 3: 0   (zero, logical 1 slot, emits nothing)
//   index 4: 250 (logical 0)
inline std::vector<uint8_t> zero_duration_payload()
{
    return build_payload({ 0, 150, 0, 0, 250 });
}

// ── ODD-TAIL fixture ────────────────────────────────────────────────────────
// A payload whose byte length is ODD, so the final unpaired byte must be ignored
// (floor pairing). Two complete values plus one trailing sentinel byte that must
// never be read. The sentinel is a distinctive value so that, if it were ever
// paired/decoded, the resulting value would be far outside the two real values.
inline std::vector<uint8_t> odd_tail_payload()
{
    std::vector<uint8_t> out = build_payload({ 40, 80 }); // 2 values -> 4 bytes
    out.push_back(0xEE);                                  // trailing unpaired byte
    return out;                                           // total length 5 (odd)
}
} // namespace fsk_fixtures

// ─── Req 9.4/9.8: SHORT fixture is a valid generic payload ──────────────────────

TEST_CASE("Fixture(short): a small handful of single-portion values drains cleanly")
{
    std::vector<uint8_t> data = fsk_fixtures::short_payload();

    // Behavior comes only from the bytes: 4 values, each one portion.
    REQUIRE(fsk_value_count(data.size()) == 4);

    const uint16_t values[] = { 10, 25, 100, 200 };

    DrainResult r = drain(data.data(), data.size());

    // Every value is single-portion, so portion_count == value_count and total
    // ticks == sum of per-value ticks derived purely from the values.
    uint64_t expected_ticks = 0;
    for (uint16_t v : values)
        expected_ticks += fsk_ticks_for_value(v);

    CHECK(r.portion_count == 4);
    CHECK(r.total_ticks == expected_ticks);
    CHECK(r.max_portion <= FSK_MAX_PORTION_TICKS);
    CHECK(r.first_level == false); // index 0 even -> logical 0

    // Per-value confirmation through a fresh cursor: value/level/ticks all from
    // the payload contents and index parity, never from the fixture's name.
    FskChunkView view = fsk_view_init(data.data(), data.size());
    for (size_t i = 0; i < 4; ++i)
    {
        CAPTURE(i);
        FskStep step = fsk_view_step(view);
        REQUIRE(step.produced);
        CHECK(step.level_high == fsk_level_for_index(i));
        CHECK(step.ticks == fsk_ticks_for_value(values[i]));
        // done is true only on the final value's single portion.
        CHECK(step.done == (i == 3));
    }
}

// ─── Req 9.4/9.8: LONG fixture splits its big value into many portions ──────────

TEST_CASE("Fixture(long): a value that splits into many same-level portions")
{
    std::vector<uint8_t> data = fsk_fixtures::long_payload();

    REQUIRE(fsk_value_count(data.size()) == 3);

    // 65535 -> 6,553,500 ticks -> 201 portions; the two neighbors are 1 portion
    // each. Total portions = 1 + 201 + 1 = 203.
    DrainResult r = drain(data.data(), data.size());
    CHECK(r.portion_count == 203);
    CHECK(r.max_portion == FSK_MAX_PORTION_TICKS); // the long value hits the cap
    CHECK(r.total_ticks ==
          fsk_ticks_for_value(50) + fsk_ticks_for_value(65535) + fsk_ticks_for_value(300));

    // Walk the cursor and confirm the long value (index 1, odd -> logical 1)
    // emits exactly 201 same-level portions summing to its full duration, while
    // its neighbors emit one portion each. All expectations come from the bytes.
    FskChunkView view = fsk_view_init(data.data(), data.size());

    // index 0: value 50, single portion, logical 0.
    FskStep s0 = fsk_view_step(view);
    REQUIRE(s0.produced);
    CHECK(s0.level_high == false);
    CHECK(s0.ticks == fsk_ticks_for_value(50));
    CHECK_FALSE(s0.done);

    // index 1: value 65535, 201 portions, all logical 1, capped at max except
    // the final remainder portion.
    size_t   long_portions = 0;
    uint64_t long_ticks    = 0;
    for (;;)
    {
        FskStep s = fsk_view_step(view);
        REQUIRE(s.produced);
        CHECK(s.level_high == true); // index 1 odd -> logical 1 for every portion
        CHECK(s.ticks <= FSK_MAX_PORTION_TICKS);
        ++long_portions;
        long_ticks += s.ticks;
        CHECK_FALSE(s.done); // index 2 still remains after the long value
        if (view.remaining_ticks == 0)
            break;
    }
    CHECK(long_portions == 201);
    CHECK(long_ticks == fsk_ticks_for_value(65535));

    // index 2: value 300, single portion, logical 0, and it is the last value so
    // its single portion carries done in the same call.
    FskStep s2 = fsk_view_step(view);
    REQUIRE(s2.produced);
    CHECK(s2.level_high == false);
    CHECK(s2.ticks == fsk_ticks_for_value(300));
    CHECK(s2.done);
}

// ─── Req 9.4/9.8: ZERO fixtures — empty payload and zero-duration values ────────

TEST_CASE("Fixture(zero): empty payload and interspersed zero-duration values")
{
    SUBCASE("empty payload produces nothing and is immediately done")
    {
        std::vector<uint8_t> data = fsk_fixtures::empty_payload();
        CHECK(data.empty());
        CHECK(fsk_value_count(data.size()) == 0);

        DrainResult r = drain(data.empty() ? nullptr : data.data(), data.size());
        CHECK(r.portion_count == 0);
        CHECK(r.total_ticks == 0);
    }

    SUBCASE("zero-duration values emit nothing but keep later parity aligned")
    {
        std::vector<uint8_t> data = fsk_fixtures::zero_duration_payload();

        // 5 values: [0, 150, 0, 0, 250]. Only indices 1 and 4 emit.
        REQUIRE(fsk_value_count(data.size()) == 5);

        DrainResult r = drain(data.data(), data.size());

        // Only the two non-zero values emit a (single) portion each.
        CHECK(r.portion_count == 2);
        CHECK(r.total_ticks == fsk_ticks_for_value(150) + fsk_ticks_for_value(250));

        // The first emitted portion is value 150 at index 1 -> logical 1.
        CHECK(r.first_level == true);

        // Confirm the parity of each emitted value derives from its ORIGINAL
        // index (1 odd -> logical 1, 4 even -> logical 0), proving the zeros
        // consumed parity slots without shifting the survivors.
        FskChunkView view = fsk_view_init(data.data(), data.size());

        FskStep first = fsk_view_step(view);
        REQUIRE(first.produced);
        CHECK(first.level_high == fsk_level_for_index(1)); // true
        CHECK(first.ticks == fsk_ticks_for_value(150));
        CHECK_FALSE(first.done); // value 250 still remains

        FskStep second = fsk_view_step(view);
        REQUIRE(second.produced);
        CHECK(second.level_high == fsk_level_for_index(4)); // false
        CHECK(second.ticks == fsk_ticks_for_value(250));
        CHECK(second.done); // last emitting value

        // A further step past exhaustion is a non-producing done.
        FskStep past_end = fsk_view_step(view);
        CHECK_FALSE(past_end.produced);
        CHECK(past_end.done);
    }
}

// ─── Req 9.4/9.8: ODD-TAIL fixture ignores the trailing unpaired byte ───────────

TEST_CASE("Fixture(odd-tail): odd-length payload ignores the trailing unpaired byte")
{
    std::vector<uint8_t> data = fsk_fixtures::odd_tail_payload();

    // Length is odd; floor pairing yields 2 complete values.
    CHECK(data.size() % 2 == 1);
    REQUIRE(fsk_value_count(data.size()) == 2);

    const uint16_t values[] = { 40, 80 };

    // Drain: exactly two single portions, and the cursor stops before the tail.
    DrainResult r = drain(data.data(), data.size());
    CHECK(r.portion_count == 2);
    CHECK(r.total_ticks == fsk_ticks_for_value(40) + fsk_ticks_for_value(80));

    FskChunkView view = fsk_view_init(data.data(), data.size());

    FskStep s0 = fsk_view_step(view);
    REQUIRE(s0.produced);
    CHECK(s0.level_high == fsk_level_for_index(0)); // false
    CHECK(s0.ticks == fsk_ticks_for_value(values[0]));
    CHECK_FALSE(s0.done);

    FskStep s1 = fsk_view_step(view);
    REQUIRE(s1.produced);
    CHECK(s1.level_high == fsk_level_for_index(1)); // true
    CHECK(s1.ticks == fsk_ticks_for_value(values[1]));
    CHECK(s1.done);

    // The cursor consumed exactly the two complete pairs (4 bytes); the trailing
    // unpaired byte at index 4 was never read.
    CHECK(view.byte_pos == 4);
    CHECK(view.byte_pos < data.size());

    // A further step past exhaustion is a non-producing done.
    FskStep past_end = fsk_view_step(view);
    CHECK_FALSE(past_end.produced);
    CHECK(past_end.done);
}
