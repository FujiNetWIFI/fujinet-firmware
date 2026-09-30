#ifndef FSK_PROGRESSIVE_H
#define FSK_PROGRESSIVE_H

// fsk_progressive.h - pure, host-testable model of PROGRESSIVE raw-FSK loading.
//
// The full-preload path reads the whole zero-IRG run (header scan + payload) before the first
// RMT symbol is produced. On a slow backing filesystem that turns the per-request latency into
// time-to-first-edge (remote TNFS: 985 x ~237 ms; remote HTTP: 16 window fetches). Here the run
// is instead LOADED SEQUENTIALLY BY A PRODUCER WHILE THE RMT PLAYS IT:
//
//   producer (loader task)                       consumer (RMT encoder callback, ISR)
//   ----------------------                       ------------------------------------
//   read header k+1 (sequentially, no scan)      never does I/O, malloc or logging
//   fill descriptor k+1, publish pub_chunks      reads only ordinals  < pub_values
//   read payload block by block                  reads only chunks    < pub_chunks
//   publish pub_values after each block          at the end of the published data:
//   FINAL / FAILED / STOPPED at the end             FINAL      -> natural end of the run
//                                                   otherwise  -> UNDERRUN (explicit stop)
//
// Publication is a release store of a 32-bit word; the consumer does an acquire load of it, so
// every descriptor / block byte written before the store is visible after the load. The producer
// writes only unpublished blocks and descriptors; a published block is never written again.
//
// "Ordinal" is the position of a value in the run counted across chunks (chunk 0's values first).
//
// No ESP-IDF, filesystem, allocation or logging in this module: the producer reads through an
// injected sequential reader and allocates through an injected allocator.

#include "fsk_plan.h"
#include "cassette_time_plan.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#if defined(__GNUC__) || defined(__clang__)
#define FSK_PROG_INLINE static inline __attribute__((always_inline))
#else
#define FSK_PROG_INLINE static inline
#endif

// ---- tunables ------------------------------------------------------------------------------

// Payload is stored in blocks of this many bytes; every chunk starts on a block boundary (same
// layout as the full-preload path, so a value never straddles a chunk).
static constexpr size_t FSK_PROG_BLOCK_BYTES = 512;

// A run whose FIRST chunk carries at least this many payload bytes is loaded progressively; anything
// smaller (the MARK stubs of the interleaved images, tiny runs) keeps the full-preload path, which is
// cheap there and also classifies all-MARK runs.
static constexpr size_t FSK_PROG_MIN_FIRST_CHUNK_BYTES = 16384;

// Tape time that must already be PUBLISHED ahead of the start position before the waveform starts
// (also required before a MOTOR resume). It is measured in waveform time, from the durations of the
// published values, not in bytes: a long leader needs few bytes, dense data needs many.
static constexpr uint64_t FSK_PROG_MIN_RUNWAY_US = 20ULL * 1000000ULL;

// Values the RMT encoder may consume before the first refill: the prefill fills 512 symbols (two
// portions each, at most one value per portion) plus one more half-buffer of margin.
static constexpr uint32_t FSK_PROG_PREFILL_VALUES = 1024 + 512;

// Largest chunk is 65535 payload bytes = 128 blocks; a run has at most FSK_RUN_MAX_CHUNKS chunks.
static constexpr size_t FSK_PROG_MAX_CHUNK_BLOCKS = (65535 + FSK_PROG_BLOCK_BYTES - 1) / FSK_PROG_BLOCK_BYTES;
static constexpr size_t FSK_PROG_TABLE_MAX_BLOCKS = FSK_RUN_MAX_CHUNKS * FSK_PROG_MAX_CHUNK_BLOCKS;

// ---- publication primitives ----------------------------------------------------------------

FSK_PROG_INLINE uint32_t fsk_pub_load(const volatile uint32_t *p)
{
    return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}

FSK_PROG_INLINE void fsk_pub_store(volatile uint32_t *p, uint32_t v)
{
    __atomic_store_n(p, v, __ATOMIC_RELEASE);
}

