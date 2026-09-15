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

// Active FSK Rewind — pure resident-run target resolution. Resolves a target
// absolute CAS time to a position within one already-resident contiguous
// zero-IRG FSK run, via an injected value accessor — same no-I/O discipline
// as cas_walk_tape_time() above. Production supplies a reader over the
// resident PSRAM block table; host tests read an in-memory array decoded
// from a real .cas payload.
//
// Accessor: returns the raw on-file FSK value at value index `value_index`
// within run chunk `chunk_index` (both 0-based, relative to the CURRENT
// run). Caller guarantees value_index < run_value_counts[chunk_index].
using fsk_run_value_fn = uint16_t (*)(void *ctx, size_t chunk_index, size_t value_index);

struct FskActiveRewindResolution
{
    // false => target_us is before this run entirely (run_chunk_count==0 or
    // target_us < run_start_time_us); caller must fall back to
    // cas_walk_tape_time(). chunk_index/value_index are meaningless here.
    bool resolved;

    // true => target_us fell inside the run's own leading IRG: resolve to
    // chunk 0 from scratch (replay the full IRG), not a resume — the caller
    // must not treat value_index==0 here as "resume at value 0".
    bool inside_leading_irg;

    // Valid only when resolved && !inside_leading_irg: the run chunk/value
    // whose START time is the greatest <= target_us — never mid-value.
    // chunk_index==0 && value_index==0 needs no resume flag either (same as
    // entering fresh); anything else requires a genuine resume.
    size_t chunk_index;
    size_t value_index;
};

// Pure resolver; never resumes mid-value — the returned value's start time
// is always <= target_us, and the next value's (if any) is always >
// target_us. run_chunk_count==0 always yields resolved==false.
FskActiveRewindResolution cas_fsk_resolve_active_rewind(
    const size_t *run_value_counts, size_t run_chunk_count,
    fsk_run_value_fn value_reader, void *ctx,
    uint64_t run_start_time_us, uint64_t leading_irg_us, uint64_t target_us);

#endif // CASSETTE_TIME_PLAN_H
