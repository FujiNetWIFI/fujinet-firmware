#ifndef CARTRIDGE_H
#define CARTRIDGE_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

#include "bus.h"
#include "fnio.h"
#include "global_types.h"

class fujiHost;

// The cartridge behind the companion MCU (FUJI_DEVICEID::DBC). A mounted ROM
// is pushed into it whole; nothing is read back. Each bus with a cartridge
// overrides what its companion firmware does differently.
class Cartridge
{
public:
    explicit Cartridge(SystemBusBase &bus) : _bus(bus) {}
    virtual ~Cartridge() = default;

    // Whether `filename` goes to the cartridge instead of a disk slot.
    virtual bool is_rom(const char *filename, uint32_t size) = 0;

    // Pushes the ROM in `fileh` into the cartridge, which boots it, and closes
    // `fileh` either way.
    virtual success_is_true mount(fnFile *fileh, const char *filename, uint32_t size,
                                  fujiHost &host) = 0;

protected:
    // Returns bytes copied; 0 at end or on error.
    using Reader = std::function<size_t(uint32_t offset, uint8_t *buffer, size_t length)>;

    static constexpr size_t MAX_BLOCK = 512;

    SystemBusBase &_bus;

    // NET_OPEN, `size` bytes from `read` in NET_WRITE blocks, then NET_CLOSE.
    success_is_true push(uint8_t stream_id, uint32_t size, const Reader &read);

    // NET_WRITE payload bytes, at most MAX_BLOCK.
    virtual size_t block_size() const { return 256; }

    // A u16 param: load segment 0.
    virtual std::unique_ptr<FujiBusPacket> open(uint8_t stream_id, uint32_t size);

    // Any reply counts.
    virtual bool accepted(const FujiBusPacket *reply) const { return reply != nullptr; }

    // Always sends NET_CLOSE, or the companion's stream state wedges; this one
    // goes unchecked. `complete` is an error after a short transfer.
    virtual success_is_true close(success_is_true complete);
};

#endif /* CARTRIDGE_H */
