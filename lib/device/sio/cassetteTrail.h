#ifndef CASSETTE_TRAIL_H
#define CASSETTE_TRAIL_H

#include <cstddef>
#include <cstdint>

#include "../../media/atari/casTape.h"

// A place on the tape the player can resume from: where in the file, how long the tape has played by
// then, and the baud in effect there.
struct CassetteBoundary
{
    uint32_t offset;
    uint32_t time_ms;
    uint16_t baud;
};
static_assert(sizeof(CassetteBoundary) == 12, "a boundary is two words and a half word, padded");

// The boundaries the player has passed on the way to where the tape is now, so a rewind by seconds is
// a lookup in memory and never a walk over the file.
//
// The trail is the ACTIVE history: a rewind moves the position back to a boundary and forgets every
// boundary after it, and playing on extends the trail from there.
//
// Only the task that plays the tape uses it; nothing here is locked, and nothing may call it from an
// interrupt (it allocates). When the trail cannot answer, a rewind answers with the start of the tape.
class CassetteTrail
{
public:
    using AllocFn = void *(*)(size_t bytes);

    static constexpr size_t DEFAULT_CAPACITY = 1024;
    static constexpr CassetteBoundary START = {0, 0, CAS_DEFAULT_BAUD};

    // `alloc` is malloc unless a test says otherwise.
    explicit CassetteTrail(size_t capacity = DEFAULT_CAPACITY, AllocFn alloc = nullptr);
    ~CassetteTrail();
    CassetteTrail(const CassetteTrail &) = delete;
    CassetteTrail &operator=(const CassetteTrail &) = delete;

    // Call before the tape moves. A trail belongs to one mounted tape (`generation`): a different one
    // starts it over. A tape that is not where the trail says it is (and is not at the start) makes the
    // trail unusable until the tape is at the start again.
    void sync(uint32_t generation, size_t tape_offset);

    // The tape moved on to `offset`, passing a record of `duration_ms` and leaving the baud at `baud`.
    void played(size_t offset, uint32_t duration_ms, unsigned short baud);

    // The tape is back at its start.
    void restart();

    // Rewinds `seconds` of tape time: to the last boundary at or before that point, or to the start of
    // the tape when it has not played that long. Later boundaries are dropped. The trail keeps at most
    // `capacity` boundaries by thinning out the old ones, so the landing can be farther back than the
    // exact point, never nearer.
    CassetteBoundary rewind_by(uint32_t seconds);

    bool valid() const { return _valid; }
    size_t size() const { return _count; }
    uint32_t stride() const { return _stride; }
    const CassetteBoundary &cursor() const { return _cursor; }
    const CassetteBoundary &at(size_t index) const { return _entries[index]; }

private:
    void compact();

    size_t _capacity;
    AllocFn _alloc;
    CassetteBoundary *_entries = nullptr;
    size_t _count = 0;

    bool _bound = false; // a tape generation has been seen
    uint32_t _generation = 0;
    bool _valid = false;
    CassetteBoundary _cursor = START; // where the tape is now, whether or not it is an entry
    uint32_t _stride = 1;             // played records per entry, grows when the trail is thinned
    uint32_t _since_entry = 0;
};

#endif // CASSETTE_TRAIL_H
