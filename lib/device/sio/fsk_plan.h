#ifndef FSK_PLAN_H
#define FSK_PLAN_H

// fsk_plan.h — pure, host-buildable A8CAS FSK parsing/timing rules.
//
// No I/O, no hardware, no FujiNet globals, no allocation, no logging.
// The static-inline rule helpers below are trivial/inlinable arithmetic and
// IRAM-safe so the production RMT encoder callback (fsk_encode_cb) can inline
// the SAME rules the host tests exercise, keeping production and tests from
// diverging on the modeled duration.
//
// `fsk_view_step` is declared here for host tests but is implemented in
// fsk_plan.cpp; it is intentionally NOT static inline and NOT called from the
// ISR.

#include <cstdint>
#include <cstddef>

// Force-inline the pure rule helpers that the production IRAM RMT callback
// (fsk_encode_cb) may call. Plain `static inline` is only a hint; the compiler
// may still emit an out-of-line, flash-resident copy, which is unsafe to call
// from an IRAM/ISR context. `always_inline` guarantees the body is inlined at
// each call site so nothing flash-resident is invoked from the ISR path. Stays
// host-buildable: no ESP-IDF dependency and no IRAM_ATTR in this pure module.
#if defined(__GNUC__) || defined(__clang__)
#define FSK_FORCE_INLINE static inline __attribute__((always_inline))
#else
#define FSK_FORCE_INLINE static inline
#endif

// The RMT 15-bit per-level tick limit (max ticks in one rmt_symbol duration field).
static constexpr uint32_t FSK_MAX_PORTION_TICKS = 32767;

// 1 us per RMT tick (1 MHz); one A8CAS unit = 1/10 ms = 100 us = 100 ticks.
static constexpr uint32_t FSK_RMT_TICKS_PER_A8CAS_UNIT = 100;

// Scale one A8CAS value (in 1/10 ms units) to RMT ticks at the 1 us grid.
FSK_FORCE_INLINE uint32_t fsk_ticks_for_value(uint16_t value)
{
    return (uint32_t)value * FSK_RMT_TICKS_PER_A8CAS_UNIT;
}

// Decode one little-endian uint16 duration from the two bytes at p (Req 2.1).
// The caller guarantees p and p+1 are inside the available data region.
FSK_FORCE_INLINE uint16_t fsk_decode_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

// Logical level for a value by ORIGINAL A8CAS value index parity (Req 2.5/4.2):
// even index = logical 0 (returns false), odd index = logical 1 (returns true).
FSK_FORCE_INLINE bool fsk_level_for_index(size_t value_index)
{
    return (value_index & 1) != 0;
}

// One split step (pure state transition, NOT a materialized list). Ticks for a
// value V are V * 100 (1/10 ms = 100 us = 100 ticks at 1 us/tick). Given the
// ticks remaining for the current value, return the next portion to emit
// (min(remaining, 32767)); the caller subtracts it to get the new remaining.
// remaining==0 returns 0 (nothing to emit) (Req 4.1, 4.6).
FSK_FORCE_INLINE uint32_t fsk_next_portion(uint32_t remaining_ticks)
{
    return remaining_ticks > FSK_MAX_PORTION_TICKS ? FSK_MAX_PORTION_TICKS
                                                   : remaining_ticks;
}

// Number of FSK_Signal_Values available: floor(data_len_available / 2) (Req 2.3/6.4).
FSK_FORCE_INLINE size_t fsk_value_count(size_t data_len_available)
{
    return data_len_available / 2;
}

// A small pure cursor over the pre-read byte buffer. Holds O(1) state; allocates
// nothing; never reads outside [0, data_len_available). Host tests advance this
// cursor with fsk_view_step. Production carries equivalent O(1) state but does
// not call fsk_view_step; it uses the same static-inline decode/parity/scale/
// split rules.
struct FskChunkView
{
    const uint8_t *data;               // pre-read data region (null iff len==0)
    size_t   data_len_available;       // clamped bytes present (caller-bounded)
    size_t   value_index;              // 0-based FSK_Signal_Index of current value
    size_t   byte_pos;                 // next byte to read (== value_index*2)
    uint32_t remaining_ticks;          // ticks left in the current value being split (value*100)
    bool     remaining_level_high;     // level of the value currently being split
};

// Initialize a cursor at the start of the buffer.
FSK_FORCE_INLINE FskChunkView fsk_view_init(const uint8_t *data, size_t data_len_available)
{
    return FskChunkView{ data, data_len_available, 0, 0, 0, false };
}

// Result of one cursor step.
struct FskStep
{
    bool     produced;   // true if this step yields a portion to emit
    bool     level_high; // level of the produced portion
    uint32_t ticks;      // portion ticks (1..32767) when produced
    bool     done;       // true once no more portions/values remain
};

// Advance the cursor by exactly one emitted portion. If the current value still
// has remaining ticks, emit the next portion of it (splitting on the fly). Else
// load the next value: skip any leading zero-duration values (they consume an
// index and thus parity but emit nothing), then emit their first portion. Sets
// done when all value_count values are exhausted. Reads only within
// [0, data_len_available). HOST-TEST-ONLY: defined in fsk_plan.cpp, NOT static
// inline, NOT called from the ISR, and NOT in IRAM. It uses the same static-
// inline pure RULES (fsk_decode_le16 / fsk_level_for_index / fsk_ticks_for_value /
// fsk_next_portion) that the production IRAM callback fsk_encode_cb inlines, so
// both share logic.
FskStep fsk_view_step(FskChunkView &view);

#endif // FSK_PLAN_H
