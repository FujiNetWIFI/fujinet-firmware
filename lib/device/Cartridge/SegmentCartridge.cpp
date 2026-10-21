#include "SegmentCartridge.h"

#include "../../include/debug.h"

success_is_true SegmentCartridge::accepted(const FujiBusPacket *reply) const
{
    RETURN_SUCCESS_IF(reply != nullptr);
}

success_is_true SegmentCartridge::load(const Source &image)
{
    const uint16_t segment = 0;

    if (image.size == 0)
    {
        Debug_printv("ROM image is empty or unreadable");
        RETURN_ERROR_AS_FALSE();
    }

    if (accepted(_link.request(CMD::NET_OPEN, segment).get()).is_error())
    {
        Debug_printv("Failed to open pico bank");
        RETURN_ERROR_AS_FALSE();
    }

    uint32_t sent = write_blocks(image, BLOCK);

    _link.request(CMD::NET_CLOSE);
    Debug_printv("ROM transfer complete: %lu / %lu bytes",
                 static_cast<unsigned long>(sent), static_cast<unsigned long>(image.size));

    RETURN_SUCCESS_IF(sent == image.size);
}
