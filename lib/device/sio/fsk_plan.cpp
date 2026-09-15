// fsk_plan.cpp — host-buildable pure implementation of the two non-inline
// members: fsk_preload_into_blocks (bounded, injected-reader preload loop)
// and fsk_view_step (host-test cursor over a segmented payload). No
// FujiNet/ESP-IDF/filesystem dependency; no allocation or logging of its own.
//
// fsk_view_step shares the same pure rule helpers fsk_plan.h declares that
// the production RMT callback (fsk_encode_cb) inlines, so host tests and
// production can't diverge on duration, parity, or split.

#include "fsk_plan.h"

// Fills an already-allocated block table with up to `want` logical payload
// bytes: never requests more than `read_max` per reader call, never writes
// past a block boundary or past `want`, accumulates short reads, stops when
// the reader returns 0. Returns bytes actually loaded (== want on success).
//
// Preconditions: if want > 0 then blocks != nullptr, block_size > 0,
// block_count >= ceil(want / block_size), each blocks[i] has block_size
// writable bytes; read_max may be 0 only when want == 0.
size_t fsk_preload_into_blocks(uint8_t *const *blocks,
                               size_t block_count,
                               size_t block_size,
                               size_t want,
                               size_t read_max,
                               fsk_read_fn reader,
                               void *ctx)
{
    if (want == 0)
        return 0;

    // Defensive: without a reader, a positive block table, or a usable read cap,
    // no bytes can be loaded. Returning 0 == "nothing loaded" keeps the caller's
    // short-read/EOF handling correct rather than reading out of bounds.
    if (reader == nullptr || blocks == nullptr || block_size == 0 ||
        block_count == 0 || read_max == 0)
        return 0;

    size_t loaded = 0;

    while (loaded < want)
    {
        const size_t block_index = loaded / block_size;

        // Do not address a block outside the allocated table. This should not
        // happen when the caller sizes the table for `want`, but guarding keeps
        // the helper memory-safe for any inputs the host tests throw at it.
        if (block_index >= block_count)
            break;

        uint8_t *block = blocks[block_index];
        if (block == nullptr)
            break;

        const size_t offset_in_block = loaded % block_size;

        // Clamp one reader request to: the caller's per-read cap, the bytes left
        // in the current block (so a single write never straddles a block), and
        // the total bytes still wanted.
        size_t chunk = want - loaded;
        const size_t block_remaining = block_size - offset_in_block;
        if (chunk > block_remaining)
            chunk = block_remaining;
        if (chunk > read_max)
            chunk = read_max;

        const size_t got = reader(ctx, block + offset_in_block, chunk);

        // Reader delivered nothing: EOF/failure. Stop at the bytes already
        // loaded (a short total the caller detects as truncation/failure).
        if (got == 0)
            break;

        // A reader must never claim more than it was asked for; clamp defensively
        // so a misbehaving stub can never push `loaded` past `want` or out of the
        // block it is filling.
        const size_t accepted = (got > chunk) ? chunk : got;
        loaded += accepted;
    }

    return loaded;
}

// Host-test cursor step over a segmented payload: advances by exactly one
// emitted portion (or reports done). Values read via fsk_block_le16() so a
// value straddling two blocks reassembles correctly. Zero-duration values
// still consume their index (parity) but emit no portion; long values split
// incrementally into <=FSK_MAX_PORTION_TICKS portions via O(1) state.
FskStep fsk_view_step(FskChunkView &view)
{
    // Case 1: current value still being split. Emit its next portion.
    if (view.remaining_ticks > 0)
    {
        uint32_t portion = fsk_next_portion(view.remaining_ticks);
        view.remaining_ticks -= portion;

        // The LAST emitted portion carries done=true in the SAME call. More
        // work remains iff this value has ticks left or another is queued.
        bool more = view.remaining_ticks != 0 ||
                    view.value_index < fsk_value_count(view.data_len_available);
        return FskStep{ true, view.remaining_level_high, portion, !more };
    }

    // Case 2: load the next value. Skip leading zero-duration values, which
    // still consume a value index (advancing parity) and byte position but
    // produce no portion. Keep advancing until a non-zero value is found or
    // the values are exhausted.
    const size_t value_count = fsk_value_count(view.data_len_available);

    while (view.value_index < value_count)
    {
        uint16_t decoded = fsk_block_le16(view.blocks, view.block_size, view.byte_pos);
        bool     level   = fsk_level_for_index(view.value_index);
        uint32_t ticks   = fsk_ticks_for_value(decoded);

        // Consume this value's index/parity slot and byte position.
        view.value_index += 1;
        view.byte_pos    += 2;

        if (ticks == 0)
        {
            // Zero-duration value: consumed a parity slot, emits nothing.
            continue;
        }

        // Non-zero value: begin splitting it and emit its first portion.
        view.remaining_ticks      = ticks;
        view.remaining_level_high = level;

        uint32_t portion = fsk_next_portion(view.remaining_ticks);
        view.remaining_ticks -= portion;

        // May be the LAST emitted portion (single-portion value), in which
        // case done=true is returned in the SAME call.
        bool more = view.remaining_ticks != 0 ||
                    view.value_index < value_count;
        return FskStep{ true, level, portion, !more };
    }

    // Case 3: all values exhausted and nothing left to split.
    return FskStep{ false, false, 0, true };
}


// Single source of truth for the structural bounds/next-offset rule;
// play_fsk_chunk() consumes this exact result — no second O+8+L formula.
// Subtraction-guarded so it never underflows.
FskBounds fsk_compute_bounds(size_t filesize, size_t offset,
                             uint16_t declared_len)
{
    FskBounds b{ 0, 0, 0, false, false };

    if (filesize < offset)
        return b; // offset past EOF -> no complete header (next=0)

    const size_t remaining = filesize - offset;
    if (remaining < 8)
        return b; // < 8 header bytes remain -> end-of-tape

    b.header_complete = true;

    const size_t after_header = remaining - 8;
    const size_t declared = static_cast<size_t>(declared_len);

    b.structurally_truncated = (declared > after_header);
    b.data_avail = b.structurally_truncated ? after_header : declared;
    b.value_count = fsk_value_count(b.data_avail);

    // Well-formed -> advance past the full declared extent (O+8+L);
    // overrun/truncated -> EOT (next=0), since O+8+declared would overrun.
    b.next_offset = b.structurally_truncated ? 0 : (offset + 8 + declared);
    return b;
}
