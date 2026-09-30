#include "fsk_progressive.h"

#include <cstring>

// See fsk_progressive.h for the producer / consumer contract.

namespace
{
    // Raw chunk-header fields ("fsk " + length + irg), same layout as struct tape_FUJI_hdr.
    inline bool hdr_is_fsk(const uint8_t *h)
    {
        return h[0] == 'f' && h[1] == 's' && h[2] == 'k' && h[3] == ' ';
    }
    inline uint16_t hdr_len(const uint8_t *h) { return static_cast<uint16_t>(h[4] | (h[5] << 8)); }
    inline uint16_t hdr_irg(const uint8_t *h) { return static_cast<uint16_t>(h[6] | (h[7] << 8)); }

    // Producer: publish failure / stop, leaving everything already published valid.
    FskProgStep fail(FskProgRun &r, FskProgState st)
    {
        fsk_pub_store(&r.state, static_cast<uint32_t>(st));
        return st == FskProgState::STOPPED ? FskProgStep::STOPPED : FskProgStep::FAILED;
    }

    FskProgStep finish(FskProgLoader &L, size_t next_offset)
    {
        FskProgRun &r = *L.run;
        r.next_offset = next_offset;
        L.phase = FskProgLoader::DONE;
        fsk_pub_store(&r.state, static_cast<uint32_t>(FskProgState::FINAL));
        return FskProgStep::DONE;
    }

    // A read that did not complete. It is retried (cursor untouched) while the fault budget lasts, then FAILED.
    // Never FINAL: the file structure said these bytes exist.
    FskProgStep read_fault(FskProgLoader &L)
    {
        ++L.faults;
        if (L.fault_budget > 0)
        {
            --L.fault_budget;
            return FskProgStep::PROGRESS;
        }
        return fail(*L.run, FskProgState::FAILED);
    }

    // Sequential read that accumulates short reads; a zero-length read is a failure.
    bool read_exact(FskProgLoader &L, uint8_t *dst, size_t n)
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
                return false;
            L.last_end = off + r;
            got += r;
        }
        L.file_pos += n;
        L.bytes += n;
        return true;
    }
} // namespace

void fsk_prog_bind(FskProgRun &r, size_t *value_counts, size_t *block_base,
                   uint8_t **blocks, size_t block_capacity)
{
    r.value_counts = value_counts;
    r.block_base = block_base;
    r.blocks = blocks;
    r.block_capacity = block_capacity;
    for (size_t i = 0; i < FSK_RUN_MAX_CHUNKS; ++i)
    {
        value_counts[i] = 0;
        block_base[i] = 0;
        r.hdr_off[i] = 0;
        std::memset(r.hdr[i], 0, sizeof(r.hdr[i]));
        r.chunk_next[i] = 0;
        r.chunk_avail[i] = 0;
        r.chunk_ticks[i] = 0;
    }
    for (size_t i = 0; i <= FSK_RUN_MAX_CHUNKS; ++i)
        r.prefix[i] = 0;
    r.pub_chunks = 0;
    r.pub_values = 0;
    r.pub_complete = 0;
    r.state = static_cast<uint32_t>(FskProgState::LOADING);
    r.stop_req = 0;
    r.next_offset = 0;
}

bool fsk_prog_add_first_chunk(FskProgRun &r, size_t hdr_off, const uint8_t hdr8[8],
                              size_t data_avail, size_t next_offset)
{
    if (r.value_counts == nullptr || r.blocks == nullptr)
        return false;
    if (fsk_prog_blocks_for(data_avail) > r.block_capacity)
        return false;
    r.hdr_off[0] = hdr_off;
    std::memcpy(r.hdr[0], hdr8, 8);
    r.chunk_next[0] = next_offset;
    r.chunk_avail[0] = data_avail;
    r.value_counts[0] = fsk_value_count(data_avail);
    r.block_base[0] = 0;
    r.prefix[0] = 0;
    r.prefix[1] = r.value_counts[0];
    r.pub_values = 0;
    fsk_pub_store(&r.pub_chunks, 1);
    return true;
}

size_t fsk_prog_find_chunk(const FskProgRun &r, size_t hdr_offset)
{
    const uint32_t c = fsk_pub_load(&r.pub_chunks);
    for (size_t i = 0; i < c; ++i)
        if (r.hdr_off[i] == hdr_offset)
            return i;
    return SIZE_MAX;
}

