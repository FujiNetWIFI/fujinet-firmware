// cassette_time_plan.cpp — host-buildable pure implementation for the
// Custom Rewind real-duration model + chunk boundary walker.
//
// Carries no FujiNet, ESP-IDF, GPIO, RMT, filesystem, or global-state
// dependency. All I/O is delegated to the injected positional reader. No
// allocation, no logging.

#include "cassette_time_plan.h"
#include "fsk_plan.h"

namespace
{
    constexpr uint16_t CAS_FUJI_HEADER_BYTES = 8;
    constexpr unsigned short CAS_LEGACY_BAUD = 600; // CASSETTE_BAUDRATE
    constexpr size_t CAS_LEGACY_BLOCK_LEN = 128;    // BLOCK_LEN

    struct RawChunkHeader
    {
        uint8_t type[4];
        uint16_t chunk_length;
        uint16_t irg_length;
    };

    // Reads the 8-byte chunk header at `offset`. Returns false on a short
    // read (caller treats this identically to fsk_compute_bounds's own
    // header_complete == false case: end of tape / malformed).
    bool read_header(cas_time_read_fn reader, void *ctx, size_t offset, RawChunkHeader &hdr)
    {
        uint8_t buf[CAS_FUJI_HEADER_BYTES];
        if (reader(ctx, offset, buf, CAS_FUJI_HEADER_BYTES) != CAS_FUJI_HEADER_BYTES)
            return false;
        hdr.type[0] = buf[0];
        hdr.type[1] = buf[1];
        hdr.type[2] = buf[2];
        hdr.type[3] = buf[3];
        hdr.chunk_length = fsk_decode_le16(&buf[4]);
        hdr.irg_length = fsk_decode_le16(&buf[6]);
        return true;
    }

    bool type_is(const RawChunkHeader &hdr, char a, char b, char c, char d)
    {
        return hdr.type[0] == (uint8_t)a && hdr.type[1] == (uint8_t)b &&
               hdr.type[2] == (uint8_t)c && hdr.type[3] == (uint8_t)d;
    }

    bool type_is_pwm(const RawChunkHeader &hdr)
    {
        return hdr.type[0] == 'p' && hdr.type[1] == 'w' && hdr.type[2] == 'm';
    }
} // namespace

uint64_t cas_bits_duration_us(size_t byte_count, unsigned short baud)
{
    if (baud == 0)
        return 0; // malformed 'baud' chunk (irg_length==0) — no divide-by-zero
    // Every operand widened to uint64_t BEFORE the first multiplication:
    // byte_count (size_t, up to 65535 for one chunk) * 10 bits/byte can
    // already exceed uint32_t range once multiplied by 1'000'000 below.
    return (static_cast<uint64_t>(byte_count) * 10ULL * 1000000ULL) /
           static_cast<uint64_t>(baud);
}

uint64_t cas_data_record_duration_us(uint16_t irg_length_ms, uint16_t chunk_length,
                                     unsigned short baud)
{
    return static_cast<uint64_t>(irg_length_ms) * 1000ULL +
           cas_bits_duration_us(chunk_length, baud);
}

uint64_t cas_legacy_block_duration_us()
{
    // send_tape_block(): SYSTEM_BUS.write(atari_sector_buffer, BLOCK_LEN+3)
    // = 131 bytes, PLUS a separate 1-byte checksum write = 132 bytes total,
    // at the fixed CASSETTE_BAUDRATE, plus a fixed fnSystem.delay(300).
    constexpr size_t kWireBytes = CAS_LEGACY_BLOCK_LEN + 3 + 1; // 132
    return cas_bits_duration_us(kWireBytes, CAS_LEGACY_BAUD) + 300000ULL;
}

