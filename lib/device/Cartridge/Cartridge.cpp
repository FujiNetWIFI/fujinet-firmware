#include "Cartridge.h"

#include <algorithm>
#include <string>

#include "../../include/debug.h"

std::unique_ptr<FujiBusPacket> Cartridge::open(uint8_t, uint32_t)
{
    return _bus.sendCommand(FUJI_DEVICEID::DBC, CMD::NET_OPEN, uint16_t{0});
}

success_is_true Cartridge::close(success_is_true complete)
{
    _bus.sendCommand(FUJI_DEVICEID::DBC, CMD::NET_CLOSE);
    RETURN_SUCCESS_AS_TRUE();
}

success_is_true Cartridge::push(uint8_t stream_id, uint32_t size, const Reader &read)
{
    if (size == 0)
    {
        Debug_printv("ROM push: stream %u is empty or unreadable\n", stream_id);
        RETURN_ERROR_AS_FALSE();
    }

    std::unique_ptr<FujiBusPacket> reply = open(stream_id, size);
    if (!accepted(reply.get()))
    {
        Debug_printv("ROM push: failed to open stream %u (%lu bytes)\n", stream_id,
                     static_cast<unsigned long>(size));
        RETURN_ERROR_AS_FALSE();
    }

    uint8_t buffer[MAX_BLOCK];
    size_t block = std::min(block_size(), sizeof(buffer));
    uint32_t sent = 0;
    while (sent < size)
    {
        size_t got = read(sent, buffer, std::min<size_t>(block, size - sent));
        if (got == 0)
            break;

        reply = _bus.sendCommand(FUJI_DEVICEID::DBC, CMD::NET_WRITE,
                                 std::string(reinterpret_cast<char *>(buffer), got));
        if (!accepted(reply.get()))
        {
            Debug_printv("ROM push: block at %lu of %lu bytes not taken\n",
                         static_cast<unsigned long>(sent), static_cast<unsigned long>(size));
            break;
        }
        sent += got;
    }

    success_is_true complete(sent == size);
    if (complete.is_error())
    {
        Debug_printv("ROM push: stream %u short transfer: %lu of %lu bytes\n", stream_id,
                     static_cast<unsigned long>(sent), static_cast<unsigned long>(size));
    }

    if (close(complete).is_error())
    {
        Debug_printv("ROM push: stream %u close failed/rejected\n", stream_id);
        RETURN_ERROR_AS_FALSE();
    }
    return complete;
}
