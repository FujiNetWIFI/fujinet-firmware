#ifndef FSK_PLAN_H
#define FSK_PLAN_H

// fsk_plan.h — pure, host-buildable A8CAS FSK parsing/timing rules. No
// filesystem, RMT/GPIO, FujiNet globals, allocation, or logging. The rule
// helpers the production RMT ISR callback uses live here so host tests and
// production share exactly the same decode/parity/timing/split logic.
//
// The preloaded FSK payload is stored as a table of fixed-size blocks rather
// than one contiguous allocation; the block-table accessors below expose
// that logical payload without any ESP-IDF/filesystem dependency.

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
// `blocks` is a contiguous table of block pointers, each `block_size` bytes
// (the final block may be only partly used). No bounds checks here — the
// production ISR path must stay tiny and deterministic; callers guarantee
// requested logical positions are valid.

// Return logical payload byte k from the segmented block table.
FSK_FORCE_INLINE uint8_t fsk_block_byte(const uint8_t *const *blocks,
                                        size_t block_size,
                                        size_t k)
{
    return blocks[k / block_size][k % block_size];
}

// Decode a little-endian FSK value from logical positions k and k+1, fetched
// independently since k+1 may reside in the following block.
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

// Injected reader used by fsk_preload_into_blocks: places up to `n` bytes
// into `dst`, returns the number delivered. A positive short return is
// accumulated; 0 before `want` is reached means EOF/failure.
using fsk_read_fn = size_t (*)(void *ctx, uint8_t *dst, size_t n);

// Fills an already-allocated block table with up to `want` logical bytes:
// never requests more than `read_max` per call, never writes past a block or
// past `want`, accumulates short reads. Returns bytes actually loaded;
// success is return value == want. No filesystem dependency — production
// supplies an fnio::fread adapter, host tests supply stub readers.
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
// All derived structural state for one A8CAS chunk in ONE result, so
// play_fsk_chunk() and the host tests share the same next-offset rule — no
// second O+8+L formula anywhere. Arithmetic is subtraction-guarded so it
// never underflows.
struct FskBounds
{
    size_t data_avail;            // min(declared_len, bytes after the 8-byte header)
    size_t value_count;           // floor(data_avail / 2)
    size_t next_offset;           // caller's next read offset: O+8+L when well-formed,
                                  //   0 for incomplete header or body overrun (EOT)
    bool   header_complete;       // false when < 8 header bytes remain (or offset > filesize)
    bool   structurally_truncated;// declared body would pass EOF (clamped to what exists)
};

// Computes the structural bounds + next offset for a chunk header at
// `offset`: incomplete header or overrun body -> next_offset=0 (EOT);
// well-formed -> data_avail=declared_len, next_offset=offset+8+declared_len.
// value_count is always floor(data_avail/2). Pure; single source of truth
// for production and host tests.
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

// Initialize a cursor over a caller-owned segmented payload. For an empty
// payload, blocks may be nullptr and block_size 0 — fsk_view_step will have
// no complete values to read.
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

// Advances the pure cursor by one emitted portion: reads only complete
// 2-byte values via fsk_block_le16(), skips zero-duration values (still
// consuming their index), splits long durations incrementally. Never
// materializes the waveform or touches allocation/I/O/hardware. Host-test
// only — not called from the RMT ISR.
FskStep fsk_view_step(FskChunkView &view);

// -----------------------------------------------------------------------------
// Contiguous zero-IRG FSK "run" membership (pure, host-testable)
// -----------------------------------------------------------------------------
//
// Authentic A8CAS raw-FSK images encode one continuous tape signal as
// consecutive `fsk ` chunks where only the first carries a non-zero IRG.
// Reproducing each with its own RMT lifecycle would insert a gap at every
// container boundary, breaking loader continuity — so a maximal run of such
// chunks is reproduced as ONE continuous RMT lifecycle instead.
//
// This predicate decides whether the next candidate chunk joins the
// in-progress run (the first chunk is always included by the caller): it
// must be `fsk `, IRG exactly 0, and structurally valid. Anything else ends
// the run (a non-zero-IRG FSK chunk starts a new run later).
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

// Upper bound on chunks reproduced as ONE continuous FSK run (largest known
// authentic corpus run is 7). A run growing beyond this simply ends at the
// cap; the walker's next call resumes at the following chunk with no extra
// gap, since that chunk's own IRG is still 0.
static constexpr size_t FSK_RUN_MAX_CHUNKS = 16;

// -----------------------------------------------------------------------------
// All-MARK run classification (pure, host-testable)
// -----------------------------------------------------------------------------
//
// An FSK run whose every even-indexed (LOW/SPACE) value is zero requests no
// LOW time at all: the line is MARK for the whole run. Such a run needs no RMT
// waveform, only a MARK-preserving wait of `total_ticks`.
struct FskRunSummary
{
    bool     has_space;   // true when some even-indexed value is non-zero
    uint64_t total_ticks; // whole-run duration in 1 MHz ticks (1 tick = 1 us);
                          //   only complete when has_space == false, because the
                          //   scan stops at the first SPACE
};

// Scans a preloaded run in the same per-chunk layout production uses: chunk c
// starts at logical byte chunk_block_base[c] * block_size and holds
// chunk_value_counts[c] values. Parity restarts at index 0 for every chunk;
// zero-duration values still consume their index. `blocks` may be nullptr only
// when every value count is zero.
FskRunSummary fsk_run_summarize(const uint8_t *const *blocks,
                                size_t block_size,
                                const size_t *chunk_block_base,
                                const size_t *chunk_value_counts,
                                size_t chunk_count);

#undef FSK_FORCE_INLINE

#endif // FSK_PLAN_H
