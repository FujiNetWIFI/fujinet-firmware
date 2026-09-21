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

// Same classification for the part of the run still to be played after a
// resume: starts at value `start_value` of chunk `start_chunk`, with the first
// `skip_ticks` of that value already consumed (so only its remainder counts).
// `total_ticks` is then the remaining duration. Parity stays the chunk-local
// value index. start_chunk >= chunk_count (or start_value past the chunk's last
// value) yields an empty, all-MARK result.
FskRunSummary fsk_run_summarize_from(const uint8_t *const *blocks,
                                     size_t block_size,
                                     const size_t *chunk_block_base,
                                     const size_t *chunk_value_counts,
                                     size_t chunk_count,
                                     size_t start_chunk,
                                     size_t start_value,
                                     uint64_t skip_ticks);

// -----------------------------------------------------------------------------
// MOTOR-driven pause: physical position + stop-claim rules (pure, host-testable)
// -----------------------------------------------------------------------------

// Why an active RMT transaction is being stopped. Guarded by the channel lock
// in production; only one reason can own a transaction.
enum class FskStopReason : uint8_t { NONE = 0, MOTOR = 1, HTTP = 2 };

// Single-owner claim of a live transaction. Succeeds only when the channel is
// still published and no other reason has claimed it yet; it can never succeed
// after natural completion has unpublished the channel.
FSK_FORCE_INLINE bool fsk_stop_try_claim(FskStopReason &slot,
                                         bool channel_published,
                                         FskStopReason requester)
{
    if (!channel_published || requester == FskStopReason::NONE ||
        slot != FskStopReason::NONE)
        return false;
    slot = requester;
    return true;
}

// Hardware-proven waveform positions of one RMT transaction, in the run's own
// tick count (from the run's first value, including any resume prefix).
//
// A reference is a pair (ticks, ts_us): the waveform had physically reached
// `ticks` no later than `ts_us`. The seed is the position the transaction
// started from (stamped right after rmt_transmit() returned, i.e. never before
// the real hardware start), and each 256-symbol refill callback adds one
// reference. A callback runs AFTER the hardware crossed its boundary, so the
// crossing is at some unknown instant <= the callback timestamp, never later:
//     true_position(t) >= ref.ticks + (t - ref.ts_us)     for every t >= ref.ts_us
// A reference whose timestamp is LATER than a stop time says nothing about that
// stop: the boundary may have been crossed before or after it. Only references
// not later than the stop are usable, and the newest of them is the tightest.
// The log keeps the last FSK_REF_RING references so a task that runs a few
// refills late can still resolve an earlier stop; the seed is always kept.
static constexpr size_t FSK_REF_RING = 8;

struct FskConfirmRef
{
    uint64_t ticks;
    int64_t  ts_us;
};

struct FskRefLog
{
    FskConfirmRef seed;                 // transaction start (never overwritten)
    FskConfirmRef ring[FSK_REF_RING];   // refill references, oldest overwritten first
    uint32_t      count;                // references recorded so far (may exceed FSK_REF_RING)
};

// Start a transaction's log at `seed_ticks`. The seed timestamp is stamped by
// fsk_ref_log_set_seed_ts() once the hardware has really started.
FSK_FORCE_INLINE void fsk_ref_log_reset(FskRefLog &log, uint64_t seed_ticks)
{
    log.seed.ticks = seed_ticks;
    log.seed.ts_us = 0;
    for (size_t i = 0; i < FSK_REF_RING; ++i)
    {
        log.ring[i].ticks = seed_ticks;
        log.ring[i].ts_us = 0;
    }
    log.count = 0;
}

FSK_FORCE_INLINE void fsk_ref_log_set_seed_ts(FskRefLog &log, int64_t ts_us)
{
    log.seed.ts_us = ts_us;
}

// Record one refill reference (encoder callback, ISR context).
FSK_FORCE_INLINE void fsk_ref_log_confirm(FskRefLog &log, uint64_t ticks, int64_t ts_us)
{
    FskConfirmRef &slot = log.ring[log.count % FSK_REF_RING];
    slot.ticks = ticks;
    slot.ts_us = ts_us;
    ++log.count;
}

