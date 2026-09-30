#ifndef CAS_TAPE_H
#define CAS_TAPE_H

// What the tape player needs to know about how long the records it plays take. Tape time is kept in
// whole milliseconds, each record rounded up.

#include <cstddef>
#include <cstdint>

// A tape starts at this baud, before any "baud" chunk.
constexpr unsigned short CAS_DEFAULT_BAUD = 600;

// 8N1 framing: 10 bits per byte. A malformed baud of 0 takes no time.
constexpr uint32_t cas_bits_duration_ms(size_t byte_count, unsigned short baud)
{
    if (baud == 0)
        return 0;
    if (byte_count > (UINT64_MAX - UINT16_MAX) / 10000ULL)
        return UINT32_MAX; // more than the arithmetic below can hold
    const uint64_t ms = (static_cast<uint64_t>(byte_count) * 10ULL * 1000ULL + baud - 1) / baud;
    return ms > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(ms);
}

// An A8CAS "data" record: the gap ahead of it (its aux, in ms), then its bytes at the current baud.
constexpr uint32_t cas_data_record_ms(uint16_t gap_ms, size_t byte_count, unsigned short baud)
{
    const uint64_t ms = static_cast<uint64_t>(gap_ms) + cas_bits_duration_ms(byte_count, baud);
    return ms > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(ms);
}

// Legacy raw tape: send_tape_block() sends 128 data bytes with 3 header bytes and a checksum at 600 baud,
// then waits 300 ms.
constexpr size_t CAS_LEGACY_WIRE_BYTES = 128 + 4;
constexpr uint32_t CAS_LEGACY_DELAY_MS = 300;
constexpr uint32_t CAS_LEGACY_BLOCK_MS = cas_bits_duration_ms(CAS_LEGACY_WIRE_BYTES, CAS_DEFAULT_BAUD) + CAS_LEGACY_DELAY_MS;

#endif // CAS_TAPE_H