// The three ways a progressive run can stop being fed, and only these:
//   FINAL     the run ended STRUCTURALLY: the file has no further chunk to join (end of file, a non-`fsk `
//             chunk, a chunk with a non-zero IRG, a truncated chunk, or the chunk cap). It is never the result
//             of a read that did not complete.
//   FAILED    the backing file / network could not deliver bytes the file structure says exist (a read
//             returned less than a declared payload or an existing header, a block could not be allocated).
//             Everything published stays valid; the cursor (chunk, chunk_bytes, file_pos) still names the
//             exact last confirmed byte, so the producer can be resumed (fsk_prog_loader_recover).
//   STOPPED   someone asked the producer to stop (unmount, reposition, free).
// UNDERRUN is not a producer state: it is what the CONSUMER reports when it catches a producer that is
// still LOADING (or is FAILED / STOPPED) - see FskProgNext.
enum class FskProgState : uint32_t
{
    LOADING = 0, // the producer is running
    FINAL   = 1, // the run is complete: pub_chunks / pub_values / next_offset will not change
    FAILED  = 2, // read or allocation failure: the published part stays valid
    STOPPED = 3  // stop requested (unmount, reposition, free)
};

// State shared between the producer and the consumers. The arrays are bound to storage owned by
// the caller (the sioCassette members the ISR already reads, or test fixtures); this module never
// allocates them.
struct FskProgRun
{
    // Bound storage.
    size_t   *value_counts  = nullptr; // [FSK_RUN_MAX_CHUNKS] values per chunk
    size_t   *block_base    = nullptr; // [FSK_RUN_MAX_CHUNKS] first block index of the chunk
    uint8_t **blocks        = nullptr; // block pointer table, block_capacity entries, zero-initialised
    size_t    block_capacity = 0;

    // Per-chunk metadata, written by the producer BEFORE the chunk is published.
    size_t   hdr_off[FSK_RUN_MAX_CHUNKS];             // file offset of the chunk header
    uint8_t  hdr[FSK_RUN_MAX_CHUNKS][8];              // raw 8-byte chunk header
    size_t   chunk_next[FSK_RUN_MAX_CHUNKS];          // structural offset after the chunk (0 = end of tape)
    size_t   chunk_avail[FSK_RUN_MAX_CHUNKS];         // clamped payload bytes of the chunk
    size_t   prefix[FSK_RUN_MAX_CHUNKS + 1];          // ordinal of the chunk's first value
    uint64_t chunk_ticks[FSK_RUN_MAX_CHUNKS];         // waveform ticks of the chunk; valid for c < pub_complete

    // Publication words (release by the producer, acquire by the readers).
    volatile uint32_t pub_chunks;   // chunk descriptors published
    volatile uint32_t pub_values;   // values published, as an ordinal count across the run
    volatile uint32_t pub_complete; // chunks whose payload is completely loaded (chunk_ticks valid)
    volatile uint32_t state;        // FskProgState
    volatile uint32_t stop_req;     // set by anyone: the producer must stop at its next step

    size_t next_offset;             // structural offset after the run; written before state = FINAL
};

// Resets `r` and binds its storage. The block table must be zero-initialised.
void fsk_prog_bind(FskProgRun &r, size_t *value_counts, size_t *block_base,
                   uint8_t **blocks, size_t block_capacity);

// Publishes chunk 0 (the header the walker already read). data_avail is the clamped payload size
// (fsk_compute_bounds). Must be called before the producer starts. Returns false if the table
// cannot hold the chunk.
bool fsk_prog_add_first_chunk(FskProgRun &r, size_t hdr_off, const uint8_t hdr8[8],
                              size_t data_avail, size_t next_offset);

FSK_PROG_INLINE FskProgState fsk_prog_state(const FskProgRun &r)
{
    return static_cast<FskProgState>(fsk_pub_load(&r.state));
}

// Blocks a chunk needs.
FSK_PROG_INLINE size_t fsk_prog_blocks_for(size_t data_avail)
{
    return (data_avail + FSK_PROG_BLOCK_BYTES - 1) / FSK_PROG_BLOCK_BYTES;
}

// Pointer-table entries for a run starting at `run_start` in a file of `filesize` bytes.
FSK_PROG_INLINE size_t fsk_prog_table_blocks(size_t filesize, size_t run_start)
{
    const size_t bytes = filesize > run_start ? filesize - run_start : 0;
    const size_t n = (bytes + FSK_PROG_BLOCK_BYTES - 1) / FSK_PROG_BLOCK_BYTES + FSK_RUN_MAX_CHUNKS + 1;
    return n < FSK_PROG_TABLE_MAX_BLOCKS ? n : FSK_PROG_TABLE_MAX_BLOCKS;
}

