#ifndef FSK_PLAN_H
#define FSK_PLAN_H

// fsk_plan.h — pure, host-buildable A8CAS FSK parsing/timing rules.
//
// No filesystem, no RMT/GPIO, no FujiNet globals, no allocation, no logging.
//
// The small rule helpers used by the production RMT ISR callback live here so
// host tests and production share exactly the same decode/parity/timing/split
// logic.
//
// The revised V2 design stores a preloaded FSK payload as a table of fixed-size
// blocks rather than as one large contiguous allocation. The pure block-table
// accessors below expose that logical payload without knowing anything about
// ESP-IDF or the filesystem.
//
// fsk_view_step and fsk_preload_into_blocks are implemented in fsk_plan.cpp and
// remain hardware/filesystem independent.

#include <cstddef>
#include <cstdint>

// Force-inline helpers that may be called from the IRAM RMT callback.
// This module deliberately has no ESP-IDF dependency and therefore does not use
// IRAM_ATTR directly.
#if defined(__GNUC__) || defined(__clang__)
#define FSK_FORCE_INLINE static inline __attribute__((always_inline))
#else
#define FSK_FORCE_INLINE static inline
#endif

// Maximum duration accepted by one RMT level-duration field (15 bits).
static constexpr uint32_t FSK_MAX_PORTION_TICKS = 32767;

// RMT runs at 1 MHz: 1 tick = 1 us.
// One A8CAS FSK unit is 1/10 ms = 100 us = exactly 100 RMT ticks.
static constexpr uint32_t FSK_RMT_TICKS_PER_A8CAS_UNIT = 100;

// Scale one A8CAS duration value to the exact 1 MHz RMT tick grid.
FSK_FORCE_INLINE uint32_t fsk_ticks_for_value(uint16_t value)
{
    return static_cast<uint32_t>(value) *
           FSK_RMT_TICKS_PER_A8CAS_UNIT;
}

// Decode one little-endian uint16 from a contiguous two-byte pair.
// Caller guarantees p[0] and p[1] exist.
FSK_FORCE_INLINE uint16_t fsk_decode_le16(const uint8_t *p)
{
    return static_cast<uint16_t>(p[0]) |
           (static_cast<uint16_t>(p[1]) << 8);
}

// Logical level follows ORIGINAL A8CAS signal-value index parity:
// even index -> logical 0
// odd  index -> logical 1
FSK_FORCE_INLINE bool fsk_level_for_index(size_t value_index)
{
    return (value_index & 1U) != 0;
}

// Return the next RMT-sized portion of a duration.
// remaining_ticks == 0 produces no portion.
FSK_FORCE_INLINE uint32_t fsk_next_portion(uint32_t remaining_ticks)
{
    return remaining_ticks > FSK_MAX_PORTION_TICKS
               ? FSK_MAX_PORTION_TICKS
               : remaining_ticks;
}

// Number of complete uint16 FSK values present in a byte region.
// Any trailing odd byte is intentionally excluded.
FSK_FORCE_INLINE size_t fsk_value_count(size_t data_len_available)
{
    return data_len_available / 2;
}

// -----------------------------------------------------------------------------
// Segmented payload logical-byte access
// -----------------------------------------------------------------------------
//
// `blocks` points to a contiguous table of block pointers.
// Every normal block has `block_size` bytes. The final block may be only partly
// logically used; bounds are governed by the caller's logical payload length.
//
// These helpers perform no bounds checks themselves because the production ISR
// path must remain tiny and deterministic. Callers guarantee that requested
// logical positions are valid.

// Return logical payload byte k from the segmented block table.
// Preconditions:
//   blocks != nullptr
//   block_size > 0
//   k is inside the caller's logical payload length
FSK_FORCE_INLINE uint8_t fsk_block_byte(const uint8_t *const *blocks,
                                        size_t block_size,
                                        size_t k)
{
    return blocks[k / block_size][k % block_size];
}

