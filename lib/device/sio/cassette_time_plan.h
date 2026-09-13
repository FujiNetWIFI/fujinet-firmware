#ifndef CASSETTE_TIME_PLAN_H
#define CASSETTE_TIME_PLAN_H

// cassette_time_plan.h — pure, host-buildable real-duration model + chunk
// boundary walker for A8CAS/FUJI and legacy raw CAS files.
//
// No filesystem, no RMT/GPIO, no FujiNet globals, no allocation, no logging
// — the same discipline as fsk_plan.h/.cpp, so this is fully testable
// without ESP-IDF or hardware. This module does not decide the CUSTOM
// REWIND policy; it only answers "how much real time has this tape played
// by file offset X" and its inverse, using exclusively fields already
// present on disk (chunk_length, irg_length/aux, baud) and the same integer
// formulas production already uses to drive SIO/RMT (see fsk_plan.h for the
// FSK case, and cassette.cpp's send_FUJI_tape_block / send_turbo2000_tape_block
// / send_QROS_tape_block / send_tape_block for the data/baud/pwm*/legacy
// cases this module mirrors).
//
// All accumulation is uint64_t. No float/double anywhere in this module.

#include <cstddef>
#include <cstdint>

// Format state that must survive a reposition. Field types match the real
// sioCassette members exactly (see cassette.h) so a caller can copy this
// struct into the live object with zero narrowing.
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

// Injected, POSITIONAL reader: reads up to `n` bytes starting at absolute
// file offset `offset` into `dst`, returns the number of bytes actually
// read (0 on EOF/failure). Positional (an explicit offset every call, not a
// sequential cursor) so this module never depends on, or perturbs, any
// stateful file cursor the caller may also be using elsewhere.
using cas_time_read_fn = size_t (*)(void *ctx, size_t offset, uint8_t *dst, size_t n);

// Exact duration of `byte_count` bytes sent at `baud` bits per second using
// standard async 8N1 framing (1 start + 8 data + 1 stop = 10 bits/byte) —
// the real convention SYSTEM_BUS.write()/setBaudrate() already drives.
// Floor (truncating integer) division: 10*1'000'000/baud is rarely an exact
// integer, so this is NOT bit-exact to real hardware timing, but it is used
// identically in both walk directions (OFFSET->TIME and TIME->OFFSET), so
// the walker stays internally self-consistent. Returns 0 if baud == 0
// (malformed `baud` chunk) rather than dividing by zero.
uint64_t cas_bits_duration_us(size_t byte_count, unsigned short baud);

// Duration of one `data`/QROS-data-style record: an exact IRG (milliseconds,
// no rounding) plus the async byte transmission above.
uint64_t cas_data_record_duration_us(uint16_t irg_length_ms, uint16_t chunk_length,
                                     unsigned short baud);

// Legacy raw / SDrive block duration (mirrors send_tape_block() exactly):
// 132 bytes actually placed on the wire (131-byte record: 2 sync + 1 marker
// + up to 128 data, via ONE write of BLOCK_LEN+3 bytes, plus a SEPARATE
// 1-byte checksum write) at the fixed CASSETTE_BAUDRATE (600 baud), plus a
// fixed 300ms post-block delay (fnSystem.delay(300)). Constant regardless of
// content — every legacy block, including the terminal 0xFE record, costs
// exactly this.
uint64_t cas_legacy_block_duration_us();

// Sums the exact FSK payload duration for `data_avail` bytes starting at
// absolute file offset `payload_offset`, via the injected reader, reusing
// fsk_plan.h's exact primitives (fsk_decode_le16 + fsk_ticks_for_value) — the
// SAME per-value tick arithmetic the production RMT ISR uses, so this has
// ZERO rounding error (no division anywhere: 1 A8CAS unit == 100 exact
// microseconds). Reads the payload in small fixed-size chunks (never the
// whole 65535-byte max at once) via the reader. Returns false on a short or
// failed read.
bool cas_fsk_payload_duration_us(cas_time_read_fn reader, void *ctx,
                                 size_t payload_offset, size_t data_avail,
                                 uint64_t &out_us);