// Is this dispatch a candidate for progressive loading?
FSK_PROG_INLINE bool fsk_prog_wanted(bool enabled, bool header_complete, bool structurally_truncated,
                                     size_t first_chunk_data_avail)
{
    return enabled && header_complete && !structurally_truncated &&
           first_chunk_data_avail >= FSK_PROG_MIN_FIRST_CHUNK_BYTES;
}

// Index of the published chunk whose header is at `hdr_offset`, or SIZE_MAX.
size_t fsk_prog_find_chunk(const FskProgRun &r, size_t hdr_offset);

// ---- the producer --------------------------------------------------------------------------

// Positional sequential reader: read up to n bytes at file offset `file_off`, return the bytes read
// (0 on failure). The producer always asks for the offset right after the previous read, so an
// implementation over a cursor-based file needs no seek.
using fsk_prog_read_fn  = size_t (*)(void *ctx, size_t file_off, uint8_t *dst, size_t n);
// Allocates one block (PSRAM in production). nullptr = failure.
using fsk_prog_alloc_fn = uint8_t *(*)(void *ctx, size_t n);

struct FskProgLoader
{
    FskProgRun       *run = nullptr;
    fsk_prog_read_fn  read = nullptr;
    void             *read_ctx = nullptr;
    fsk_prog_alloc_fn alloc = nullptr;
    void             *alloc_ctx = nullptr;
    size_t            filesize = 0;

    // Cursor.
    enum Phase : uint8_t { PAYLOAD, HEADER, DONE } phase = PAYLOAD;
    size_t   chunk = 0;         // absolute index of the chunk being loaded
    size_t   chunk_bytes = 0;   // payload bytes of that chunk already read
    size_t   chunk_avail = 0;   // clamped payload bytes of that chunk
    size_t   file_pos = 0;      // file offset of the next sequential byte
    size_t   last_end = 0;      // file offset right after the previous read (a read elsewhere counts as a seek)
    size_t   values_done = 0;   // values of the chunk already accumulated into tick_acc
    uint64_t tick_acc = 0;

    // Read faults. A read that does not complete is retried by the caller (fsk_prog_step returns PROGRESS
    // again with the cursor untouched) up to `fault_budget` times in a row; a completed read restores the
    // budget. Only when it is spent does the run become FAILED.
    static constexpr uint32_t FAULT_BUDGET = 3;
    uint32_t fault_budget = FAULT_BUDGET;
    uint32_t faults = 0;        // read faults seen (each one was retried or ended in FAILED)

    // Statistics (read by the host tests: sequential access, read counts).
    uint32_t reads = 0;         // payload + header reads issued
    uint32_t seeks = 0;         // reads whose offset was NOT the byte after the previous read
    uint64_t bytes = 0;
};

// Prepares the producer for a run whose chunk 0 was published with fsk_prog_add_first_chunk().
void fsk_prog_loader_init(FskProgLoader &L, FskProgRun &run, fsk_prog_read_fn read, void *read_ctx,
                          fsk_prog_alloc_fn alloc, void *alloc_ctx, size_t filesize);

// Resumes a FAILED run from the exact byte it stopped at: state back to LOADING, fault budget restored, the
// cursor and everything published untouched. The caller starts the producer again.
void fsk_prog_loader_recover(FskProgLoader &L);

enum class FskProgStep : uint8_t { PROGRESS, DONE, FAILED, STOPPED };

// One unit of producer work: at most one block read, or one chunk header read + descriptor
// publication. DONE = the run is FINAL; FAILED / STOPPED leave the published part valid.
FskProgStep fsk_prog_step(FskProgLoader &L);

// ---- reading a file that can lose its place --------------------------------------------------
//
// A cursor-based file (stdio over a network filesystem) can lose its position on a transport failure: a
// remote READ carries no offset ("read from where the server is"), so a request whose reply is lost after the
// server executed it leaves the server ahead of the client, and the next sequential read returns bytes from
// the wrong place WITHOUT any error. stdio makes it worse: fseek() to the position it already believes it is
// at is answered without touching the file. Two rules follow:
//   * a read that comes back short before the end of the file is a FAULT, whatever it returned: the stream
//     position is unknown from then on;
//   * after a fault the position is re-established with a REAL absolute seek (to a far offset and back, so no
//     buffer window can absorb it), the whole request is read again, and the bytes the faulty attempt did
//     deliver must equal the first bytes of the re-read.
struct FskFileOps
{
    void  *ctx;
    long   (*tell)(void *ctx);                     // the stream's own idea of its position (ftell)
    bool   (*seek)(void *ctx, size_t off);         // fseek(off, SEEK_SET)
    size_t (*read)(void *ctx, uint8_t *dst, size_t n); // fread(dst, 1, n): may return less than n
    bool   (*stopping)(void *ctx);                 // true: stop retrying (unmount / free)
};

