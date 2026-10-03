#include "casFSK.h"

FSKChunkHeader fsk_parse_header(const uint8_t raw[FSK_CHUNK_HEADER_BYTES])
{
    FSKChunkHeader h;
    h.type[0] = raw[0];
    h.type[1] = raw[1];
    h.type[2] = raw[2];
    h.type[3] = raw[3];
    h.length  = fsk_decode_le16(raw + 4);
    h.irg     = fsk_decode_le16(raw + 6);
    return h;
}

FSKChunkCheck fsk_check_chunk(const FSKChunkHeader &h, size_t offset, size_t filesize)
{
    if (!fsk_is_chunk_type(h.type))
        return FSKChunkCheck::not_fsk;
    if ((h.length & 1u) != 0)
        return FSKChunkCheck::malformed; // payload must be whole LE16 values
    if (offset + FSK_CHUNK_HEADER_BYTES + h.length > filesize)
        return FSKChunkCheck::malformed; // payload would run past end of file
    return FSKChunkCheck::ok;
}

namespace
{

// Reads one 8-byte A8CAS chunk header at `offset`. Returns false if fewer
// than 8 bytes are available (truncated header) or the read comes up short.
bool fsk_read_header(FSKReadFn read, void *ctx, size_t filesize, size_t offset, FSKChunkHeader &out)
{
    if (offset + FSK_CHUNK_HEADER_BYTES > filesize)
        return false;

    uint8_t raw[FSK_CHUNK_HEADER_BYTES];
    if (read(ctx, offset, raw, sizeof(raw)) != sizeof(raw))
        return false;

    out = fsk_parse_header(raw);
    return true;
}

} // namespace

bool fsk_scan_run(FSKReadFn read, void *ctx, size_t filesize, size_t header_offset, FSKRunInfo &out)
{
    out = FSKRunInfo{};
    size_t offset = header_offset;

    while (out.chunk_count < FSK_RUN_MAX_CHUNKS)
    {
        FSKChunkHeader hdr;
        if (!fsk_read_header(read, ctx, filesize, offset, hdr))
        {
            if (out.chunk_count == 0)
                return false; // header_offset itself is unreadable
            break; // end of file while peeking for a continuation: the run ends cleanly here
        }

        const FSKChunkCheck check = fsk_check_chunk(hdr, offset, filesize);
        if (out.chunk_count == 0)
        {
            if (check != FSKChunkCheck::ok)
                return false; // header_offset is not a well-formed fsk chunk
            out.leading_irg_ms = hdr.irg;
        }
        else if (!fsk_chunk_joins(check, hdr))
        {
            if (check == FSKChunkCheck::malformed && hdr.irg == 0)
                return false; // a malformed continuation invalidates the run
            break; // some other chunk type, or a fresh fsk chunk with its own gap
        }

        out.chunk_count++;
        out.payload_bytes += hdr.length;
        offset += FSK_CHUNK_HEADER_BYTES + hdr.length;
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