// The newest reference whose timestamp is not later than `stop_ts_us`; the seed
// when none qualifies (all references are later than the stop, or were
// overwritten because the task ran more than FSK_REF_RING refills late).
FSK_FORCE_INLINE FskConfirmRef fsk_ref_log_at(const FskRefLog &log, int64_t stop_ts_us)
{
    const uint32_t avail = log.count < FSK_REF_RING ? log.count
                                                    : static_cast<uint32_t>(FSK_REF_RING);
    for (uint32_t i = 0; i < avail; ++i)
    {
        const FskConfirmRef &r = log.ring[(log.count - 1u - i) % FSK_REF_RING];
        if (r.ts_us <= stop_ts_us)
            return r;
    }
    return log.seed;
}

// -----------------------------------------------------------------------------
// Producer of the references: which boundary does each threshold callback prove?
//
// ESP-IDF's non-DMA RMT TX runs a two-half ping-pong over the channel memory
// (mem_block_symbols = 512, threshold = 256 symbols). The driver calls the
// encoder callback:
//   * once, inside rmt_transmit(), offering the WHOLE 512-symbol memory
//     (the prefill: symbols 0..511), then
//   * on every threshold interrupt, offering only the 256-symbol half that has
//     just become free (mem_end alternates 256 / 512).
// Let Hk be the cumulative duration of symbols 0..256*(k+1)-1, so H0 is the end
// of the first half, H1 the end of the second, and so on:
//
//   prefill      encodes symbols 0..511          cumulative = H1   (pending = H0)
//   threshold 1  hardware has consumed 0..255    PROVES H0
//                refill encodes 512..767         cumulative = H2   (pending = H1)
//   threshold 2  hardware has consumed 256..511  PROVES H1
//                refill encodes 768..1023        cumulative = H3   (pending = H2)
//
// The half a callback has just encoded is NOT yet consumed when the next
// threshold fires; that threshold proves the half that was ALREADY resident
// before this refill began. So a refill's next pending boundary is the
// cumulative total at the START of that refill (`resident_ticks`), never the
// total at its end. Only the prefill is special: it encodes two halves at once
// but only the first is proven by the first threshold (`prefill_half_ticks`).
//
// A callback that finishes the waveform sets no pending boundary: the driver
// stops calling the encoder and no later threshold can add a reference.
//
// WHAT A THRESHOLD PROVES ABOUT THE SIGNAL ON THE PIN
// The event does not mean "the half has been played". The RMT reads ahead of the
// pin, in units of ENTRIES (one symbol word holds two: level0/duration0 and
// level1/duration1). Measured on the FujiNet over 495 thresholds of a complete
// Zorro load (per-threshold records with the callback time, the boundary, the
// durations of the last symbols and the raw RMT status word):
//     boundary - (pin position at the callback) = b1 + a0 + b0 - 20 us
// with a 1.0 us standard deviation and a 20..32 us range, where a0/b0 are the
// two entries of the LAST symbol of the half and b1 is the second entry of the
// second-to-last one. That is: when the threshold fires the pin is at the start of
// the second entry of the second-to-last symbol, i.e. the last
// FSK_RMT_PREFETCH_ENTRIES (3) entries of the half are still to be played
// (the read pointer in the RMT status word is exactly at the half boundary at
// every callback). No other combination of the last symbols' halves fits (the
// next best has a 3.8 ms spread; "the last two whole symbols" over-subtracts a1,
// up to 32.8 ms), and the constant left over is the callback latency.
//
// A reference that pairs the boundary with the callback time is therefore AHEAD
// of the pin by those three entries (mean 11.3 ms, up to 98 ms with the 65.5 ms
// symbols of the Zorro leader). The published reference is the boundary minus
// them: the position the pin had reached when the threshold fired, which the
// callback (later by its latency) can only under-report. A stop resolved from it
// is never ahead of the tape, and behind it by no more than that latency.
// -----------------------------------------------------------------------------

// Entries of the resident half the transmitter has yet to play when that half's
// threshold event fires: b of the second-to-last symbol, then a and b of the last.
static constexpr size_t FSK_RMT_PREFETCH_ENTRIES = 3;

// Durations of the last entries (portions) encoded are kept newest-first (at most this many).
static constexpr size_t FSK_TAIL_ENTRIES = 4;
static_assert(FSK_RMT_PREFETCH_ENTRIES <= FSK_TAIL_ENTRIES, "tail too short for the prefetch depth");