struct FskReadStats
{
    uint32_t faults = 0;   // reads that came back short before the end of the file (or failed to seek)
    uint32_t resyncs = 0;  // real absolute re-seeks performed
    uint32_t mismatches = 0; // re-reads whose overlap disagreed with what the faulty attempt had delivered
};

static constexpr unsigned FSK_PROG_READ_ATTEMPTS = 3;

// Reads exactly n bytes at `off`, or returns 0 (nothing usable). Returns fewer than n only at the true end of
// the file (off + result >= filesize). `suspect` is the caller's persistent "stream position unknown" flag:
// set by a fault (or by the caller after a restart), cleared by a read that completed.
size_t fsk_prog_resilient_read(const FskFileOps &f, size_t filesize, bool &suspect, FskReadStats &st,
                               size_t off, uint8_t *dst, size_t n, unsigned attempts = FSK_PROG_READ_ATTEMPTS);

// ---- consumers -----------------------------------------------------------------------------

// Per-chunk value counts limited to what is published: chunk c contributes
// min(value_counts[c], max(0, pub_values - prefix[c])) values, chunks not yet published nothing.
// Starts at chunk `first`; writes up to FSK_RUN_MAX_CHUNKS counts to `out`; returns their number.
size_t fsk_prog_pub_counts(const FskProgRun &r, size_t first, size_t *out);

// fsk_run_value_fn over a run view starting at chunk `first` (chunk index passed in is view-relative).
struct FskProgView
{
    const FskProgRun *run;
    size_t            first;
};
uint16_t fsk_prog_view_value(void *ctx, size_t chunk_index, size_t value_index);

// The start of a transmission, decided from what is published.
struct FskProgPlanIn
{
    uint64_t irg_us;        // leading IRG of the view's first chunk
    uint64_t q0_us;         // frozen position (0 for a fresh start)
    int64_t  elapsed_us;    // time since MOTOR ON (time base only)
    bool     timebase;      // fsk_timebase_applies() for this dispatch
    uint64_t min_runway_us; // FSK_PROG_MIN_RUNWAY_US unless a test / caller overrides it
    size_t   first_chunk;   // first chunk of the view (0 for a run started here, >0 after a rewind into a resident run)
};

struct FskProgPlan
{
    bool     ready = false;         // the waveform may start now (an all-MARK published prefix waits for FINAL, see has_space)
    bool     failed = false;        // the producer failed/stopped before the start condition was met
    bool     run_finished = false;  // the position is at/after the end of a FINAL run (nothing to play)
    bool     inert_unknown = false; // time base: the published prefix is too short to decide the inert length
    uint64_t tb_want = 0;
    uint64_t tb_inert = 0;
    uint64_t q_start = 0;           // logical start position (IRG included)
    uint64_t p0 = 0;                // waveform ticks already played at q_start
    uint64_t runway_us = 0;         // published waveform time ahead of the start position (capped at min_runway_us)
    size_t   seed_chunk = 0;        // view-relative, as cas_fsk_locate_ticks returns them
    size_t   seed_value = 0;
    uint64_t seed_skip = 0;
    uint64_t ahead_values = 0;      // published values after the seed
    bool     has_space = false;     // some even-indexed (LOW) value with time exists from the seed on, among what is published
};

FskProgPlan fsk_prog_plan_start(const FskProgRun &r, const FskProgPlanIn &in);

// Same as cas_fsk_inert_ticks(), over published counts, reporting whether the scan ran out of
// published values before it could decide (limit reached / first data LOW found).
uint64_t fsk_prog_inert_ticks(const size_t *counts, size_t chunks, fsk_run_value_fn reader, void *ctx,
                              uint64_t limit_ticks, uint64_t low_budget_ticks, bool &exhausted);

// ---- the encoder's view (runs in the RMT ISR) ------------------------------------------------