bool cas_fsk_payload_duration_us(cas_time_read_fn reader, void *ctx,
                                 size_t payload_offset, size_t data_avail,
                                 uint64_t &out_us)
{
    uint64_t total = 0;
    const size_t value_count = fsk_value_count(data_avail); // floor(data_avail/2)

    // Read in small fixed chunks so a 65535-byte payload never requires one
    // giant read; 256 bytes = 128 FSK values per read.
    constexpr size_t kReadChunkBytes = 256;
    uint8_t buf[kReadChunkBytes];

    size_t values_done = 0;
    while (values_done < value_count)
    {
        const size_t values_this_read =
            (value_count - values_done > kReadChunkBytes / 2)
                ? (kReadChunkBytes / 2)
                : (value_count - values_done);
        const size_t bytes_this_read = values_this_read * 2;
        const size_t file_off = payload_offset + values_done * 2;

        if (reader(ctx, file_off, buf, bytes_this_read) != bytes_this_read)
            return false; // short/failed read

        for (size_t i = 0; i < values_this_read; ++i)
        {
            const uint16_t v = fsk_decode_le16(&buf[i * 2]);
            total += fsk_ticks_for_value(v); // ticks == microseconds (1 MHz), exact
        }
        values_done += values_this_read;
    }

    out_us = total;
    return true;
}

uint16_t cas_t2k_samples_to_us(uint8_t samples, uint16_t samplerate)
{
    if (samplerate == 0)
        return 0;
    return static_cast<uint16_t>((static_cast<uint32_t>(samples) * 1000000ULL) /
                                 samplerate / 2ULL);
}

uint64_t cas_pwmc_duration_us(uint16_t silence_ms)
{
    return static_cast<uint64_t>(silence_ms) * 1000ULL;
}

uint64_t cas_pwml_duration_us(uint16_t silence_ms, const uint8_t *sync_data, size_t sync_len,
                              uint16_t samplerate, uint16_t pilot_count, uint16_t pilot_half_us)
{
    uint64_t total = static_cast<uint64_t>(silence_ms) * 1000ULL;

    if (samplerate != 0)
    {
        for (size_t i = 0; i + 3 < sync_len; i += 4)
        {
            const uint16_t s0 = fsk_decode_le16(&sync_data[i]);
            const uint16_t s1 = fsk_decode_le16(&sync_data[i + 2]);
            // "Values are already half-periods — don't divide by 2" (matches
            // send_turbo2000_tape_block's pwml handling exactly).
            total += (static_cast<uint64_t>(s0) * 1000000ULL) / samplerate;
            total += (static_cast<uint64_t>(s1) * 1000000ULL) / samplerate;
        }
    }

    // Pilot tone deferred from the preceding pwmc, folded into this
    // transmission (production: pilot+sync+data in ONE rmt_transmit).
    total += static_cast<uint64_t>(pilot_count) * pilot_half_us * 2ULL;

    return total;
}

uint64_t cas_pwmd_duration_us(const uint8_t *data, size_t data_len,
                              uint16_t bit0_half_us, uint16_t bit1_half_us)
{
    uint64_t total = 0;
    for (size_t i = 0; i < data_len; ++i)
    {
        const uint8_t byte = data[i];
        for (int bit = 0; bit < 8; ++bit)
        {
            const bool one = ((byte >> bit) & 1) != 0;
            total += static_cast<uint64_t>(one ? bit1_half_us : bit0_half_us) * 2ULL;
        }
    }
    return total;
}

