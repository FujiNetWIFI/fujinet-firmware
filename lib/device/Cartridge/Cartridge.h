#ifndef CARTRIDGE_H
#define CARTRIDGE_H

#include <cstddef>
#include <cstdint>
#include <functional>

#include "bus.h"
#include "global_types.h"

// The cartridge behind the companion MCU (FUJI_DEVICEID::DBC). A mounted ROM
// is pushed into it whole; nothing is read back.
class Cartridge
{
public:
    // How one companion firmware takes a ROM.
    struct Dialect
    {
        size_t block; // NET_WRITE payload bytes, at most MAX_BLOCK
        // OPEN payload [stream id][size u32le], .cfg map on stream 1 before the
        // image on 0. Otherwise OPEN is a u16 load segment param (0).
        bool streams;
        // Otherwise any reply counts and CLOSE goes unchecked, with no abort.
        bool ack_required;
    };

    static constexpr Dialect PICO_STREAMS{512, true, true}; // pico/<console> carts
    static constexpr Dialect FUJIVERSAL{256, false, false}; // pico/fujiversal

    // One stream of ROM bytes; read() returns bytes copied, 0 at end or on error.
    struct Source
    {
        uint32_t size = 0; // 0 when absent or unreadable
        std::function<size_t(uint32_t offset, uint8_t *buffer, size_t length)> read;
    };

    static constexpr size_t MAX_BLOCK = 512;

    Cartridge(SystemBusBase &bus, const Dialect &dialect) : _bus(bus), _dialect(dialect) {}

    // Pushes `map` (nullptr if none), then `image`, whose CLOSE boots it.
    success_is_true load(const Source &image, const Source *map);

private:
    SystemBusBase &_bus;
    const Dialect _dialect;

    success_is_true accepted(const FujiBusPacket *reply) const;
    success_is_true push_stream(const Source &source, uint8_t stream_id);
};

#endif /* CARTRIDGE_H */
