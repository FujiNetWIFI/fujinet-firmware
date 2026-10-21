#include "StreamCartridge.h"

#include <string>

#include "../../include/debug.h"

// CLOSE always goes out or the companion's stream state wedges; on failure it
// carries a 0x01 abort so partial data isn't booted. The size lets the
// companion reject an oversized ROM before we pull it over the network.
success_is_true StreamCartridge::push_stream(const Source &source, StreamId stream)
{
    uint8_t stream_id = static_cast<uint8_t>(stream);

    if (source.size == 0)
    {
        Debug_printv("ROM push: stream %u is empty or unreadable\n", stream_id);
        RETURN_ERROR_AS_FALSE();
    }

    struct __attribute__((packed))
    {
        uint8_t id;
        u32le_t size;
    } open_hdr;
    static_assert(sizeof(open_hdr) == 5, "OPEN header must not be padded");
    open_hdr.id = stream_id;
    open_hdr.size = source.size;

    auto opened = _link.request(
        CMD::NET_OPEN, std::string(reinterpret_cast<const char *>(&open_hdr), sizeof(open_hdr)));
    if (accepted(opened.get()).is_error())
    {
        Debug_printv("ROM push: failed to open DBC stream %u (%lu bytes)\n",
                     stream_id, static_cast<unsigned long>(source.size));
        RETURN_ERROR_AS_FALSE();
    }

    uint32_t sent = write_blocks(source, BLOCK);
    success_is_true result(sent == source.size);
    if (result.is_error())
    {
        Debug_printv("ROM push: stream %u short transfer: %lu of %lu bytes\n", stream_id,
                     static_cast<unsigned long>(sent), static_cast<unsigned long>(source.size));
    }

    auto closed = result.is_success() ? _link.request(CMD::NET_CLOSE)
                                      : _link.request(CMD::NET_CLOSE, std::string(1, '\x01'));
    if (accepted(closed.get()).is_error())
    {
        Debug_printv("ROM push: stream %u close failed/rejected\n", stream_id);
        result = success_is_true(false);
    }
    return result;
}

// The image's CLOSE triggers the boot, so the map has to land first.
success_is_true StreamCartridge::load(const Source &image, const Source *map)
{
    if (map != nullptr && push_stream(*map, StreamId::MemoryMap).is_error())
    {
        Debug_printv("ROM push: memory map push failed\n");
        RETURN_ERROR_AS_FALSE();
    }

    if (push_stream(image, StreamId::Image).is_error())
    {
        Debug_printv("ROM push: ROM push failed\n");
        RETURN_ERROR_AS_FALSE();
    }

    RETURN_SUCCESS_AS_TRUE();
}
