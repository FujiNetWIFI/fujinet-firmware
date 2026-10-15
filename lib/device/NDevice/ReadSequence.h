#ifndef READSEQUENCE_H
#define READSEQUENCE_H

#include "global_types.h"

#ifdef ESP_PLATFORM
#include "PSRAMAllocator.h"
#endif

/** NET_SET_SEQUENCE: bit 15 of a READ length flips on every new READ. A bus
    retry repeats the whole length word, so it gets the last chunk again. */
class ReadSequence
{
public:
    bool enabled() const { return on; }

    /** Turns the mode on or off and forgets the last chunk. OPEN and CLOSE turn it off. */
    void set(bool enable)
    {
        on = enable;
        lastParam = -1;
        lastFailed = false;
        decltype(lastChunk)().swap(lastChunk);
    }

    /** The length a READ asks for, without the sequence bit. */
    uint16_t length(uint16_t param) const { return on ? param & 0x7FFF : param; }

    /** Same bit and same length as the last READ: the bus retried it. The length
        a READ asked for is not the chunk's size, which is shorter at the end of
        the data and empty after an error. */
    bool is_retry(uint16_t param) const { return on && param == lastParam; }

    void remember(uint16_t param, const ByteBuffer &chunk, bool failed)
    {
        if (!on)
            return;
        lastParam = param;
        lastChunk.assign(chunk.begin(), chunk.end());
        lastFailed = failed;
    }

    /** Hands out the last chunk again with the result it had. */
    error_is_true replay(ByteBuffer &chunk) const
    {
        chunk.assign(lastChunk.begin(), lastChunk.end());
        return error_is_true(lastFailed);
    }

private:
    bool on = false;
    int lastParam = -1;
    bool lastFailed = false;
#ifdef ESP_PLATFORM
    // One per channel, so keep it out of internal DRAM.
    std::vector<uint8_t, PSRAMAllocator<uint8_t>> lastChunk;
#else
    ByteBuffer lastChunk;
#endif
};

#endif /* READSEQUENCE_H */
