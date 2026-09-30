#include "cassetteTrail.h"

#include <cstdlib>

CassetteTrail::CassetteTrail(size_t capacity, AllocFn alloc)
    : _capacity(capacity < 2 ? 2 : capacity), _alloc(alloc != nullptr ? alloc : std::malloc)
{
}

CassetteTrail::~CassetteTrail()
{
    std::free(_entries);
}

void CassetteTrail::sync(uint32_t generation, size_t tape_offset)
{
    if (!_bound || generation != _generation)
    {
        _bound = true;
        _generation = generation;
        restart();
    }
    else if (tape_offset == 0 && (!_valid || _cursor.offset != 0))
        restart(); // at the start the trail can always begin again, allocating if it could not before
    else if (tape_offset != _cursor.offset)
        _valid = false;
}

void CassetteTrail::restart()
{
    _cursor = START;
    _count = 0;
    _stride = 1;
    _since_entry = 0;

    // allocated when the first tape needs it, and kept
    if (_entries == nullptr)
        _entries = static_cast<CassetteBoundary *>(_alloc(_capacity * sizeof(CassetteBoundary)));

    _valid = _entries != nullptr;
    if (_valid)
        _entries[_count++] = START;
}

void CassetteTrail::played(size_t offset, uint32_t duration_ms, unsigned short baud)
{
    if (!_valid)
        return;
    if (offset > UINT32_MAX)
    {
        _valid = false;
        return;
    }

    const uint64_t time_ms = static_cast<uint64_t>(_cursor.time_ms) + duration_ms;
    _cursor.offset = static_cast<uint32_t>(offset);
    _cursor.time_ms = time_ms > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(time_ms);
    _cursor.baud = baud;

    if (++_since_entry < _stride)
        return;
    _since_entry = 0;

    if (_count == _capacity)
        compact();
    _entries[_count++] = _cursor;
}

// Keeps every other boundary, the start among them. What is kept is still real boundaries, with the
// state they had.
void CassetteTrail::compact()
{
    const size_t kept = (_count + 1) / 2;
    for (size_t i = 1; i < kept; ++i)
        _entries[i] = _entries[2 * i];
    _count = kept;
    _stride *= 2;
}

CassetteBoundary CassetteTrail::rewind_by(uint32_t seconds)
{
    if (!_valid)
    {
        // nothing to go back along: the tape starts over, and the trail with it
        restart();
        return START;
    }

    const uint64_t back_ms = static_cast<uint64_t>(seconds) * 1000ULL;
    size_t found = 0; // the start, unless the tape has played longer than that
    if (back_ms < _cursor.time_ms)
    {
        const uint64_t target_ms = _cursor.time_ms - back_ms;

        // the last boundary at or before the target; times never decrease
        size_t lo = 0;
        size_t hi = _count;
        while (lo < hi)
        {
            const size_t mid = lo + (hi - lo) / 2;
            if (_entries[mid].time_ms <= target_ms)
                lo = mid + 1;
            else
                hi = mid;
        }
        found = lo - 1; // the start has time 0, so lo >= 1
    }

    _count = found + 1; // what was played after it is no longer history
    _cursor = _entries[found];
    _since_entry = 0;
    return _cursor;
}