bool cas_walk_tape_time(size_t filesize, cas_time_read_fn reader, void *ctx,
                        size_t stop_at_offset, uint64_t stop_at_time_us,
                        CassetteWalkState &out)
{
    if (filesize == 0 || reader == nullptr)
        return false;

    // Same nominal defaults sioCassette's own field initializers use
    // (cassette.h) and the same 600-baud default check_for_FUJI_file()
    // applies before any 'baud' chunk is seen.
    CassetteWalkState state{};
    state.offset = 0;
    state.time_us = 0;
    state.baud = 600;
    state.t2k_samplerate = 44100;
    state.t2k_bit0_half = 272;
    state.t2k_bit1_half = 589;
    state.t2k_pilot_half = 726;
    state.t2k_pilot_count = 3072;
    state.qros_turbo_baud = 6580;

    // Detect FUJI vs legacy raw exactly like check_for_FUJI_file(): first 4
    // bytes literally "FUJI".
    uint8_t magic[4];
    const bool is_fuji = (reader(ctx, 0, magic, 4) == 4 &&
                         magic[0] == 'F' && magic[1] == 'U' &&
                         magic[2] == 'J' && magic[3] == 'I');

    if (!is_fuji)
    {
        // Legacy raw / SDrive: fixed 128-byte blocks, offset 0..filesize.
        const uint64_t block_us = cas_legacy_block_duration_us();
        while (true)
        {
            // out is always the LAST boundary that satisfies both stop
            // conditions — never overshoot.
            if (state.offset > stop_at_offset)
                break;
            if (state.time_us > stop_at_time_us)
                break;
            out = state;
            if (state.offset >= filesize)
                break; // final legacy boundary (terminal 0xFE record point)

            const size_t next_offset =
                (filesize - state.offset > CAS_LEGACY_BLOCK_LEN)
                    ? (state.offset + CAS_LEGACY_BLOCK_LEN)
                    : filesize;
            state.offset = next_offset;
            state.time_us += block_us;
        }
        return true;
    }

    // FUJI/A8CAS: walk chunk headers from offset 0.
    while (true)
    {
        if (state.offset > stop_at_offset)
            break;
        if (state.time_us > stop_at_time_us)
            break;
        out = state;

        RawChunkHeader hdr{};
        if (!read_header(reader, ctx, state.offset, hdr))
            break; // end of tape (fewer than 8 header bytes remain)

        const FskBounds bounds =
            fsk_compute_bounds(filesize, state.offset, hdr.chunk_length);
        if (!bounds.header_complete)
            break; // matches fsk_compute_bounds's own EOT rule

        if (type_is(hdr, 'd', 'a', 't', 'a'))
        {
            if (bounds.structurally_truncated)
                break; // truncated body -> EOT, mirrors send_FUJI_tape_block
            state.time_us += cas_data_record_duration_us(hdr.irg_length, hdr.chunk_length,
                                                          state.baud);
        }
        else if (type_is(hdr, 'b', 'a', 'u', 'd'))
        {
            if (bounds.structurally_truncated)
                break;
            state.baud = hdr.irg_length; // 0 duration, mirrors send_FUJI_tape_block
            if (state.baud > 3000)
                state.qros_turbo_baud = state.baud; // mirrors check_for_FUJI_file's QROS detection
        }
        else if (type_is(hdr, 'f', 's', 'k', ' '))
        {
            // FSK is exempt from the truncation-terminates-immediately rule
            // (Task 7 policy): a structurally truncated FSK chunk still
            // clamps to its present bytes and contributes that much time,
            // matching play_fsk_chunk()'s own clamped-payload behavior.
            uint64_t fsk_us = 0;
            if (!cas_fsk_payload_duration_us(reader, ctx, state.offset + CAS_FUJI_HEADER_BYTES,
                                             bounds.data_avail, fsk_us))
                break; // short/failed read — cannot account for this chunk
            state.time_us += static_cast<uint64_t>(hdr.irg_length) * 1000ULL + fsk_us;
            if (bounds.structurally_truncated)
                break; // clamped payload accounted for; nothing follows
        }
        else if (type_is_pwm(hdr))
        {
            if (bounds.structurally_truncated)
                break;
            if (type_is(hdr, 'p', 'w', 'm', 's'))
            {
                uint8_t tmp[2];
                if (hdr.chunk_length >= 2 &&
                    reader(ctx, state.offset + CAS_FUJI_HEADER_BYTES, tmp, 2) == 2)
                {
                    state.t2k_samplerate = fsk_decode_le16(tmp);
                }
                // 0 duration — matches send_turbo2000_tape_block's pwms handling
            }
            else if (type_is(hdr, 'p', 'w', 'm', 'c'))
            {
                uint8_t tmp[3];
                const size_t want = (hdr.chunk_length >= 3) ? 3 : hdr.chunk_length;
                if (want > 0 && reader(ctx, state.offset + CAS_FUJI_HEADER_BYTES, tmp, want) == want)
                {
                    const uint8_t pilot_pulse_len = tmp[0];
                    state.t2k_pilot_half =
                        cas_t2k_samples_to_us(pilot_pulse_len, state.t2k_samplerate);
                    if (want >= 3)
                        state.t2k_pilot_count =
                            static_cast<uint16_t>(tmp[1] | (tmp[2] << 8));
                }
                state.time_us += cas_pwmc_duration_us(hdr.irg_length);
            }
            else if (type_is(hdr, 'p', 'w', 'm', 'l'))
            {
                uint8_t syncbuf[64];
                const size_t want = (hdr.chunk_length > sizeof(syncbuf))
                                       ? sizeof(syncbuf) : hdr.chunk_length;
                size_t got = 0;
                if (want > 0)
                    got = reader(ctx, state.offset + CAS_FUJI_HEADER_BYTES, syncbuf, want);
                state.time_us += cas_pwml_duration_us(
                    hdr.irg_length, syncbuf, got, state.t2k_samplerate,
                    state.t2k_pilot_count, state.t2k_pilot_half);
            }
            else if (type_is(hdr, 'p', 'w', 'm', 'd'))
            {
                state.t2k_bit0_half =
                    cas_t2k_samples_to_us(static_cast<uint8_t>(hdr.irg_length & 0xFF),
                                         state.t2k_samplerate);
                state.t2k_bit1_half =
                    cas_t2k_samples_to_us(static_cast<uint8_t>((hdr.irg_length >> 8) & 0xFF),
                                         state.t2k_samplerate);
                // Bounded read: A8CAS chunk length is uint16, capped at
                // 65535 bytes; read in fixed windows so no unbounded stack
                // buffer is needed.
                constexpr size_t kWin = 256;
                uint8_t win[kWin];
                size_t remaining = hdr.chunk_length;
                size_t off = state.offset + CAS_FUJI_HEADER_BYTES;
                while (remaining > 0)
                {
                    const size_t chunk = (remaining > kWin) ? kWin : remaining;
                    if (reader(ctx, off, win, chunk) != chunk)
                        break; // short read: undercount rather than fail the whole walk
                    state.time_us += cas_pwmd_duration_us(win, chunk, state.t2k_bit0_half,
                                                          state.t2k_bit1_half);
                    off += chunk;
                    remaining -= chunk;
                }
            }
        }
        // else: unknown chunk type (or the leading "FUJI" header chunk
        // itself) — 0 duration, just advance.

        if (bounds.structurally_truncated)
            break; // no well-formed next_offset
        // fsk_compute_bounds guarantees next_offset == offset+8+declared_len
        // (>= 8) whenever structurally_truncated is false, so this always
        // advances strictly forward — no separate EOT-via-zero check needed.
        state.offset = bounds.next_offset;
    }

    return true;
}

