#ifndef CASSETTE_REWIND_REQUEST_H
#define CASSETTE_REWIND_REQUEST_H

#include <atomic>
#include <cstdint>

// What a rewind asks of the tape. The task that asks (the web UI's) does not own the tape: it leaves
// the request here, and the task that plays the tape takes it at a point where the tape is at rest.
struct CassetteRewind
{
    enum class Kind : uint8_t
    {
        none,
        seconds,
        to_start,
    };

    Kind kind = Kind::none;
    uint32_t seconds = 0; // Kind::seconds only
};

// At most one request is pending, kept in one word. Requests made before it is taken merge:
//   - seconds add up;
//   - the start of the tape beats any number of seconds, whichever was asked first, because
//     going back from the start is still the start.
class CassetteRewindRequest
{
public:
    // From any task.
    void add_seconds(uint32_t seconds);
    void to_start() { _pending = TO_START; }

    // From any task: the tape the request was meant for is gone (unmounted, or another one mounted).
    void discard() { _pending = NONE; }

    // From the task that owns the tape. Takes the pending request, if any. A tape being recorded has
    // no timeline to go back along, so it only takes a rewind to the start.
    CassetteRewind take(bool playback);

private:
    static constexpr uint32_t NONE = 0;
    static constexpr uint32_t TO_START = UINT32_MAX;

    // Seconds otherwise: at most TO_START - 1, which is longer than any tape. On the ESP32 build
    // (-mdisable-hardware-atomics, PSRAM workaround) ESP-IDF supplies the 32-bit operations: S32C1I
    // in internal RAM, a spinlock in PSRAM. Either way they are safe between tasks, and no ISR uses this.
    std::atomic<uint32_t> _pending{NONE};
};

inline void CassetteRewindRequest::add_seconds(uint32_t seconds)
{
    uint32_t pending = _pending;
    uint32_t total;
    do
    {
        if (pending == TO_START)
            return;
        total = seconds >= TO_START - pending ? TO_START - 1 : pending + seconds;
    } while (!_pending.compare_exchange_weak(pending, total));
}

inline CassetteRewind CassetteRewindRequest::take(bool playback)
{
    if (_pending == NONE)
        return {};

    const uint32_t pending = _pending.exchange(NONE);
    if (pending == TO_START)
        return {CassetteRewind::Kind::to_start, 0};
    if (pending == NONE || !playback)
        return {};
    return {CassetteRewind::Kind::seconds, pending};
}

#endif // CASSETTE_REWIND_REQUEST_H
