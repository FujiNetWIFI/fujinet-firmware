#ifndef STREAMCARTRIDGE_H
#define STREAMCARTRIDGE_H

#include "Cartridge.h"

// OPEN's payload is [stream id][size u32le]; params are ignored. Spoken by
// the pico/<console> carts on RS232.
class StreamCartridge : public Cartridge
{
public:
    explicit StreamCartridge(DBCLink &link) : Cartridge(link) {}

    // Pushes `map` (nullptr if the ROM has none), then `image`, whose CLOSE
    // boots it.
    success_is_true load(const Source &image, const Source *map);

private:
    enum class StreamId : uint8_t
    {
        Image = 0,
        MemoryMap = 1,
    };

    static constexpr size_t BLOCK = 512;

    success_is_true push_stream(const Source &source, StreamId stream);
};

#endif /* STREAMCARTRIDGE_H */