// Symbols in one ping-pong half (the RMT TX threshold).
static constexpr size_t FSK_RMT_HALF_SYMBOLS = 256;

struct FskBoundaryTracker
{
    uint64_t cumulative_ticks;    // total encoded so far, seed included
    uint64_t prefill_half_ticks;  // cumulative at the end of the first half (prefill only)
    uint64_t pending_ticks;       // boundary the NEXT threshold callback proves
    uint64_t resident_ticks;      // cumulative when the current refill began
    // Durations of the last entries (portions) before each of those totals, newest first.
    uint32_t tail[FSK_TAIL_ENTRIES];              // ... before cumulative_ticks
    uint32_t prefill_half_tail[FSK_TAIL_ENTRIES]; // ... before prefill_half_ticks
    uint32_t pending_tail[FSK_TAIL_ENTRIES];      // ... before pending_ticks
    uint32_t resident_tail[FSK_TAIL_ENTRIES];     // ... before resident_ticks
};

FSK_FORCE_INLINE void fsk_tail_copy(uint32_t *dst, const uint32_t *src)
{
    for (size_t i = 0; i < FSK_TAIL_ENTRIES; ++i)
        dst[i] = src[i];
}

FSK_FORCE_INLINE void fsk_bounds_reset(FskBoundaryTracker &t, uint64_t seed_ticks)
{
    t.cumulative_ticks   = seed_ticks;
    t.prefill_half_ticks = seed_ticks;
    t.pending_ticks      = seed_ticks;
    t.resident_ticks     = seed_ticks;
    for (size_t i = 0; i < FSK_TAIL_ENTRIES; ++i)
        t.tail[i] = t.prefill_half_tail[i] = t.pending_tail[i] = t.resident_tail[i] = 0;
}

// The position the pin had already reached when the threshold for
// `boundary_ticks` fired: the boundary minus the entries still to be played.
FSK_FORCE_INLINE uint64_t fsk_boundary_physical_ticks(uint64_t boundary_ticks,
                                                      const uint32_t *tail)
{
    uint64_t lead = 0;
    for (size_t i = 0; i < FSK_RMT_PREFETCH_ENTRIES; ++i)
        lead += tail[i];
    return boundary_ticks > lead ? boundary_ticks - lead : 0;
}

// Encoder callback entry. A refill (not the prefill) is a threshold callback:
// it records the boundary the hardware just proved, and remembers how much was
// already resident ahead of the cursor before it encodes anything.
FSK_FORCE_INLINE void fsk_bounds_refill_begin(FskBoundaryTracker &t, bool is_prefill,
                                              FskRefLog &log, int64_t now_us)
{
    if (is_prefill)
        return;
    fsk_ref_log_confirm(log, fsk_boundary_physical_ticks(t.pending_ticks, t.pending_tail), now_us);
    t.resident_ticks = t.cumulative_ticks;
    fsk_tail_copy(t.resident_tail, t.tail);
}

// One entry (portion) was encoded: it counts toward the total and becomes the
// newest of the tail durations.
FSK_FORCE_INLINE void fsk_bounds_add_portion(FskBoundaryTracker &t, uint32_t portion_ticks)
{
    t.cumulative_ticks += portion_ticks;
    for (size_t i = FSK_TAIL_ENTRIES - 1; i > 0; --i)
        t.tail[i] = t.tail[i - 1];
    t.tail[0] = portion_ticks;
}

// After each complete symbol; `symbols_written` counts this call's symbols.
FSK_FORCE_INLINE void fsk_bounds_symbol_written(FskBoundaryTracker &t, bool is_prefill,
                                                size_t symbols_written)
{
    if (is_prefill && symbols_written == FSK_RMT_HALF_SYMBOLS)
    {
        t.prefill_half_ticks = t.cumulative_ticks;
        fsk_tail_copy(t.prefill_half_tail, t.tail);
    }
}

