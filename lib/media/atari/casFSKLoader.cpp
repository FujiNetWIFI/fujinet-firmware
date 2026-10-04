#include "casFSKLoader.h"

#include "global_types.h"

namespace
{

FSKStep fail(FSKProgressiveRun &r, FSKLoaderState st)
{
    fsk_pub_store(&r.state, static_cast<uint32_t>(st));
    return st == FSKLoaderState::stopped ? FSKStep::stopped : FSKStep::failed;
}

FSKStep finish(FSKRunLoader &L, size_t next_offset)
{
    FSKProgressiveRun &r = *L.run;
    r.next_offset = next_offset;
    L.phase = FSKLoaderPhase::done;
    fsk_pub_store(&r.state, static_cast<uint32_t>(FSKLoaderState::final_run));
    return FSKStep::done;
}

// A read that did not complete: retried with the cursor untouched while the budget lasts, then failed.
// Never final: the file structure said these bytes exist.
FSKStep read_fault(FSKRunLoader &L)
{
    ++L.faults;
    if (L.fault_budget > 0)
    {
        --L.fault_budget;
        return FSKStep::progress;
    }
    return fail(*L.run, FSKLoaderState::failed);
}

// Sequential read that accumulates short reads; a zero-length read is a failure.
success_is_true read_exact(FSKRunLoader &L, uint8_t *dst, size_t n)
{
    size_t got = 0;
    while (got < n)
    {
        const size_t off = L.file_pos + got;
        const size_t r = L.read(L.read_ctx, off, dst + got, n - got);
        ++L.reads;
        if (off != L.last_end)
            ++L.seeks;
        if (r == 0)
            RETURN_ERROR_AS_FALSE();
        L.last_end = off + r;
        got += r;
    }
    L.file_pos += n;
    RETURN_SUCCESS_AS_TRUE();
}

size_t blocks_for(size_t bytes)
{
    return (bytes + FSK_LOADER_BLOCK_BYTES - 1) / FSK_LOADER_BLOCK_BYTES;
}

} // namespace

void fsk_run_bind(FSKProgressiveRun &r, uint8_t **blocks, size_t block_capacity)
{
    r.blocks = blocks;
    r.block_capacity = block_capacity;
    r.pub_values = 0;
    r.state = static_cast<uint32_t>(FSKLoaderState::loading);
    r.stop_req = 0;
    r.next_offset = 0;
    r.total_ticks = 0;
}

void fsk_loader_init(FSKRunLoader &L, FSKProgressiveRun &run, FSKReadFn read, void *read_ctx,
                     FSKAllocFn alloc, void *alloc_ctx, size_t filesize, size_t header_offset,
                     uint16_t first_length)
{
    L = FSKRunLoader{};
    L.run = &run;
    L.read = read;
    L.read_ctx = read_ctx;
    L.alloc = alloc;
    L.alloc_ctx = alloc_ctx;
    L.filesize = filesize;
    L.chunks = 1;
    L.chunk_end = header_offset + FSK_CHUNK_HEADER_BYTES + first_length;
    L.chunk_avail = first_length;
    L.file_pos = header_offset + FSK_CHUNK_HEADER_BYTES;
    L.last_end = L.file_pos;
}

void fsk_loader_recover(FSKRunLoader &L)
{
    L.fault_budget = FSKRunLoader::FAULT_BUDGET;
    L.last_end = SIZE_MAX; // the caller re-establishes the position, so the next read counts as a seek
    fsk_pub_store(&L.run->stop_req, 0);
    fsk_pub_store(&L.run->state, static_cast<uint32_t>(FSKLoaderState::loading));
}

