// fsk_plan.cpp — host-test-only cursor step for the pure A8CAS FSK module.
//
// This file implements ONLY fsk_view_step. It is host-buildable and carries no
// FujiNet, ESP-IDF, GPIO, RMT, filesystem, or global-state dependency. It does
// not allocate, perform I/O, or log, and it performs only O(1) state
// transitions on the FskChunkView cursor (it never materializes the full
// waveform). It reads strictly within [0, data_len_available).
//
// fsk_view_step shares the SAME pure rule helpers (fsk_decode_le16,
// fsk_level_for_index, fsk_ticks_for_value, fsk_next_portion, fsk_value_count)
// declared in fsk_plan.h that the production IRAM RMT callback (fsk_encode_cb)
// inlines, so host tests and production cannot diverge on the modeled duration.

#include "fsk_plan.h"

FskStep fsk_view_step(FskChunkView &view)
{
    // Case 1: the current value is still being split. Emit its next portion.
    if (view.remaining_ticks > 0)
    {
        uint32_t portion = fsk_next_portion(view.remaining_ticks);
        view.remaining_ticks -= portion;
        return FskStep{ true, view.remaining_level_high, portion, false };
    }

    // Case 2: load the next value. Skip leading zero-duration values, which
    // still consume a value index (advancing parity) and byte position but
    // produce no portion. Keep advancing until a non-zero value is found or
    // the values are exhausted.
    const size_t value_count = fsk_value_count(view.data_len_available);

    while (view.value_index < value_count)
    {
        // byte_pos == value_index * 2, and value_index < floor(len/2), so both
        // byte_pos and byte_pos+1 are strictly within [0, data_len_available).
        uint16_t decoded = fsk_decode_le16(&view.data[view.byte_pos]);
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
        return FskStep{ true, level, portion, false };
    }

    // Case 3: all values exhausted and nothing left to split.
    return FskStep{ false, false, 0, true };
}