void fsk_prog_loader_init(FskProgLoader &L, FskProgRun &run, fsk_prog_read_fn read, void *read_ctx,
                          fsk_prog_alloc_fn alloc, void *alloc_ctx, size_t filesize)
{
    L = FskProgLoader{};
    L.run = &run;
    L.read = read;
    L.read_ctx = read_ctx;
    L.alloc = alloc;
    L.alloc_ctx = alloc_ctx;
    L.filesize = filesize;
    L.phase = FskProgLoader::PAYLOAD;
    L.chunk = 0;
    L.chunk_bytes = 0;
    L.chunk_avail = run.chunk_avail[0];
    L.file_pos = run.hdr_off[0] + 8;
    L.last_end = L.file_pos;
    L.values_done = 0;
    L.tick_acc = 0;
}

void fsk_prog_loader_recover(FskProgLoader &L)
{
    L.fault_budget = FskProgLoader::FAULT_BUDGET;
    L.last_end = SIZE_MAX; // the next read counts as a seek: the caller re-establishes the position
    fsk_pub_store(&L.run->stop_req, 0);
    fsk_pub_store(&L.run->state, static_cast<uint32_t>(FskProgState::LOADING));
}

size_t fsk_prog_resilient_read(const FskFileOps &f, size_t filesize, bool &suspect, FskReadStats &st,
                               size_t off, uint8_t *dst, size_t n, unsigned attempts)
{
    uint8_t kept[64];
    size_t kept_n = 0;
    for (unsigned a = 0; a < attempts; ++a)
    {
        if (suspect)
        {
            // Real absolute re-seek: away (outside any buffer window) and back.
            const size_t away = off >= 1024 ? 0 : off + 1024;
            (void)f.seek(f.ctx, away);
            ++st.resyncs;
            if (!f.seek(f.ctx, off))
            {
                ++st.faults;
                if (f.stopping(f.ctx))
                    return 0;
                continue;
            }
        }
        else if (f.tell(f.ctx) != static_cast<long>(off))
        {
            if (!f.seek(f.ctx, off))
            {
                suspect = true;
                ++st.faults;
                continue;
            }
        }

        const size_t r = f.read(f.ctx, dst, n);
        if (r == n)
        {
            if (kept_n != 0 && std::memcmp(kept, dst, kept_n) != 0)
            {
                // The position is proven wrong or the data unstable: hand nothing over.
                ++st.mismatches;
                suspect = true;
                return 0;
            }
            suspect = false;
            return n;
        }
        if (off + r >= filesize)
        {
            suspect = false;
            return r; // the true end of the file
        }

        // Short before the end of the file: a transport fault. The bytes it did deliver are kept only to
        // be compared with the re-read.
        ++st.faults;
        suspect = true;
        if (kept_n == 0 && r > 0)
        {
            kept_n = r < sizeof(kept) ? r : sizeof(kept);
            std::memcpy(kept, dst, kept_n);
        }
        if (f.stopping(f.ctx))
            return 0;
    }
    return 0;
}

