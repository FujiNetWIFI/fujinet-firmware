#ifndef CASSETTE_TIME_PLAN_H
#define CASSETTE_TIME_PLAN_H

// cassette_time_plan.h — pure, host-buildable real-duration model + chunk
// boundary walker for A8CAS/FUJI and legacy raw CAS files.
//
// No filesystem, RMT/GPIO, FujiNet globals, allocation, or logging — same
// discipline as fsk_plan.h/.cpp. Answers "how much real time has this tape
// played by file offset X" and its inverse, using only on-disk fields and
// the same integer formulas production uses to drive SIO/RMT. Does not
// decide Custom Rewind policy itself.
//
// All accumulation is uint64_t; no float/double anywhere in this module.

#include <cstddef>
#include <cstdint>

// Format state that must survive a reposition. Field types match the real
// sioCassette members exactly, so a caller can copy this struct into the
// live object with zero narrowing.
struct CassetteWalkState
{
    size_t         offset;           // a real chunk-header offset (FUJI) or a
                                      // real legacy block boundary, or filesize at EOT
    uint64_t       time_us;          // accumulated duration from offset 0 to `offset`
    unsigned short baud;             // == sioCassette::baud
    uint16_t       t2k_samplerate;   // == sioCassette::t2k_samplerate
    uint16_t       t2k_bit0_half;    // == sioCassette::t2k_bit0_half
    uint16_t       t2k_bit1_half;    // == sioCassette::t2k_bit1_half
    uint16_t       t2k_pilot_half;   // == sioCassette::t2k_pilot_half
    uint16_t       t2k_pilot_count;  // == sioCassette::t2k_pilot_count
    uint16_t       qros_turbo_baud;  // == sioCassette::qros_turbo_baud
};

// Injected, POSITIONAL reader: reads up to `n` bytes at absolute file offset
// `offset`, returns bytes actually read (0 on EOF/failure). Positional so
// this module never depends on a stateful file cursor.
using cas_time_read_fn = size_t (*)(void *ctx, size_t offset, uint8_t *dst, size_t n);

// Duration of `byte_count` bytes at `baud` bps, standard async 8N1 framing
// (10 bits/byte). Floor division, so not bit-exact to hardware, but used
// identically in both walk directions so the walker stays self-consistent.
// Returns 0 if baud == 0 rather than dividing by zero.
uint64_t cas_bits_duration_us(size_t byte_count, unsigned short baud);

// Duration of one `data`/QROS-data-style record: exact IRG (ms) plus the
// async byte transmission above.
uint64_t cas_data_record_duration_us(uint16_t irg_length_ms, uint16_t chunk_length,
                                     unsigned short baud);

// Legacy raw/SDrive block duration (mirrors send_tape_block()): 132 bytes on
// the wire at 600 baud plus a fixed 300ms post-block delay. Constant
// regardless of content, including the terminal 0xFE record.
uint64_t cas_legacy_block_duration_us();

// Sums the exact FSK payload duration for `data_avail` bytes at
// `payload_offset`, reusing fsk_plan.h's exact per-value tick arithmetic (no
// division, so zero rounding error). Reads in small fixed chunks. Returns
// false on a short/failed read.
bool cas_fsk_payload_duration_us(cas_time_read_fn reader, void *ctx,
                                 size_t payload_offset, size_t data_avail,
                                 uint64_t &out_us);

// Turbo 2000 PWM duration helpers — best-effort reading of
// send_turbo2000_tape_block(); no real .cas corpus with `pwm*` chunks was
// available to validate against hardware, so these are exercised only by
// scalar tests, not the real-CAS suite. All integer, no float.
//
// pwmc: the explicit silence_ms wait; the pilot tone itself is generated
// later, folded into the following pwml transmission.
uint64_t cas_pwmc_duration_us(uint16_t silence_ms);

// pwml: its own silence_ms wait, plus raw sync symbol pairs (already
// half-periods, not halved again), plus the pilot tone deferred from pwmc.
uint64_t cas_pwml_duration_us(uint16_t silence_ms, const uint8_t *sync_data, size_t sync_len,
                              uint16_t samplerate, uint16_t pilot_count, uint16_t pilot_half_us);

// pwmd: sum over every bit of `data` of its bit0/bit1 half-period * 2, using
// the LOCAL half-periods from this chunk's own aux field.
uint64_t cas_pwmd_duration_us(const uint8_t *data, size_t data_len,
                              uint16_t bit0_half_us, uint16_t bit1_half_us);

// Raw sample count -> half-period in microseconds (mirrors
// sioCassette::t2k_samples_to_us). Returns 0 if samplerate == 0.
uint16_t cas_t2k_samples_to_us(uint8_t samples, uint16_t samplerate);

