#ifdef BUILD_COCO

#include "casReader.h"

#include <string.h>

void CasReader::begin()
{
    _size = _src ? _src->size() : 0;
    _win_off = 0;
    _win_len = 0;
}

int CasReader::raw_byte(uint32_t offset)
{
    if (offset >= _size)
        return -1;

    if (offset < _win_off || offset >= _win_off + _win_len)
    {
        // Start one byte back: assembling a bit-shifted byte needs both b and
        // b+1, so refilling at b+1 would evict b and the next bit position
        // would fetch it straight back, thrashing on every window boundary.
        uint32_t start = offset > 0 ? offset - 1 : 0;
        size_t want = CAS_WINDOW_SIZE;
        if (start + want > _size)
            want = _size - start;

        size_t got = _src->read_at(start, _win, want);
        if (got == 0)
            return -1;

        _win_off = start;
        _win_len = (uint32_t)got;

        if (offset >= _win_off + _win_len)
            return -1;
    }

    return _win[offset - _win_off];
}

int CasReader::byte_at(uint64_t bitpos)
{
    if (bitpos + 8 > bit_count())
        return -1;

    uint32_t b = (uint32_t)(bitpos >> 3);
    unsigned s = (unsigned)(bitpos & 7);

    int lo = raw_byte(b);
    if (lo < 0)
        return -1;
    if (s == 0)
        return lo;

    int hi = raw_byte(b + 1);
    if (hi < 0)
        return -1;

    return ((lo >> s) | (hi << (8 - s))) & 0xff;
}

int64_t CasReader::find_sync(uint64_t bitpos, uint32_t *leader_bits)
{
    uint64_t start = bitpos;
    uint64_t end = bit_count();

    while (bitpos + 8 <= end)
    {
        int b = byte_at(bitpos);
        if (b < 0)
            break;
        if (b == CAS_SYNC_BYTE)
        {
            if (leader_bits)
                *leader_bits = (uint32_t)(bitpos - start);
            return (int64_t)(bitpos + 8);
        }
        bitpos++;
    }
    return -1;
}

int CasReader::read_block(uint64_t *bitpos, CasBlock *blk, uint8_t *payload)
{
    uint32_t leader = 0;
    int64_t p = find_sync(*bitpos, &leader);
    if (p < 0)
        return -1;

    int type = byte_at((uint64_t)p);
    int len = byte_at((uint64_t)p + 8);
    if (type < 0 || len < 0)
        return -1;

    uint64_t dbit = (uint64_t)p + 16;
    if (dbit + (uint64_t)(len + 1) * 8 > bit_count())
        return -1;                       // truncated block

    unsigned sum = (unsigned)type + (unsigned)len;
    for (int i = 0; i < len; i++)
    {
        int d = byte_at(dbit + (uint64_t)i * 8);
        if (d < 0)
            return -1;
        if (payload)
            payload[i] = (uint8_t)d;
        sum += (unsigned)d;
    }

    int ck = byte_at(dbit + (uint64_t)len * 8);
    if (ck < 0)
        return -1;

    blk->sync_bit = (uint32_t)p;
    blk->data_bit = (uint32_t)dbit;
    blk->leader_bits = leader;
    blk->type = (uint8_t)type;
    blk->len = (uint8_t)len;
    blk->sum_ok = (((sum - (unsigned)ck) & 0xff) == 0);

    *bitpos = dbit + (uint64_t)(len + 1) * 8;
    return type;
}

size_t CasReader::read_bytes(uint64_t bitpos, uint8_t *buf, size_t len)
{
    size_t n = 0;
    while (n < len)
    {
        int b = byte_at(bitpos + (uint64_t)n * 8);
        if (b < 0)
            break;
        buf[n++] = (uint8_t)b;
    }
    return n;
}

#endif // BUILD_COCO