// Decode a little-endian FSK value from logical positions k and k+1.
//
// Fetching the two bytes independently is intentional: if k is the last byte of
// one preload block, k+1 may reside in the following block.
//
// Preconditions:
//   blocks != nullptr
//   block_size > 0
//   k and k+1 are inside the caller's logical payload length
FSK_FORCE_INLINE uint16_t fsk_block_le16(const uint8_t *const *blocks,
                                         size_t block_size,
                                         size_t k)
{
    const uint16_t lo =
        static_cast<uint16_t>(fsk_block_byte(blocks, block_size, k));
    const uint16_t hi =
        static_cast<uint16_t>(fsk_block_byte(blocks, block_size, k + 1));

    return lo | (hi << 8);
}

// -----------------------------------------------------------------------------
// Host-testable bounded preload helper
// -----------------------------------------------------------------------------

// Injected reader used by fsk_preload_into_blocks.
//
// The reader attempts to place up to `n` bytes into `dst` and returns the number
// actually delivered.
//
// A positive short return is valid and must be accumulated by the preload loop.
// Returning 0 before the requested payload is complete represents EOF/failure
// for this pure helper.
using fsk_read_fn = size_t (*)(void *ctx, uint8_t *dst, size_t n);

// Fill an already-allocated block table with exactly `want` logical bytes where
// possible.
//
// The helper:
//   - never requests more than `read_max` bytes in one reader call;
//   - never writes beyond a block;
//   - never writes beyond logical byte `want`;
//   - accumulates positive short reads;
//   - stops when `want` bytes have been loaded or reader returns 0.
//
// Returns total bytes actually loaded.
// Success is indicated by return value == want.
//
// This function contains no filesystem dependency: production supplies an
// fnio::fread adapter, while host tests supply deterministic stub readers.
size_t fsk_preload_into_blocks(uint8_t *const *blocks,
                               size_t block_count,
                               size_t block_size,
                               size_t want,
                               size_t read_max,
                               fsk_read_fn reader,
                               void *ctx);

// -----------------------------------------------------------------------------
// Structural chunk bounds + caller next-offset (pure, host-testable)
// -----------------------------------------------------------------------------
//
// All derived structural state for one A8CAS chunk in ONE result, so the
// production caller (sioCassette::play_fsk_chunk) and the host property tests
// use the SAME next-offset rule — there is no second O+8+L formula anywhere.
//
// Integer widths mirror the production caller: file size / offsets / byte
// counts are size_t; the declared chunk length is the on-file uint16.
// Arithmetic is subtraction-guarded (offset-past-EOF and <8-remaining checked
// before any subtraction) so it never underflows.
struct FskBounds
{
    size_t data_avail;            // min(declared_len, bytes after the 8-byte header)
    size_t value_count;           // floor(data_avail / 2) (Req 6.4)
    size_t next_offset;           // caller's next read offset: O+8+L when well-formed,
                                  //   0 for incomplete header or body overrun (EOT)
    bool   header_complete;       // false when < 8 header bytes remain (or offset > filesize)
    bool   structurally_truncated;// declared body would pass EOF (clamped to what exists)
};

// Compute the structural bounds + next offset for a chunk header at `offset`
// within a file of `filesize` bytes declaring `declared_len` payload bytes.
//
// Rules (design "Malformed Chunk Policy" / Property 7):
//   - offset > filesize OR remaining < 8  -> header_complete=false, everything 0,
//                                            next_offset=0 (EOT)
//   - declared_len > body_available       -> structurally_truncated=true,
//                                            data_avail=body_available,
//                                            next_offset=0 (EOT/overrun)
//   - well-formed                         -> data_avail=declared_len,
//                                            next_offset=offset+8+declared_len
// value_count is always floor(data_avail/2).
//
// Pure: no I/O, no hardware, no globals. This is the single source of truth for
// the production caller and the host tests.
FskBounds fsk_compute_bounds(size_t filesize, size_t offset,
                             uint16_t declared_len);

// -----------------------------------------------------------------------------
// Pure host-test cursor over a segmented payload
// -----------------------------------------------------------------------------

struct FskChunkView
{
    const uint8_t *const *blocks; // caller-owned immutable block pointer table
    size_t block_size;            // bytes per allocated payload block

