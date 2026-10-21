#include "Cartridge.h"

#include <algorithm>
#include <string>

#include "../../include/debug.h"

success_is_true Cartridge::accepted(const FujiBusPacket *reply) const
{
    RETURN_SUCCESS_IF(reply != nullptr && reply->command() == CMD::FUJI_ACK);
}

uint32_t Cartridge::write_blocks(const Source &source, size_t block)
{
    uint8_t buffer[MAX_BLOCK];
    block = std::min(block, sizeof(buffer));

    uint32_t sent = 0;
    while (sent < source.size)
    {
        size_t want = std::min<size_t>(block, source.size - sent);
        size_t got = source.read(sent, buffer, want);
        if (got == 0)
            break; // the caller reports the short transfer

        auto reply = _link.request(CMD::NET_WRITE,
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
    return sent;
}
