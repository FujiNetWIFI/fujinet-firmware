#ifndef READSEQUENCE_H
#define READSEQUENCE_H

#include "global_types.h"

#ifdef ESP_PLATFORM
#include "PSRAMAllocator.h"
#endif

/** NET_SET_SEQUENCE: a READ carries a sequence number that changes on every
    new READ. A bus retry repeats it, so it gets the last chunk again. */
class ReadSequence
{
public:
    bool enabled() const { return on; }

    /** Turns the mode on or off and forgets the last chunk. OPEN and CLOSE turn it off. */
    void set(bool enable)
    {
        on = enable;
        lastLength = 0;
        lastSequence = NONE;
        lastResult = error_is_true(false);
        decltype(lastChunk)().swap(lastChunk);
    }

    /** A READ's 16-bit length field: bit 15 is the sequence number while the mode is on. */
    uint16_t length(uint16_t lengthField) const { return on ? lengthField & 0x7FFF : lengthField; }
    uint16_t sequence(uint16_t lengthField) const { return on ? lengthField >> 15 : NONE; }

    /** Same sequence number and same length as the last READ: the bus retried it.
        The length asked for is compared, not the chunk's size, which is shorter at
        the end of the data and empty after an error. */
    bool is_retry(uint16_t length, uint16_t sequence) const
    {
        return on && sequence == lastSequence && length == lastLength;
    }

    void remember(uint16_t length, uint16_t sequence, const ByteBuffer &chunk, error_is_true result)
    {
        if (!on)
            return;
        lastLength = length;
        lastSequence = sequence;
        lastChunk.assign(chunk.begin(), chunk.end());
        lastResult = result;
    }

    /** Hands out the last chunk again, with the result the first attempt had. */
    error_is_true replay(ByteBuffer &chunk) const
    {
        chunk.assign(lastChunk.begin(), lastChunk.end());
        return lastResult;
    }

private:
    // No READ yet: matches no sequence number a READ can carry.
    static constexpr uint16_t NONE = 0xFFFF;

    bool on = false;
    uint16_t lastLength = 0;
    uint16_t lastSequence = NONE;
    error_is_true lastResult{false};
#ifdef ESP_PLATFORM
    // One per channel, so keep it out of internal DRAM.
    std::vector<uint8_t, PSRAMAllocator<uint8_t>> lastChunk;
#else
    ByteBuffer lastChunk;
#endif
};

#endif /* READSEQUENCE_H */
