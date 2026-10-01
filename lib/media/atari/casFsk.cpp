#include "casFsk.h"

namespace
{

struct FskChunkHeader
{
    uint8_t  type[4];
    uint16_t length;
    uint16_t irg;
};

// Reads one 8-byte A8CAS chunk header at `offset`. Returns false if fewer
// than 8 bytes are available (truncated header) or the read comes up short.
bool fsk_read_header(FskReadFn read, void *ctx, size_t filesize, size_t offset, FskChunkHeader &out)
{
    if (offset + 8 > filesize)
        return false;

    uint8_t raw[8];
    if (read(ctx, offset, raw, sizeof(raw)) != sizeof(raw))
        return false;

    out.type[0] = raw[0];
    out.type[1] = raw[1];
    out.type[2] = raw[2];
    out.type[3] = raw[3];
    out.length  = fsk_decode_le16(raw + 4);
    out.irg     = fsk_decode_le16(raw + 6);
    return true;
}

} // namespace

bool fsk_scan_run(FskReadFn read, void *ctx, size_t filesize, size_t header_offset, FskRunInfo &out)
{
    out = FskRunInfo{};
    size_t offset = header_offset;

    while (out.chunk_count < FSK_RUN_MAX_CHUNKS)
    {
        FskChunkHeader hdr;
        if (!fsk_read_header(read, ctx, filesize, offset, hdr))
        {
            if (out.chunk_count == 0)
                return false; // header_offset itself is unreadable
            break; // end of file while peeking for a continuation: the run ends cleanly here
        }
        if (!fsk_is_chunk_type(hdr.type))
        {
            if (out.chunk_count == 0)
                return false; // header_offset is not an fsk chunk at all
            break; // end of the run: some other chunk type follows
        }
        if (out.chunk_count > 0 && hdr.irg != 0)
            break; // a fresh fsk chunk (its own gap), not a continuation
        if ((hdr.length & 1u) != 0)
            return false; // an fsk chunk's payload must be whole LE16 values

        const size_t chunk_data_offset = offset + 8;
        if (chunk_data_offset + hdr.length > filesize)
            return false; // this chunk's payload would run past end of file

        if (out.chunk_count == 0)
            out.leading_irg_ms = hdr.irg;

        out.chunk_count++;
        out.payload_bytes += hdr.length;
        offset = chunk_data_offset + hdr.length;
    }

    if (out.chunk_count == 0)
        return false; // unreachable given the checks above, kept for safety

    out.value_count = out.payload_bytes / 2;
    out.next_offset = offset;
    return true;
}

uint64_t fsk_sum_waveform_ticks(const uint8_t *payload, size_t value_count)
{
    uint64_t total_ticks = 0;
    for (size_t i = 0; i < value_count; ++i)
    {
        const uint16_t value = fsk_decode_le16(payload + i * 2);
        total_ticks += fsk_ticks_for_value(value);
    }
    return total_ticks;
}