FskProgStep fsk_prog_step(FskProgLoader &L)
{
    FskProgRun &r = *L.run;

    if (fsk_pub_load(&r.stop_req) != 0)
        return fail(r, FskProgState::STOPPED);
    if (L.phase == FskProgLoader::DONE)
        return FskProgStep::DONE;

    if (L.phase == FskProgLoader::PAYLOAD)
    {
        if (L.chunk_bytes < L.chunk_avail)
        {
            const size_t remaining = L.chunk_avail - L.chunk_bytes;
            const size_t n = remaining < FSK_PROG_BLOCK_BYTES ? remaining : FSK_PROG_BLOCK_BYTES;
            const size_t blk = r.block_base[L.chunk] + L.chunk_bytes / FSK_PROG_BLOCK_BYTES;
            if (blk >= r.block_capacity)
                return fail(r, FskProgState::FAILED);
            if (r.blocks[blk] == nullptr)
            {
                uint8_t *p = L.alloc(L.alloc_ctx, FSK_PROG_BLOCK_BYTES);
                if (p == nullptr)
                    return fail(r, FskProgState::FAILED);
                r.blocks[blk] = p; // unpublished: visible to readers with the pub_values store below
            }
            if (!read_exact(L, r.blocks[blk], n))
                return read_fault(L); // the block is re-read from its first byte; nothing was published
            L.fault_budget = FskProgLoader::FAULT_BUDGET;
            L.chunk_bytes += n;

            // Values that are now complete (a trailing odd byte never forms one).
            const size_t complete = L.chunk_bytes / 2;
            const uint8_t *const *tbl = reinterpret_cast<const uint8_t *const *>(r.blocks);
            const size_t base_pos = r.block_base[L.chunk] * FSK_PROG_BLOCK_BYTES;
            for (size_t v = L.values_done; v < complete; ++v)
                L.tick_acc += fsk_ticks_for_value(fsk_block_le16(tbl, FSK_PROG_BLOCK_BYTES, base_pos + v * 2));
            L.values_done = complete;

            fsk_pub_store(&r.pub_values, static_cast<uint32_t>(r.prefix[L.chunk] + complete));
            return FskProgStep::PROGRESS;
        }

        // Chunk complete: its exact duration becomes visible, then the next header is due.
        r.chunk_ticks[L.chunk] = L.tick_acc;
        fsk_pub_store(&r.pub_complete, static_cast<uint32_t>(L.chunk + 1));
        L.phase = FskProgLoader::HEADER;
        return FskProgStep::PROGRESS;
    }

    // HEADER: the next chunk of the run, read sequentially (the byte right after this payload).
    const size_t c = L.chunk;
    const size_t next_off = r.chunk_next[c];
    if (c + 1 >= FSK_RUN_MAX_CHUNKS || next_off == 0)
        return finish(L, next_off); // run capped / end of tape: the run ends here

    // No complete header can exist at next_off: end of file, the run ends here (the walker's EOT rule).
    if (next_off + 8 > L.filesize)
        return finish(L, next_off);

    // A header structurally exists. A read that does not deliver it is a FAULT, never the end of the run.
    uint8_t h[8];
    if (!read_exact(L, h, sizeof(h)))
        return read_fault(L);
    L.fault_budget = FskProgLoader::FAULT_BUDGET;

    const FskBounds cb = fsk_compute_bounds(L.filesize, next_off, hdr_len(h));
    if (!fsk_run_should_join(hdr_is_fsk(h), hdr_irg(h), cb.header_complete, cb.structurally_truncated))
        return finish(L, next_off); // non-FSK / IRG > 0 / truncated: the run ends before it

    const size_t nc = c + 1;
    const size_t base = r.block_base[c] + fsk_prog_blocks_for(r.chunk_avail[c]);
    if (base + fsk_prog_blocks_for(cb.data_avail) > r.block_capacity)
        return fail(r, FskProgState::FAILED);

    // Descriptor first, then publication.
    r.hdr_off[nc] = next_off;
    std::memcpy(r.hdr[nc], h, 8);
    r.chunk_next[nc] = cb.next_offset;
    r.chunk_avail[nc] = cb.data_avail;
    r.value_counts[nc] = cb.value_count;
    r.block_base[nc] = base;
    r.prefix[nc] = r.prefix[c] + r.value_counts[c];
    r.prefix[nc + 1] = r.prefix[nc] + r.value_counts[nc];
    fsk_pub_store(&r.pub_chunks, static_cast<uint32_t>(nc + 1));

    L.chunk = nc;
    L.chunk_bytes = 0;
    L.chunk_avail = cb.data_avail;
    L.values_done = 0;
    L.tick_acc = 0;
    L.phase = FskProgLoader::PAYLOAD;
    return FskProgStep::PROGRESS;
}

size_t fsk_prog_pub_counts(const FskProgRun &r, size_t first, size_t *out)
{
    // Order matters: pub_values first, then pub_chunks. Chunks are published before the values that
    // live in them, so every value counted in `v` belongs to a chunk covered by `c`.
    const uint32_t v = fsk_pub_load(&r.pub_values);
    const uint32_t c = fsk_pub_load(&r.pub_chunks);
    size_t n = 0;
    for (size_t i = first; i < c && n < FSK_RUN_MAX_CHUNKS; ++i)
    {
        const size_t vc = r.value_counts[i];
        const size_t avail = v > r.prefix[i] ? static_cast<size_t>(v) - r.prefix[i] : 0;
        out[n++] = avail < vc ? avail : vc;
    }
    return n;
}

uint16_t fsk_prog_view_value(void *ctx, size_t chunk_index, size_t value_index)
{
    const FskProgView *view = static_cast<const FskProgView *>(ctx);
    const FskProgRun &r = *view->run;
    const size_t base_pos = r.block_base[view->first + chunk_index] * FSK_PROG_BLOCK_BYTES;
    return fsk_block_le16(reinterpret_cast<const uint8_t *const *>(r.blocks), FSK_PROG_BLOCK_BYTES,
                          base_pos + value_index * 2);
}

