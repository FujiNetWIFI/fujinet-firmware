#include "Cartridge.h"

#include <algorithm>
#include <memory>
#include <string>

#include "../../include/debug.h"

enum : uint8_t
{
    STREAM_IMAGE = 0,
    STREAM_MEMORY_MAP = 1,
};

success_is_true Cartridge::accepted(const FujiBusPacket *reply) const
{
    RETURN_SUCCESS_IF(reply != nullptr &&
                      (!_dialect.ack_required || reply->command() == CMD::FUJI_ACK));
}

// CLOSE always goes out or the companion's stream state wedges. In the streams
// dialect the size lets the companion reject an oversized ROM up front.
success_is_true Cartridge::push_stream(const Source &source, uint8_t stream_id)
{
    if (source.size == 0)
    {
        Debug_printv("ROM push: stream %u is empty or unreadable\n", stream_id);
        RETURN_ERROR_AS_FALSE();
    }

    std::unique_ptr<FujiBusPacket> reply;
    if (_dialect.streams)
    {
        struct __attribute__((packed))
        {
            uint8_t id;
            u32le_t size;
        } open_hdr;
        static_assert(sizeof(open_hdr) == 5, "OPEN header must not be padded");
        open_hdr.id = stream_id;
        open_hdr.size = source.size;
        reply = _bus.sendCommand(
            FUJI_DEVICEID::DBC, CMD::NET_OPEN,
            std::string(reinterpret_cast<const char *>(&open_hdr), sizeof(open_hdr)));
    }
    else
    {
        reply = _bus.sendCommand(FUJI_DEVICEID::DBC, CMD::NET_OPEN, uint16_t{0});
    }
    if (accepted(reply.get()).is_error())
    {
        Debug_printv("ROM push: failed to open stream %u (%lu bytes)\n", stream_id,
                     static_cast<unsigned long>(source.size));
        RETURN_ERROR_AS_FALSE();
    }

    uint8_t buffer[MAX_BLOCK];
    size_t block = std::min(_dialect.block, sizeof(buffer));
    uint32_t sent = 0;
    while (sent < source.size)
    {
        size_t got = source.read(sent, buffer, std::min<size_t>(block, source.size - sent));
        if (got == 0)
            break;

        reply = _bus.sendCommand(FUJI_DEVICEID::DBC, CMD::NET_WRITE,
                                 std::string(reinterpret_cast<char *>(buffer), got));
        if (accepted(reply.get()).is_error())
        {
            Debug_printv("ROM push: block at %lu of %lu bytes not taken\n",
                         static_cast<unsigned long>(sent),
                         static_cast<unsigned long>(source.size));
            break;
        }
        sent += got;
    }

    success_is_true result(sent == source.size);
    if (result.is_error())
    {
        Debug_printv("ROM push: stream %u short transfer: %lu of %lu bytes\n", stream_id,
                     static_cast<unsigned long>(sent), static_cast<unsigned long>(source.size));
    }

    if (!_dialect.ack_required)
    {
        _bus.sendCommand(FUJI_DEVICEID::DBC, CMD::NET_CLOSE);
        return result;
    }

    // 0x01 aborts, so partial data isn't booted
    reply = result.is_success()
                ? _bus.sendCommand(FUJI_DEVICEID::DBC, CMD::NET_CLOSE)
                : _bus.sendCommand(FUJI_DEVICEID::DBC, CMD::NET_CLOSE, std::string(1, '\x01'));
    if (accepted(reply.get()).is_error())
    {
        Debug_printv("ROM push: stream %u close failed/rejected\n", stream_id);
        RETURN_ERROR_AS_FALSE();
    }
    return result;
}

// The image's CLOSE triggers the boot, so the map has to land first.
success_is_true Cartridge::load(const Source &image, const Source *map)
{
    if (map != nullptr && push_stream(*map, STREAM_MEMORY_MAP).is_error())
    {
        Debug_printv("ROM push: memory map push failed\n");
        RETURN_ERROR_AS_FALSE();
    }

    if (push_stream(image, STREAM_IMAGE).is_error())
    {
        Debug_printv("ROM push: ROM push failed\n");
        RETURN_ERROR_AS_FALSE();
    }

    RETURN_SUCCESS_AS_TRUE();
}