FskActiveRewindResolution cas_fsk_resolve_active_rewind(
    const size_t *run_value_counts, size_t run_chunk_count,
    fsk_run_value_fn value_reader, void *ctx,
    uint64_t run_start_time_us, uint64_t leading_irg_us, uint64_t target_us)
{
    FskActiveRewindResolution r{};
    r.resolved = false;
    r.inside_leading_irg = false;
    r.chunk_index = 0;
    r.value_index = 0;

    if (run_chunk_count == 0 || target_us < run_start_time_us)
        return r; // target lies before this run entirely — caller falls back

    if (target_us < run_start_time_us + leading_irg_us)
    {
        r.resolved = true;
        r.inside_leading_irg = true;
        return r;
    }

    uint64_t accum = run_start_time_us + leading_irg_us;
    size_t best_chunk = 0, best_value = 0;
    bool past_target = false;
    for (size_t c = 0; c < run_chunk_count && !past_target; ++c)
    {
        const size_t vcount = run_value_counts[c];
        for (size_t v = 0; v < vcount; ++v)
        {
            if (accum > target_us)
            {
                past_target = true; // best_chunk/best_value already hold the
                                    // last value whose start time <= target_us
                break;
            }
            best_chunk = c;
            best_value = v;
            const uint16_t val = value_reader(ctx, c, v);
            accum += fsk_ticks_for_value(val); // ticks == microseconds, exact
        }
    }

    r.resolved = true;
    r.inside_leading_irg = false;
    r.chunk_index = best_chunk;
    r.value_index = best_value;
    return r;
}