// The callback filled everything it was offered and the waveform continues, so
// another threshold will follow: publish the boundary it will prove.
FSK_FORCE_INLINE void fsk_bounds_refill_full(FskBoundaryTracker &t, bool is_prefill)
{
    t.pending_ticks = is_prefill ? t.prefill_half_ticks : t.resident_ticks;
    fsk_tail_copy(t.pending_tail, is_prefill ? t.prefill_half_tail : t.resident_tail);
}

// Waveform ticks physically emitted at `stop_ts_us`: the newest reference not
// later than the stop, interpolated forward at the 1 MHz RMT rate, never
// crediting more than the encoder ever produced. The result is a lower bound of
// the true position (a slightly early stop position only replays a little tape;
// a late one would skip data). It never uses a reference newer than the stop.
FSK_FORCE_INLINE uint64_t fsk_physical_ticks_at(const FskRefLog &log,
                                                uint64_t cumulative_ticks,
                                                int64_t stop_ts_us)
{
    const FskConfirmRef ref = fsk_ref_log_at(log, stop_ts_us);
    uint64_t t = ref.ticks;
    if (stop_ts_us > ref.ts_us)
        t += static_cast<uint64_t>(stop_ts_us - ref.ts_us);
    return t > cumulative_ticks ? cumulative_ticks : t;
}

// True when the tape had already physically ended at the accepted stop time:
// every value was encoded and the physical position reached the encoded total.
// Such a stop is a genuine end-of-run, not a pause (it must never replay the
// last value).
FSK_FORCE_INLINE bool fsk_stop_is_natural_end(bool encoding_complete,
                                              uint64_t physical_ticks,
                                              uint64_t cumulative_ticks)
{
    return encoding_complete && physical_ticks >= cumulative_ticks;
}

// What the emit wait should do on one wake-up, decided by PHYSICAL ordering and
// never by which flag the task happens to inspect first:
//   * no accepted stop request -> only the hardware can end the run:
//       hw_done ? NATURAL : KEEP_WAITING
//   * an accepted stop (MOTOR or HTTP) -> compare its timestamp with the
//     physical end of the encoded waveform, even if the hardware has already
//     reported done: before the end -> FROZEN at the stop position; at or after
//     the end -> NATURAL (the position is then the end of the run, so a
//     genuine end is never turned into a replay of the last value).
// physical_ticks is the frozen waveform position (or, for NATURAL, the end).
// `log` may be null only when reason is NONE. The classification is a lower
// bound: a stop is NATURAL only if even the conservative position has reached
// the encoded total, so a hardware-done flag seen late can never turn a stop
// that preceded the physical end into a natural end.
enum class FskEmitOutcome : uint8_t { KEEP_WAITING, NATURAL, FROZEN };

struct FskEmitDecision
{
    FskEmitOutcome outcome;
    uint64_t       physical_ticks;
};

FSK_FORCE_INLINE FskEmitDecision fsk_decide_emit(bool hw_done,
                                                 FskStopReason reason,
                                                 bool encoding_complete,
                                                 const FskRefLog *log,
                                                 uint64_t cumulative_ticks,
                                                 int64_t stop_ts_us)
{
    if (reason == FskStopReason::NONE || log == nullptr)
        return FskEmitDecision{ hw_done ? FskEmitOutcome::NATURAL
                                        : FskEmitOutcome::KEEP_WAITING,
                                cumulative_ticks };

    const uint64_t physical = fsk_physical_ticks_at(*log, cumulative_ticks, stop_ts_us);
    return FskEmitDecision{ fsk_stop_is_natural_end(encoding_complete, physical,
                                                    cumulative_ticks)
                                ? FskEmitOutcome::NATURAL
                                : FskEmitOutcome::FROZEN,
                            physical };
}

// -----------------------------------------------------------------------------
// Resume-setup failure: keep the frozen position
// -----------------------------------------------------------------------------

// A run resumed from a frozen (R, Q) consumes that position when it starts. If
// its setup then fails BEFORE any waveform is emitted (preload, RMT begin, or
// rmt_transmit), no tape time was played, so the cassette must stay exactly
// where it was and the dispatch be retried, never advanced to the next chunk.
//   resumed         - this dispatch started from a frozen position
//   q0_us           - that position (run-relative, from before the leading IRG)
//   irg_us          - the run's leading IRG
//   wave_seed_ticks - waveform ticks already played at q0 (0 while inside the IRG)
//   irg_held        - the whole remaining IRG has already elapsed in this dispatch
//                     (silent tape time that cannot be un-played: the position is
//                     then the start of the waveform, irg_us + wave_seed_ticks)
// A dispatch that did not resume from a frozen position keeps its existing
// behavior (rearm == false).
struct FskSetupRetry
{
    bool     rearm;
    uint64_t q_us;
};