// Turbo 2000 PWM duration helpers (GAP: no real .cas corpus with `pwm*`
// chunks was available to validate these against actual hardware timing —
// implemented as a faithful best-effort reading of send_turbo2000_tape_block(),
// exercised only by scalar/unit tests, never by the mandatory real-CAS test
// suite). All integer, no float.
//
// pwmc: only the explicit `silence_ms` wait is deterministic/file-encoded;
// the pilot tone itself is generated later, folded into the following pwml
// transmission (see cas_pwml_duration_us), matching production's "deferred
// to encoder" pilot generation.
uint64_t cas_pwmc_duration_us(uint16_t silence_ms);

// pwml: its own optional silence_ms wait, plus its raw sync symbol pairs
// (`sync_data`, `sync_len` bytes, 4 bytes per symbol = two 16-bit sample
// counts converted to microseconds via `samplerate`, NOT halved — matches
// production's "already half-periods" comment), plus the pilot tone deferred
// from the preceding pwmc (`pilot_count` pulses at `pilot_half_us` half-period,
// full period = pilot_half_us*2).
uint64_t cas_pwml_duration_us(uint16_t silence_ms, const uint8_t *sync_data, size_t sync_len,
                              uint16_t samplerate, uint16_t pilot_count, uint16_t pilot_half_us);

// pwmd: sum over every bit of `data` (`data_len` bytes, MSB/LSB order does
// not affect the sum) of (bit ? bit1_half_us : bit0_half_us) * 2 (full
// period per bit), using the LOCAL bit0/bit1 half-periods derived from this
// chunk's own aux field (matches production: pwmd's own header, not a
// carried-forward value, sets the bit halves for its own payload).
uint64_t cas_pwmd_duration_us(const uint8_t *data, size_t data_len,
                              uint16_t bit0_half_us, uint16_t bit1_half_us);

// Converts a raw sample count to a half-period in microseconds at the given
// sample rate (mirrors sioCassette::t2k_samples_to_us exactly: samples are a
// full period, so half-period = samples * 1'000'000 / samplerate / 2).
// Returns 0 if samplerate == 0.
uint16_t cas_t2k_samples_to_us(uint8_t samples, uint16_t samplerate);

// -----------------------------------------------------------------------------
// The walker.
// -----------------------------------------------------------------------------
//
// Walks chunk headers (FUJI: `data`/`baud`/`fsk `/`pwms`/`pwmc`/`pwml`/`pwmd`/
// anything else skipped as 0-duration) or, for a non-FUJI file, fixed
// 128-byte legacy blocks, from file offset 0 in ascending order, using ONLY
// the injected reader (never a stateful cursor), accumulating exact real
// duration and format state. This is format-agnostic by construction: it
// does not consult any cached tape_flags classification, it interprets each
// chunk's own 4-byte type tag as it is encountered — so it is never
// accidentally limited to FUJI/A8CAS the way a flags-dispatch walker could
// be (Turbo 2000 and QROS chunks live inside the same FUJI container and are
// walked exactly like any other chunk type).
//
// No FSK "run" grouping is needed here: a continuation chunk of a joined
// FSK run always carries irg==0 by the run-join contract itself, so summing
// chunk-by-chunk already yields the correct total.
//
// Stops BEFORE the first boundary that would cross either stop condition:
//   stop_at_offset  == SIZE_MAX   -> ignored (TIME->OFFSET mode)
//   stop_at_time_us == UINT64_MAX -> ignored (OFFSET->TIME mode)
// `out` always names a real, existing boundary with out.time_us <=
// stop_at_time_us (when enabled) and out.offset <= stop_at_offset (when
// enabled) — the target is never overshot (Decision: land on the START of
// the containing chunk, never advance past it).
//
// Returns false only on a structural failure (filesize == 0, or a FUJI file
// whose very first chunk header is incomplete/malformed) — never fabricates
// a result.
bool cas_walk_tape_time(size_t filesize, cas_time_read_fn reader, void *ctx,
                        size_t stop_at_offset, uint64_t stop_at_time_us,
                        CassetteWalkState &out);

#endif // CASSETTE_TIME_PLAN_H