FSKStep fsk_loader_step(FSKRunLoader &L)
{
    FSKProgressiveRun &r = *L.run;

    if (fsk_pub_load(&r.stop_req) != 0)
        return fail(r, FSKLoaderState::stopped);
    if (L.phase == FSKLoaderPhase::done)
        return FSKStep::done;

    if (L.phase == FSKLoaderPhase::payload)
    {
        if (L.chunk_bytes >= L.chunk_avail)
        {
            L.phase = FSKLoaderPhase::header;
            return FSKStep::progress;
        }

        // Read up to the end of the current block, so a published block is complete.
        const size_t in_block = L.bytes_loaded % FSK_LOADER_BLOCK_BYTES;
        const size_t room = FSK_LOADER_BLOCK_BYTES - in_block;
        const size_t remaining = L.chunk_avail - L.chunk_bytes;
        const size_t n = remaining < room ? remaining : room;
        const size_t blk = L.bytes_loaded / FSK_LOADER_BLOCK_BYTES;
        if (blk >= r.block_capacity)
            return fail(r, FSKLoaderState::failed);
        if (r.blocks[blk] == nullptr)
        {
            uint8_t *p = L.alloc(L.alloc_ctx, FSK_LOADER_BLOCK_BYTES);
            if (p == nullptr)
                return fail(r, FSKLoaderState::failed);
            r.blocks[blk] = p; // unpublished until pub_values covers it
        }
        if (read_exact(L, r.blocks[blk] + in_block, n).is_error())
            return read_fault(L);
        L.fault_budget = FSKRunLoader::FAULT_BUDGET;
        L.chunk_bytes += n;
        L.bytes_loaded += n;

        const size_t complete = L.bytes_loaded / 2;
        for (size_t v = L.values_done; v < complete; ++v)
            r.total_ticks += fsk_ticks_for_value(fsk_run_value(r, v));
        L.values_done = complete;

        fsk_pub_store(&r.pub_values, static_cast<uint32_t>(complete));
        return FSKStep::progress;
    }

    // Header of the next chunk: the bytes right after this payload.
    if (L.chunks >= FSK_RUN_MAX_CHUNKS || L.chunk_end + FSK_CHUNK_HEADER_BYTES > L.filesize)
        return finish(L, L.chunk_end);

    // A complete header exists, so failing to read it is a fault, never the end of the run.
    uint8_t raw[FSK_CHUNK_HEADER_BYTES];
    if (read_exact(L, raw, sizeof(raw)).is_error())
        return read_fault(L);
    L.fault_budget = FSKRunLoader::FAULT_BUDGET;

    const FSKChunkHeader h = fsk_parse_header(raw);
    if (!fsk_chunk_joins(fsk_check_chunk(h, L.chunk_end, L.filesize), h))
        return finish(L, L.chunk_end);
    if (blocks_for(L.bytes_loaded + h.length) > r.block_capacity)
        return fail(r, FSKLoaderState::failed);

    L.chunk_end += FSK_CHUNK_HEADER_BYTES + h.length;
    L.chunk_avail = h.length;
    L.chunk_bytes = 0;
    ++L.chunks;
    L.phase = FSKLoaderPhase::payload;
    return FSKStep::progress;
}

FSKStartGate fsk_run_start_gate(const FSKProgressiveRun &run, uint64_t min_runway_us)
{
    // State first: once final_run is seen, pub_values is final.
    const FSKLoaderState st = fsk_run_state(run);
    const bool final_run = st == FSKLoaderState::final_run;
    const uint32_t published = fsk_pub_load(&run.pub_values);

    uint64_t runway = 0;
    for (uint32_t i = 0; i < published && runway < min_runway_us; ++i)
        runway += fsk_ticks_for_value(fsk_run_value(run, i));

    const bool runway_ok = runway >= min_runway_us || final_run;
    const bool prefill_ok = published >= FSK_PREFILL_VALUES || final_run;
    if (runway_ok && prefill_ok)
        return FSKStartGate::ready;

    const bool dead = st == FSKLoaderState::failed || st == FSKLoaderState::stopped;
    return dead ? FSKStartGate::failed : FSKStartGate::waiting;
}