FSK_FORCE_INLINE FskSetupRetry fsk_setup_failure_retry(bool resumed,
                                                       uint64_t q0_us,
                                                       uint64_t irg_us,
                                                       uint64_t wave_seed_ticks,
                                                       bool irg_held)
{
    if (!resumed)
        return FskSetupRetry{ false, 0 };
    return FskSetupRetry{ true, irg_held ? irg_us + wave_seed_ticks : q0_us };
}

// -----------------------------------------------------------------------------
// Tape time base: the tape has been running since MOTOR ON
// -----------------------------------------------------------------------------
//
// A cassette recorder moves the tape from the moment MOTOR rises. The emulator
// cannot: it first has to preload the whole run from the SD card (seconds) and
// only then starts the waveform, so the tape's own time zero drifts later by
// whatever the preload took. The Atari OS ignores the first ~9.7 s after MOTOR ON
// (its cassette handler waits before it listens); if the waveform starts after
// that, whatever the tape carries in its first milliseconds - here the recorded
// 100 us LOW blip 0.7 ms in - reaches an Atari that is already acquiring the
// baud rate, which mis-measures it (BOOT ERROR -> SELF TEST).
//
// The start position is therefore the logical position q (IRG included, so the
// IRG is never counted twice) advanced by the time MOTOR has been on:
//     q_start = q0 + elapsed_since_motor_on
// It is only ever applied to the first dispatch after MOTOR ON at the physical
// start of the tape (never to a resume or a rewind, whose position is already
// known), and it may only DISCARD TAPE THAT CARRIES NO DATA: the skipped part of
// the waveform must be MARK (HIGH) apart from at most FSK_TIMEBASE_LOW_BUDGET_US
// of LOW time in total. Anything the loader could decode is therefore never
// skipped; if the run holds data early, the start is simply not advanced (the
// previous behavior) for that part.

// Total LOW time the skipped prefix may contain. Below one start bit of any
// standard baud (600 baud: 1.67 ms), so not even a single byte can hide in it.
static constexpr uint64_t FSK_TIMEBASE_LOW_BUDGET_US = 300;

// Does the time base apply to this dispatch?
//   resumed         - the dispatch resumes a frozen (R, Q): the position is known
//   first_at_start  - first dispatch after MOTOR ON, at the physical start of the tape
//   have_stamp      - the MOTOR ON time is known
//   run_loaded      - the run is resident (nothing to inspect otherwise)
FSK_FORCE_INLINE bool fsk_timebase_applies(bool resumed, bool first_at_start,
                                           bool have_stamp, bool run_loaded)
{
    return !resumed && first_at_start && have_stamp && run_loaded;
}

// Waveform ticks the tape has already played by logical position q0 + elapsed,
// beyond the leading IRG (0 while the position is still inside the IRG).
FSK_FORCE_INLINE uint64_t fsk_timebase_wave_want(uint64_t irg_us, uint64_t q0_us,
                                                 int64_t elapsed_us)
{
    if (elapsed_us <= 0)
        return 0;
    const uint64_t q_want = q0_us + static_cast<uint64_t>(elapsed_us);
    return q_want > irg_us ? q_want - irg_us : 0;
}

// The logical position the transmission starts from. `inert_ticks` is how much
// of the waveform from its start carries no data (cas_fsk_inert_ticks); the
// position is never advanced past it. While the elapsed time is still inside the
// IRG only the remaining IRG is left; exactly at its end the waveform starts at
// tick 0.
FSK_FORCE_INLINE uint64_t fsk_timebase_q(uint64_t irg_us, uint64_t q0_us, int64_t elapsed_us,
                                         uint64_t inert_ticks)
{
    if (elapsed_us <= 0)
        return q0_us;
    const uint64_t q_want = q0_us + static_cast<uint64_t>(elapsed_us);
    if (q_want <= irg_us)
        return q_want; // still inside the IRG: the remaining IRG is what is left to play
    uint64_t wave = q_want - irg_us;
    if (wave > inert_ticks)
        wave = inert_ticks; // never discard tape that may carry data
    return irg_us + wave;
}

