#ifndef CAS_FSK_H
#define CAS_FSK_H

// casFSK.h — pure, host-buildable A8CAS `fsk ` chunk interpretation. No
// ESP-IDF, no RMT, no file ownership, no allocation policy, no logging —
// same discipline as casTape.h. Basic playback only: this module answers
// "where does this run end and how long does its waveform take", not
// anything about progressive loading, MOTOR-pause/resume, or rewind.
//
// An A8CAS "fsk " chunk's payload is a sequence of little-endian uint16
// "values", each a duration in 1/10 ms units. A run is one `fsk ` chunk plus
// every immediately following `fsk ` chunk whose irg_length == 0 (a
// continuation of the same waveform, not a fresh gap-then-tone); this is the
// only join rule defined here, and the only one basic playback needs.

#include <cstddef>
#include <cstdint>

// RMT runs at 1 MHz: 1 tick = 1 us. One A8CAS FSK unit is 1/10 ms = 100 us.
constexpr uint32_t FSK_TICKS_PER_UNIT = 100;

// Max duration one RMT level-duration field can hold (15 bits).
constexpr uint32_t FSK_MAX_PORTION_TICKS = 32767;

// Safety cap on how many chunks one zero-IRG run may join. The largest real
// run found in the A8CAS corpus used during this work was 8 chunks; this
// leaves wide headroom while still bounding fsk_scan_run's work. A run
// longer than this is treated as ending here (see fsk_scan_run's comment).
constexpr size_t FSK_RUN_MAX_CHUNKS = 64;

// Recognizes the 4-byte `fsk ` chunk-type tag (not null-terminated, not a
// C string: compared byte-by-byte like every other A8CAS chunk tag in this
// codebase).
constexpr bool fsk_is_chunk_type(const uint8_t type[4])
{
    return type[0] == 'f' && type[1] == 's' && type[2] == 'k' && type[3] == ' ';
}

// Decodes one little-endian uint16 from a contiguous two-byte pair. Caller
// guarantees p[0] and p[1] exist.
constexpr uint16_t fsk_decode_le16(const uint8_t *p)
{
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

// Logical output level follows A8CAS signal-value index parity: even index
// -> logical 0 (space), odd index -> logical 1 (mark).
constexpr bool fsk_level_for_index(size_t value_index)
{
    return (value_index & 1u) != 0;
}

// Scales one A8CAS duration value to the 1 MHz RMT tick grid.
constexpr uint32_t fsk_ticks_for_value(uint16_t value)
{
    return static_cast<uint32_t>(value) * FSK_TICKS_PER_UNIT;
}

// Returns the next RMT-sized portion of a duration. remaining_ticks == 0
// yields no portion (0).
constexpr uint32_t fsk_next_portion(uint32_t remaining_ticks)
{
    return remaining_ticks > FSK_MAX_PORTION_TICKS ? FSK_MAX_PORTION_TICKS : remaining_ticks;
}

// Non-owning, positional reader: reads up to `n` bytes at absolute file
// offset `offset` into `dst`, returns the number of bytes actually read (0
// on EOF/failure). Positional so this module never depends on a stateful
// file cursor. `ctx` is caller-defined (e.g. an fnFile*, or a test fixture's
// in-memory buffer).
using FSKReadFn = size_t (*)(void *ctx, size_t offset, uint8_t *dst, size_t n);

// Structural description of one zero-IRG-joined `fsk ` run, as found by
// fsk_scan_run. Header-only: payload bytes are counted, not read.
struct FSKRunInfo
{
    size_t   chunk_count    = 0; // number of `fsk ` chunks joined into this run (>= 1)
    size_t   payload_bytes  = 0; // sum of chunk_length over the run
    size_t   value_count    = 0; // payload_bytes / 2
    size_t   next_offset    = 0; // file offset immediately after the run
    uint16_t leading_irg_ms = 0; // the run's first chunk's irg_length (the gap before the waveform starts)
};

// Scans the zero-IRG-joined run of `fsk ` chunks starting at `header_offset`
// (which must itself be an `fsk ` chunk header — callers already know this,
// having just matched fsk_is_chunk_type on it). Reads chunk headers only via
// `read`, not payload data. Stops joining further chunks at the first
// non-`fsk ` chunk, the first `fsk ` chunk whose irg_length != 0 (a fresh
// run, not a continuation), the end of the file, or after FSK_RUN_MAX_CHUNKS
// chunks — a run longer than that is reported as ending there, and the
// caller will naturally start a new (irg==0) run immediately afterward, with
// no gap inserted since the remaining chunks still carry irg==0.
//
// Returns false — and leaves `out` default-constructed — if `header_offset`
// is not itself a well-formed `fsk ` chunk header, or if any chunk header in
// the run is truncated, malformed, or claims a payload extending past
// `filesize`, or an odd chunk_length (an `fsk ` chunk's payload must be a
// whole number of 2-byte values).
bool fsk_scan_run(FSKReadFn read, void *ctx, size_t filesize, size_t header_offset,
                   FSKRunInfo &out);

// Sums the exact waveform duration, in RMT ticks (1 tick == 1 us), of
// `value_count` little-endian uint16 values stored back to back in
// `payload`. A zero-duration value contributes no ticks but still consumes
// a parity slot, matching fsk_level_for_index — the same rule the RMT
// encoder callback applies while playing the same buffer.
uint64_t fsk_sum_waveform_ticks(const uint8_t *payload, size_t value_count);

#endif // CAS_FSK_H
