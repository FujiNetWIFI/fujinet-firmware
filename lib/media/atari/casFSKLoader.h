#ifndef CAS_FSK_LOADER_H
#define CAS_FSK_LOADER_H

// casFSKLoader.h — progressive load of one A8CAS `fsk ` run: a producer fills payload blocks and
// publishes the count of complete values, and the RMT refill plays only what is published.
//
// The count and the state are release stores read with acquire loads, so a block is visible once its
// values are counted, and a published block is never written again. Chunk payloads are even and packed
// back to back, so a value never straddles a block. Reads and allocation are injected: no ESP-IDF here.

#include "casFSK.h"

// Payload block size. One block is one read request, so a faulty read is repeated from its start.
constexpr size_t FSK_LOADER_BLOCK_BYTES = 512;

// Runs whose first chunk is smaller are loaded whole by the basic path.
constexpr size_t FSK_PROGRESSIVE_MIN_FIRST_CHUNK_BYTES = 16384;

// Waveform time that must be published before the tape starts, measured from the durations of the
// published values, not their bytes.
constexpr uint64_t FSK_RUNWAY_MIN_US = 20ULL * 1000000ULL;

// Values the RMT encoder may take before its first refill: 512 symbols of two portions, plus a half
// buffer of margin.
constexpr uint32_t FSK_PREFILL_VALUES = 1024 + 512;

// Largest chunk is 65535 payload bytes; a run has at most FSK_RUN_MAX_CHUNKS chunks.
constexpr size_t FSK_LOADER_MAX_TABLE_BLOCKS =
    FSK_RUN_MAX_CHUNKS * ((65535 + FSK_LOADER_BLOCK_BYTES - 1) / FSK_LOADER_BLOCK_BYTES);

// Block-table entries for a run starting at `run_start` in a file of `filesize` bytes.
constexpr size_t fsk_loader_table_blocks(size_t filesize, size_t run_start)
{
    const size_t bytes = filesize > run_start ? filesize - run_start : 0;
    const size_t n = (bytes + FSK_LOADER_BLOCK_BYTES - 1) / FSK_LOADER_BLOCK_BYTES + 1;
    return n < FSK_LOADER_MAX_TABLE_BLOCKS ? n : FSK_LOADER_MAX_TABLE_BLOCKS;
}

// ---- publication primitives ----------------------------------------------------------------

FSK_INLINE uint32_t fsk_pub_load(const volatile uint32_t *p)
{
    return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}

FSK_INLINE void fsk_pub_store(volatile uint32_t *p, uint32_t v)
{
    __atomic_store_n(p, v, __ATOMIC_RELEASE);
}

// The only ways a run stops being fed:
//   FINAL    the run ended structurally (end of file, another chunk type, a chunk with its own gap,
//            a malformed chunk or the chunk cap); never the result of a read that did not complete
//   FAILED   bytes the file structure says exist could not be read, or a block could not be
//            allocated; everything published stays valid and the cursor names the last confirmed byte
//   STOPPED  somebody asked the producer to stop
enum class FSKLoaderState : uint32_t
{
    loading = 0,
    final_run = 1,
    failed = 2,
    stopped = 3,
};

// State shared by the producer and the consumer. `blocks` is bound to storage owned by the caller.
struct FSKProgressiveRun
{
    uint8_t **blocks = nullptr;
    size_t    block_capacity = 0;

    volatile uint32_t pub_values = 0; // complete values published, counted across the whole run
    volatile uint32_t state = static_cast<uint32_t>(FSKLoaderState::loading);
    volatile uint32_t stop_req = 0;   // set by anyone: the producer stops at its next step

    // Written by the producer before it publishes final_run.
    size_t   next_offset = 0; // file offset after the run
    uint64_t total_ticks = 0; // waveform duration of the whole run
};

// Binds `r` to a zero-initialised block table and resets it.
void fsk_run_bind(FSKProgressiveRun &r, uint8_t **blocks, size_t block_capacity);

FSK_INLINE FSKLoaderState fsk_run_state(const FSKProgressiveRun &r)
{
    return static_cast<FSKLoaderState>(fsk_pub_load(&r.state));
}

// Does a run whose first chunk has `first_chunk_bytes` of payload go to the progressive loader?
constexpr bool fsk_progressive_wanted(size_t first_chunk_bytes)
{
    return first_chunk_bytes >= FSK_PROGRESSIVE_MIN_FIRST_CHUNK_BYTES;
}