uint64_t fsk_prog_inert_ticks(const size_t *counts, size_t chunks, fsk_run_value_fn reader, void *ctx,
                              uint64_t limit_ticks, uint64_t low_budget_ticks, bool &exhausted)
{
    exhausted = false;
    uint64_t t = 0;   // waveform ticks scanned so far
    uint64_t low = 0; // LOW ticks scanned so far
    for (size_t c = 0; c < chunks; ++c)
    {
        for (size_t v = 0; v < counts[c]; ++v)
        {
            if (t >= limit_ticks)
                return t;
            const uint64_t d = fsk_ticks_for_value(reader(ctx, c, v));
            if (d == 0)
                continue;
            if (!fsk_level_for_index(v)) // even index = LOW
            {
                if (low + d > low_budget_ticks)
                    return t; // a LOW that may be data starts here
                low += d;
            }
            t += d;
        }
    }
    exhausted = true; // ran out of published values before deciding
    return t;
}

FskProgPlan fsk_prog_plan_start(const FskProgRun &r, const FskProgPlanIn &in)
{
    FskProgPlan p;
    const FskProgState st = fsk_prog_state(r);
    const bool final_run = (st == FskProgState::FINAL);
    const bool dead = (st == FskProgState::FAILED || st == FskProgState::STOPPED);

    size_t counts[FSK_RUN_MAX_CHUNKS];
    const size_t n = fsk_prog_pub_counts(r, in.first_chunk, counts);
    if (n == 0)
    {
        p.failed = dead;
        return p;
    }
    FskProgView view{ &r, in.first_chunk };

    uint64_t q_start = in.q0_us;
    if (in.timebase)
    {
        p.tb_want = fsk_timebase_wave_want(in.irg_us, in.q0_us, in.elapsed_us);
        if (p.tb_want > 0)
        {
            bool exhausted = false;
            p.tb_inert = fsk_prog_inert_ticks(counts, n, fsk_prog_view_value, &view, p.tb_want,
                                              FSK_TIMEBASE_LOW_BUDGET_US, exhausted);
            p.inert_unknown = exhausted && !final_run;
        }
        q_start = fsk_timebase_q(in.irg_us, in.q0_us, in.elapsed_us, p.tb_inert);
    }
    p.q_start = q_start;
    p.p0 = cas_run_pos_split(in.irg_us, q_start).wave_ticks;
    if (p.inert_unknown)
    {
        p.failed = dead;
        return p;
    }

    if (p.p0 > 0)
    {
        const FskLocateResult loc = cas_fsk_locate_ticks(counts, n, fsk_prog_view_value, &view, p.p0);
        if (loc.at_end)
        {
            if (final_run)
            {
                p.run_finished = true; // the position is past the end of the whole run
                p.ready = true;
            }
            else
            {
                p.failed = dead; // not published yet
            }
            return p;
        }
        p.seed_chunk = loc.chunk_index;
        p.seed_value = loc.value_index;
        p.seed_skip = loc.skip_ticks;
    }

    // Waveform time published ahead of the seed.
    uint64_t runway = 0;
    bool first = true;
    for (size_t c = p.seed_chunk; c < n && runway < in.min_runway_us; ++c)
    {
        for (size_t v = (c == p.seed_chunk ? p.seed_value : 0); v < counts[c] && runway < in.min_runway_us; ++v)
        {
            uint64_t t = fsk_ticks_for_value(fsk_prog_view_value(&view, c, v));
            if (first)
            {
                t = t > p.seed_skip ? t - p.seed_skip : 0;
                first = false;
            }
            runway += t;
        }
    }
    p.runway_us = runway;

    const uint32_t V = fsk_pub_load(&r.pub_values);
    const uint64_t seed_ordinal = r.prefix[in.first_chunk + p.seed_chunk] + p.seed_value + (p.seed_skip > 0 ? 1 : 0);
    p.ahead_values = V > seed_ordinal ? V - seed_ordinal : 0;

    // Does the published rest of the run carry any LOW time? The full-preload path plays a run WITHOUT LOW
    // time (all MARK) without an RMT lifecycle (its channel creation / teardown can drive LOW). That
    // classification needs the whole run, so a run whose published part is all MARK is not started
    // until it is FINAL, exactly like the full-preload path; the caller then classifies it identically.
    {
        size_t abs_counts[FSK_RUN_MAX_CHUNKS];
        const size_t na = fsk_prog_pub_counts(r, 0, abs_counts);
        const size_t sc = in.first_chunk + p.seed_chunk;
        p.has_space = sc < na && fsk_run_summarize_from(reinterpret_cast<const uint8_t *const *>(r.blocks),
                                                        FSK_PROG_BLOCK_BYTES, r.block_base, abs_counts, na, sc,
                                                        p.seed_value, p.seed_skip)
                                     .has_space;
    }

    const bool runway_ok = runway >= in.min_runway_us || final_run;
    const bool prefill_ok = p.ahead_values >= FSK_PROG_PREFILL_VALUES || final_run;
    p.ready = runway_ok && prefill_ok && (p.has_space || final_run);
    p.failed = !p.ready && dead;
    return p;
}