// -----------------------------------------------------------------------------
// Fast MOTOR resume: abort the RMT transmission, keep the resident run
// -----------------------------------------------------------------------------
//
// A real cassette recorder stops when MOTOR drops and continues immediately when
// it rises. Two things used to make a MOTOR OFF -> ON cycle cost seconds:
//   1. the RMT kept playing (muted) everything already queued in its 512-symbol
//      memory before the channel could be reused (1.8-3.6 s of ordinary FSK), and
//   2. the resumed dispatch re-read the whole remaining run from the SD card.
// The first is removed by aborting the transmission in hardware at freeze time,
// the second by keeping the preloaded run resident across the freeze.

// Every RMT memory block the ESP32 has (8 x 64 symbols) belongs to our single
// 512-symbol TX channel, which is therefore channel 0 and starts at word 0 of the
// RMT memory. The hardware abort (EOF marker in word 0 + read-pointer reset, the
// sequence the legacy ESP32 rmt_tx_stop() uses) is only valid in that layout; if
// the layout is not exactly this, the caller skips the abort and the transaction
// simply drains (slower, but always safe).
static constexpr uint32_t FSK_RMT_ALL_MEM_BLOCKS = 8;

// DATA IN must stay at MARK (HIGH) from the moment the RMT channel is connected to
// the pin until its first symbol. ESP-IDF 5.4 rmt_new_tx_channel() fixes the RMT
// idle level at LOW (rmt_tx.c: rmt_ll_tx_fix_idle_level(..., 0, true)) and has no
// init_level option, so a freshly created channel drives a LOW pulse on DATA IN
// until rmt_transmit() sets the idle level to eot_level (~1.5 ms here). An Atari
// that is already listening for the cassette leader (OS baud acquisition, ED3D)
// takes it for a start bit and mis-measures the baud rate: BOOT ERROR. The channel
// idle level is therefore raised to this value right after creation.
// rmt_new_tx_channel() cannot be created without connecting a real output GPIO (GPIO_IS_VALID_OUTPUT_GPIO
// rejects -1/NC) and forces the RMT idle level to 0. Creating the channel with invert_out = 1 makes the
// GPIO matrix invert the RMT output, so that forced idle 0 is MARK (HIGH) on the pad from the very instant
// the pad is connected: no LOW pulse during the creation. The encoder then writes inverted levels (the
// double inversion leaves the waveform on the pad unchanged) and the end-of-transmission / idle level that
// means MARK on the pad is the RMT-side FSK_RMT_IDLE_LEVEL_MARK below.
static constexpr uint8_t FSK_RMT_OUT_INVERT = 1;

// RMT-side level whose PAD level is MARK (HIGH): the idle level to hold and the eot_level.
static constexpr uint8_t FSK_RMT_IDLE_LEVEL_MARK = FSK_RMT_OUT_INVERT ? 0 : 1;

FSK_FORCE_INLINE bool fsk_rmt_abort_applicable(uint32_t ch0_mem_blocks)
{
    return ch0_mem_blocks == FSK_RMT_ALL_MEM_BLOCKS;
}

// May a resumed dispatch reuse the run that is still resident from the freeze,
// instead of scanning the chunk headers and preloading the SD card again?
// Only when every condition that made it valid still holds:
//   resumed        - this dispatch continues a frozen (R, Q) position
//   resident_valid - the freeze marked the run resident and nothing freed or
//                    invalidated it since (rewind, mount, unmount, any free)
//   same_offset    - it is the run whose first chunk is R
//   same_file      - the same mounted image, same size
//   blocks_present- the block table and run descriptors still exist
//   idle           - no transmission still owns the blocks
FSK_FORCE_INLINE bool fsk_resident_reuse_ok(bool resumed, bool resident_valid,
                                            bool same_offset, bool same_file,
                                            bool blocks_present, bool idle)
{
    return resumed && resident_valid && same_offset && same_file && blocks_present && idle;
}

#undef FSK_FORCE_INLINE

#endif // FSK_PLAN_H