    // Logical clamped payload length. A trailing odd byte may exist but is never
    // read as part of a value because fsk_value_count() floors len / 2.
    size_t data_len_available;

    // Original A8CAS value index. This index advances even for zero-duration
    // values so parity of every following signal value is preserved.
    size_t value_index;

    // Logical byte position of the next uint16 pair.
    // Normally == value_index * 2 after all preceding values are consumed.
    size_t byte_pos;

    // State used while one long value is split into multiple <=32767-tick
    // portions.
    uint32_t remaining_ticks;
    bool remaining_level_high;
};

// Initialize a cursor over a caller-owned segmented payload.
//
// For an empty payload:
//   blocks may be nullptr
//   block_size may be 0
// because fsk_view_step will have no complete values to read.
FSK_FORCE_INLINE FskChunkView fsk_view_init(const uint8_t *const *blocks,
                                            size_t block_size,
                                            size_t data_len_available)
{
    return FskChunkView{
        blocks,
        block_size,
        data_len_available,
        0,
        0,
        0,
        false
    };
}

struct FskStep
{
    bool produced;   // true when this step yields one RMT-sized portion
    bool level_high; // logical level of that portion
    uint32_t ticks;  // 1..32767 when produced == true
    bool done;       // true when no waveform work remains
};

// Advance the pure cursor by one emitted portion.
//
// Behavior:
//   - reads only complete 2-byte FSK values;
//   - accesses bytes through fsk_block_le16();
//   - skips zero-duration values while still consuming their original index;
//   - splits long durations incrementally using O(1) state;
//   - never materializes the waveform;
//   - never performs allocation, I/O, logging, or hardware access.
//
// Defined in fsk_plan.cpp.
// It is intended for host tests and is NOT called from the RMT ISR.
FskStep fsk_view_step(FskChunkView &view);

// -----------------------------------------------------------------------------
// Contiguous zero-IRG FSK "run" membership (pure, host-testable)
// -----------------------------------------------------------------------------
//
// Authentic A8CAS raw-FSK images encode a continuous tape signal as consecutive
// `fsk ` chunks where only the FIRST carries a non-zero Inter-Record Gap and all
// following chunks carry IRG == 0. Reproducing each chunk with its own RMT
// begin/emit/end lifecycle would insert a real-time gap (teardown + file I/O +
// re-allocation + restart) at every A8CAS container boundary, which breaks the
// continuity the original loader expects. The fix reproduces a maximal run of
// consecutive FSK chunks as ONE continuous RMT lifecycle.
//
// This pure predicate decides whether the NEXT candidate chunk should JOIN the
// run already in progress (the first chunk of a run is always included by the
// caller). A candidate joins iff it is a `fsk ` chunk, its IRG is exactly 0,
// and it is structurally valid (a complete 8-byte header is present and the
// declared body does not overrun EOF). A non-FSK chunk, an FSK chunk with a
// non-zero IRG, EOF, or a truncated/overrun boundary ends the run (the caller
// stops BEFORE such a chunk; a non-zero-IRG FSK chunk starts a NEW run later).
//
// No I/O, no hardware, no globals. `candidate_is_fsk` and the other fields are
// derived by the caller from the on-file header + fsk_compute_bounds().
FSK_FORCE_INLINE bool fsk_run_should_join(bool candidate_is_fsk,
                                          uint16_t candidate_irg,
                                          bool candidate_header_complete,
                                          bool candidate_structurally_truncated)
{
    return candidate_is_fsk &&
           candidate_irg == 0 &&
           candidate_header_complete &&
           !candidate_structurally_truncated;
}

// Upper bound on chunks reproduced as ONE continuous FSK run. The largest known
// authentic corpus run is 7 chunks; this cap is generous. A run that would grow
// beyond the cap simply ends at the cap boundary — correct because the walker's
// next call resumes at the following chunk (its own IRG is 0, so no gap is
// introduced by the split other than the same boundary cost that already exists
// for the rare >cap case, which no real corpus member hits).
static constexpr size_t FSK_RUN_MAX_CHUNKS = 16;

#undef FSK_FORCE_INLINE

#endif // FSK_PLAN_H