uint64_t fsk_prog_partial_ticks(const FskProgRun &r, size_t k)
{
    const uint32_t v = fsk_pub_load(&r.pub_values);
    const uint32_t c = fsk_pub_load(&r.pub_chunks);
    if (k >= c)
        return 0;
    const size_t avail = v > r.prefix[k] ? static_cast<size_t>(v) - r.prefix[k] : 0;
    const size_t n = avail < r.value_counts[k] ? avail : r.value_counts[k];
    const uint8_t *const *tbl = reinterpret_cast<const uint8_t *const *>(r.blocks);
    const size_t base_pos = r.block_base[k] * FSK_PROG_BLOCK_BYTES;
    uint64_t t = 0;
    for (size_t i = 0; i < n; ++i)
        t += fsk_ticks_for_value(fsk_block_le16(tbl, FSK_PROG_BLOCK_BYTES, base_pos + i * 2));
    return t;
}

bool fsk_prog_chunk_time(const FskProgRun &r, const CassetteWalkState &walk0, size_t k, uint64_t &time_us)
{
    const uint32_t C = fsk_pub_load(&r.pub_chunks);
    const uint32_t pc = fsk_pub_load(&r.pub_complete);
    if (k >= C || k > pc)
        return false;
    uint64_t t = walk0.time_us;
    for (size_t j = 0; j < k; ++j)
        t += static_cast<uint64_t>(hdr_irg(r.hdr[j])) * 1000ULL + r.chunk_ticks[j];
    time_us = t;
    return true;
}

bool fsk_prog_resolve_rewind(const FskProgRun &r, const CassetteWalkState &walk0, size_t tape_offset,
                             bool pos_valid, uint64_t pos_us, uint64_t back_us,
                             CassetteTargetResolution &out)
{
    out = CassetteTargetResolution{};

    const size_t kR = fsk_prog_find_chunk(r, tape_offset);
    if (kR == SIZE_MAX)
        return false;
    uint64_t tR = 0;
    if (!fsk_prog_chunk_time(r, walk0, kR, tR))
        return false;

    const uint64_t origin = tR + (pos_valid ? pos_us : 0ULL);
    const uint64_t target = origin > back_us ? origin - back_us : 0;
    if (target < walk0.time_us)
        return false; // before the resident run: file-based resolution

    const uint32_t C = fsk_pub_load(&r.pub_chunks);
    const uint32_t pc = fsk_pub_load(&r.pub_complete);

    // Last published chunk whose start time is <= target (the walker's boundary rule). Advancing past
    // chunk k needs its exact duration, i.e. k < pub_complete.
    uint64_t t = walk0.time_us;
    size_t k = 0;
    while (k + 1 < C && k < pc)
    {
        const uint64_t tn = t + static_cast<uint64_t>(hdr_irg(r.hdr[k])) * 1000ULL + r.chunk_ticks[k];
        if (tn > target)
            break;
        t = tn;
        ++k;
    }
    if (k >= pc)
    {
        // Chunk k is still loading, so its end is unknown; the walker's t_{k+1} is at least its start plus
        // what is already published of it. A target before that bound lies inside chunk k for certain.
        const uint64_t lower = t + static_cast<uint64_t>(hdr_irg(r.hdr[k])) * 1000ULL + fsk_prog_partial_ticks(r, k);
        if (target >= lower)
            return false;
    }
    else if (k + 1 >= C)
    {
        // The last published chunk is complete: the target must lie inside it (else the walk goes on).
        const uint64_t end = t + static_cast<uint64_t>(hdr_irg(r.hdr[k])) * 1000ULL + r.chunk_ticks[k];
        if (target >= end)
            return false;
    }

    out.walk = walk0;
    out.walk.offset = r.hdr_off[k];
    out.walk.time_us = t;
    out.is_fsk = true;
    out.irg_ms = hdr_irg(r.hdr[k]);
    out.q_us = target > t ? target - t : 0;
    return true;
}