// ---- the producer --------------------------------------------------------------------------

// Allocates one payload block (PSRAM in firmware). nullptr = failure.
using FSKAllocFn = uint8_t *(*)(void *ctx, size_t n);

enum class FSKLoaderPhase : uint8_t { payload, header, done };

struct FSKRunLoader
{
    FSKProgressiveRun *run = nullptr;
    FSKReadFn          read = nullptr; // always asked for the offset right after its previous read
    void              *read_ctx = nullptr;
    FSKAllocFn         alloc = nullptr;
    void              *alloc_ctx = nullptr;
    size_t             filesize = 0;

    // Confirmed cursor: advances only past bytes that were read completely.
    FSKLoaderPhase phase = FSKLoaderPhase::payload;
    size_t chunks = 0;       // chunks joined so far
    size_t chunk_end = 0;    // file offset after the chunk being loaded
    size_t chunk_avail = 0;  // payload bytes of that chunk
    size_t chunk_bytes = 0;  // of which already read
    size_t bytes_loaded = 0; // payload bytes of the run read so far
    size_t file_pos = 0;     // file offset of the next sequential byte
    size_t last_end = 0;     // file offset right after the previous read
    size_t values_done = 0;  // values already added to total_ticks

    // A read that does not complete is retried (step returns progress with the cursor untouched) while
    // the budget lasts; a completed read restores it. Spent: the run is failed.
    static constexpr uint32_t FAULT_BUDGET = 3;
    uint32_t fault_budget = FAULT_BUDGET;
    uint32_t faults = 0;

    uint32_t reads = 0; // reads issued
    uint32_t seeks = 0; // reads whose offset was not the byte after the previous read
};

// Prepares the producer for the run whose first chunk header is at `header_offset`, `first_length`
// payload bytes long (already checked with fsk_check_chunk). `run` must be bound.
void fsk_loader_init(FSKRunLoader &L, FSKProgressiveRun &run, FSKReadFn read, void *read_ctx,
                     FSKAllocFn alloc, void *alloc_ctx, size_t filesize, size_t header_offset,
                     uint16_t first_length);

// Resumes a failed run from the confirmed cursor: state back to loading, fault budget restored,
// everything published untouched. The caller starts the producer again.
void fsk_loader_recover(FSKRunLoader &L);

enum class FSKStep : uint8_t { progress, done, failed, stopped };

// One unit of producer work: at most one block read, or one chunk header read. done = the run is
// final; failed and stopped leave the published part valid.
FSKStep fsk_loader_step(FSKRunLoader &L);

// ---- the consumer --------------------------------------------------------------------------

FSK_INLINE uint16_t fsk_run_value(const FSKProgressiveRun &r, size_t value_index)
{
    const size_t pos = value_index * 2;
    return fsk_decode_le16(r.blocks[pos / FSK_LOADER_BLOCK_BYTES] + pos % FSK_LOADER_BLOCK_BYTES);
}

// Value source over a run being loaded; runs in the RMT refill callback. Reads nothing that is not
// published.
struct FSKPublishedValues
{
    const FSKProgressiveRun *run;

    FSK_INLINE FSKNext next(size_t value_index, uint16_t &value) const
    {
        uint32_t published = fsk_pub_load(&run->pub_values);
        if (value_index >= published)
        {
            // Values are published before the state, so once final_run is seen the count is final.
            if (fsk_pub_load(&run->state) != static_cast<uint32_t>(FSKLoaderState::final_run))
                return FSKNext::underrun;
            published = fsk_pub_load(&run->pub_values);
            if (value_index >= published)
                return FSKNext::end;
        }
        value = fsk_run_value(*run, value_index);
        return FSKNext::value;
    }
};

// ---- the start gate ------------------------------------------------------------------------

enum class FSKStartGate : uint8_t
{
    waiting, // not enough published yet
    ready,   // the waveform may start
    failed,  // the producer failed or was stopped before the start condition was met
};

// Ready once the published values cover `min_runway_us` of waveform time and the encoder's prefill,
// or the run is final.
FSKStartGate fsk_run_start_gate(const FSKProgressiveRun &run, uint64_t min_runway_us = FSK_RUNWAY_MIN_US);

#endif // CAS_FSK_LOADER_H
