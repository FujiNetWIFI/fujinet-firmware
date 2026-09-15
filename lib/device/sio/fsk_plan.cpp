// fsk_plan.cpp — host-buildable pure implementation for the A8CAS FSK module.
//
// This file implements the two non-inline members of the pure module:
//   - fsk_preload_into_blocks : the bounded, injected-reader preload loop
//   - fsk_view_step           : the host-test cursor step over a segmented payload
//
// It is host-buildable and carries no FujiNet, ESP-IDF, GPIO, RMT, filesystem,
// or global-state dependency. It does not allocate, log, or perform I/O of its
// own (all I/O is delegated to the injected reader in fsk_preload_into_blocks),
// and fsk_view_step performs only O(1) state transitions on the FskChunkView
// cursor (it never materializes the full waveform). All logical reads stay
// strictly within [0, data_len_available).
//
// fsk_view_step shares the SAME pure rule helpers (fsk_block_le16 /
// fsk_decode_le16, fsk_level_for_index, fsk_ticks_for_value, fsk_next_portion,
// fsk_value_count) declared in fsk_plan.h that the production IRAM RMT callback
// (fsk_encode_cb) inlines, so host tests and production cannot diverge on the
// modeled duration, parity, or split.

#include "fsk_plan.h"

// -----------------------------------------------------------------------------
// Bounded, injected-reader preload loop (design: "Payload Preload Strategy",
// "Host-testable preload read-loop").
// -----------------------------------------------------------------------------
//
// Fills an already-allocated block table with up to `want` logical payload
// bytes. The loop:
//   - never requests more than `read_max` bytes in one reader call (honors the
//     TNFS <=525-byte per-read limit imposed by the production caller);
//   - never writes beyond a single block (each request is also clamped to the
//     bytes remaining in the current block, so a request never straddles the
//     block boundary);
//   - never writes beyond logical byte `want`;
//   - accumulates positive short/partial reads;
//   - stops as soon as the reader returns 0 before `want` is reached
//     (EOF/failure), returning the number of bytes actually loaded.
//
// Returns the total number of bytes loaded; equals `want` on full success and is
// strictly less than `want` on a short read / EOF / reader failure.
//
// Preconditions (guaranteed by the caller): if want > 0 then blocks != nullptr,
// block_size > 0, block_count >= ceil(want / block_size), and each blocks[i]
// points to block_size writable bytes. read_max may be 0 only when want == 0.
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

// -----------------------------------------------------------------------------
// Host-test cursor step over a segmented (block-table) payload.
// -----------------------------------------------------------------------------
//
// Advances the cursor by exactly one emitted RMT-sized portion (or reports
// done). Values are read through fsk_block_le16(), so a 2-byte value whose bytes
// straddle two preload blocks is reassembled correctly. Zero-duration values
// still consume their original signal index (advancing parity) and byte
// position, but emit no portion. Long values are split incrementally into
// same-level portions of at most FSK_MAX_PORTION_TICKS using O(1) state.
FskStep fsk_view_step(FskChunkView &view)
{
    // Case 1: the current value is still being split. Emit its next portion.
    if (view.remaining_ticks > 0)
    {
        uint32_t portion = fsk_next_portion(view.remaining_ticks);
        view.remaining_ticks -= portion;

        // Approved contract: the LAST emitted portion carries done=true in the
        // SAME call. More work remains iff this value still has ticks left or
        // another value is still to be loaded.
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
        // byte_pos == value_index * 2, and value_index < floor(len/2), so both
        // logical positions byte_pos and byte_pos+1 are strictly within
        // [0, data_len_available). fsk_block_le16 fetches each byte independently
        // through the block table, so a value straddling a block boundary is
        // reassembled correctly.
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

        // Approved contract: this may be the LAST emitted portion (single-portion
        // final value), in which case done=true is returned in the SAME call.
        // More work remains iff this value still has ticks left or another value
        // is still to be loaded (value_index already advanced past this one).
        bool more = view.remaining_ticks != 0 ||
                    view.value_index < value_count;
        return FskStep{ true, level, portion, !more };
    }

    // Case 3: all values exhausted and nothing left to split.
    return FskStep{ false, false, 0, true };
}


// -----------------------------------------------------------------------------
// Structural chunk bounds + caller next-offset (pure)
// -----------------------------------------------------------------------------
//
// This is the single source of truth for the structural bounds/next-offset rule.
// The production caller (sioCassette::play_fsk_chunk) consumes this exact result;
// there is no second O+8+L formula. The arithmetic mirrors the prior in-cassette
// logic byte-for-byte: subtraction-guarded so it never underflows.
FskBounds fsk_compute_bounds(size_t filesize, size_t offset,
                             uint16_t declared_len)
{
    // Deterministic zeroed result for every early-return path.
    FskBounds b{ 0, 0, 0, false, false };

    if (filesize < offset)
        return b; // defensive: offset past EOF -> no complete header (next=0)

    const size_t remaining = filesize - offset; // bytes from header start to EOF
    if (remaining < 8)
        return b; // < 8 header bytes remain -> end-of-tape (Req 6.1); next=0

    b.header_complete = true;

    const size_t after_header = remaining - 8;
    const size_t declared = static_cast<size_t>(declared_len);

    b.structurally_truncated = (declared > after_header);
    b.data_avail = b.structurally_truncated ? after_header : declared;
    b.value_count = fsk_value_count(b.data_avail); // floor(data_avail / 2), Req 6.4

    // Well-formed chunk advances past its full declared extent (O + 8 + L);
    // an overrun/truncated chunk terminates at EOT (next = 0), because
    // offset + 8 + declared would point past the image.
    b.next_offset = b.structurally_truncated ? 0 : (offset + 8 + declared);
    return b;
}