// The walker. Walks chunk headers (or, for a non-FUJI file, fixed 128-byte
// legacy blocks) from offset 0 using only the injected reader, accumulating
// exact real duration and format state. Format-agnostic by construction: it
// interprets each chunk's own type tag rather than consulting a cached
// classification, so Turbo 2000/QROS chunks walk like any other chunk type.
// A joined FSK run needs no special grouping — continuation chunks carry
// irg==0, so summing chunk-by-chunk already yields the correct total.
//
// Stops before the first boundary that would cross either stop condition
// (stop_at_offset==SIZE_MAX or stop_at_time_us==UINT64_MAX disables that
// condition). `out` always names a real boundary <= both conditions — never
// overshoots; lands on the START of the containing chunk.
//
// Returns false only on a structural failure (filesize==0, or an
// incomplete/malformed first chunk header) — never fabricates a result.
bool cas_walk_tape_time(size_t filesize, cas_time_read_fn reader, void *ctx,
                        size_t stop_at_offset, uint64_t stop_at_time_us,
                        CassetteWalkState &out);

// -----------------------------------------------------------------------------
// Run-relative position (R, Q): the MOTOR-pause / paused-rewind timeline
// -----------------------------------------------------------------------------
//
// A raw-FSK position is stored as R (header offset of the run's first chunk) and
// Q, microseconds from cas_walk_tape_time's time at R — i.e. BEFORE R's own
// leading IRG. One scalar covers both the IRG and the waveform:
//   Q <  irg_us  -> inside the leading IRG (irg_us - Q of it remains)
//   Q == irg_us  -> exactly at the waveform start
//   Q >  irg_us  -> inside the waveform, Q - irg_us ticks after its first value
struct CasRunPosSplit
{
    bool     in_irg;            // Q is strictly inside the leading IRG
    uint64_t irg_remaining_us;  // irg_us - Q while in_irg, else 0
    uint64_t wave_ticks;        // Q - irg_us once the waveform has started, else 0
};

CasRunPosSplit cas_run_pos_split(uint64_t irg_us, uint64_t q_us);

// Accessor over a resident run: returns the raw on-file FSK value at
// `value_index` within run chunk `chunk_index` (both 0-based, relative to the
// CURRENT run). Caller guarantees value_index < value_counts[chunk_index].
using fsk_run_value_fn = uint16_t (*)(void *ctx, size_t chunk_index, size_t value_index);

// Where `ticks` (waveform ticks from the run's first value) falls inside a
// resident run. chunk_index/value_index are chunk-local (parity comes from the
// value index); skip_ticks is how far into that value the position is, so a
// resume lands inside a long value without replaying it. Zero-duration values
// are stepped over (they consume a parity slot but no time). `at_end` means
// `ticks` is at or beyond the run's total duration.
struct FskLocateResult
{
    size_t   chunk_index;
    size_t   value_index;
    uint64_t skip_ticks;
    bool     at_end;
};

FskLocateResult cas_fsk_locate_ticks(const size_t *value_counts, size_t chunk_count,
                                     fsk_run_value_fn value_reader, void *ctx,
                                     uint64_t ticks);

// How much of the waveform, from its first value, carries no data: MARK (HIGH)
// except for at most `low_budget_ticks` of LOW time in total. The scan stops as
// soon as a LOW value would exceed the budget (that is where data may start), or
// once `limit_ticks` has been passed (the caller needs no more), so its cost is
// bounded by the values before the first real signal. Uses the same value
// accessor and parity rule (even index LOW, odd HIGH) as cas_fsk_locate_ticks.
// The result is the waveform tick where the inert part ends (>= limit_ticks if
// the scan was cut by the limit; the run's total ticks if it is inert throughout).
uint64_t cas_fsk_inert_ticks(const size_t *value_counts, size_t chunk_count,
                             fsk_run_value_fn value_reader, void *ctx,
                             uint64_t limit_ticks, uint64_t low_budget_ticks);

// Resolves an absolute target CAS time to the run-relative position (R', Q') the
// paused-rewind / MOTOR-pause model stores. R' is the start of the chunk that
// contains the target (the walker's boundary); Q' = target - that chunk's start
// time, so a target inside a chunk's own IRG yields Q' < irg automatically.
// `walk` carries the format state committed with the new offset. `is_fsk` is
// false for any non-`fsk ` boundary (coarse reposition, Q' unused).
struct CassetteTargetResolution
{
    CassetteWalkState walk;
    bool              is_fsk;
    uint16_t          irg_ms;
    uint64_t          q_us;
};

bool cas_resolve_target_time(size_t filesize, cas_time_read_fn reader, void *ctx,
                             uint64_t target_us, CassetteTargetResolution &out);

#endif // CASSETTE_TIME_PLAN_H