// fsk_block_le16() (fsk_plan.h) reads through the block table; the ISR uses this function and nothing
// else to fetch values, so it can never read an ordinal that is not published.
enum class FskProgNext : uint8_t
{
    VALUE,    // a non-zero value was produced
    END,      // the run is complete (FINAL and every value consumed)
    UNDERRUN  // the next value is not published yet (or the producer is dead): stop, never guess
};

// Is the cursor at the natural end of the run: no value left in its chunk, no further chunk, run FINAL.
FSK_PROG_INLINE bool fsk_prog_at_end(const FskProgRun &r, size_t chunk, size_t value_index, size_t value_count)
{
    if (value_index < value_count)
        return false;
    const uint32_t s = fsk_pub_load(&r.state);
    const uint32_t c = fsk_pub_load(&r.pub_chunks); // loaded AFTER the state: final if the state is FINAL
    return s == static_cast<uint32_t>(FskProgState::FINAL) && chunk + 1 >= c;
}

// Produces the next NON-ZERO value at the cursor (chunk, value_index, payload_pos, value_count), skipping
// zero-duration values (they consume a parity slot but no time) and stepping across chunk boundaries with
// the same per-chunk parity reset as the full-preload encoder. `level` is the parity of the produced
// value. Reads only ordinals < pub_values and chunks < pub_chunks (acquire): a block or descriptor is
// touched only after the release store that published it.
FSK_PROG_INLINE FskProgNext fsk_prog_next_value(const FskProgRun &r, const uint8_t *const *blocks, size_t blk,
                                                size_t &chunk, size_t &value_index, size_t &payload_pos,
                                                size_t &value_count, uint16_t &v, bool &level)
{
    for (;;)
    {
        if (value_index < value_count)
        {
            const uint32_t pub = fsk_pub_load(&r.pub_values);
            if (r.prefix[chunk] + value_index >= pub)
                return FskProgNext::UNDERRUN;
            v = fsk_block_le16(blocks, blk, payload_pos);
            level = fsk_level_for_index(value_index);
            ++value_index;
            payload_pos += 2;
            if (v != 0)
                return FskProgNext::VALUE;
            continue;
        }

        // Current chunk exhausted: the next chunk, the end of the run, or an underrun. The state is
        // sampled before pub_chunks, and re-sampled once when it is not FINAL, so a run that finishes
        // between the two loads is not mistaken for an underrun.
        uint32_t s = fsk_pub_load(&r.state);
        uint32_t c = fsk_pub_load(&r.pub_chunks);
        if (chunk + 1 >= c && s != static_cast<uint32_t>(FskProgState::FINAL))
        {
            s = fsk_pub_load(&r.state);
            c = fsk_pub_load(&r.pub_chunks);
            if (chunk + 1 >= c && s != static_cast<uint32_t>(FskProgState::FINAL))
                return FskProgNext::UNDERRUN;
        }
        if (chunk + 1 >= c)
            return FskProgNext::END;
        ++chunk;
        value_index = 0;
        value_count = r.value_counts[chunk];
        payload_pos = r.block_base[chunk] * blk; // chunks start on a block boundary
    }
}

// Waveform ticks of the values of chunk k published so far (a lower bound of its duration while it loads).
uint64_t fsk_prog_partial_ticks(const FskProgRun &r, size_t k);

// ---- rewind from the resident run ----------------------------------------------------------

// Resolves "back_us before the current position" to (R', Q') from the chunk metadata published so
// far, WITHOUT reading the file: the same answer cas_resolve_target_time() gives for the same image.
// `walk0` is the walk state at the run's first chunk (time_us = tape time there). Returns false when
// the request cannot be answered from the published data (position or target outside the run, chunk
// durations not yet complete); the caller then uses the file-based resolution.
bool fsk_prog_resolve_rewind(const FskProgRun &r, const CassetteWalkState &walk0, size_t tape_offset,
                             bool pos_valid, uint64_t pos_us, uint64_t back_us,
                             CassetteTargetResolution &out);

// Tape time at the start of published chunk k (needs chunks 0..k-1 complete); false otherwise.
bool fsk_prog_chunk_time(const FskProgRun &r, const CassetteWalkState &walk0, size_t k, uint64_t &time_us);

#undef FSK_PROG_INLINE

#endif // FSK_PROGRESSIVE_H
