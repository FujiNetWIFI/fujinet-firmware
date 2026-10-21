#ifndef CARTRIDGE_H
#define CARTRIDGE_H

#include <cstddef>
#include <cstdint>
#include <functional>

#include "DBCLink.h"
#include "global_types.h"

// A cartridge on the far side of a DBCLink that ROMs are pushed into. The bus
// supplies the link; each subclass speaks one companion firmware's dialect.
class Cartridge
{
public:
    // One stream of ROM bytes; read() returns bytes copied, 0 at end or on error.
    struct Source
    {
        uint32_t size = 0; // 0 when absent or unreadable
        std::function<size_t(uint32_t offset, uint8_t *buffer, size_t length)> read;
    };

    virtual ~Cartridge() = default;

protected:
    static constexpr size_t MAX_BLOCK = 512;

    DBCLink &_link;

    explicit Cartridge(DBCLink &link) : _link(link) {}

    // Whether the cartridge took the frame.
    virtual success_is_true accepted(const FujiBusPacket *reply) const;

    // NET_WRITEs `source`, up to `block` bytes a frame; returns bytes taken.
    uint32_t write_blocks(const Source &source, size_t block);
};

#endif /* CARTRIDGE_H */
