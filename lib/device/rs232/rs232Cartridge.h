#ifndef RS232CARTRIDGE_H
#define RS232CARTRIDGE_H

#include <string>

#include "Cartridge.h"

// The pico/<console> carts: each stream is opened with its id and size, every
// frame must be ACKed, and a ROM's memory map goes in before the image.
class rs232Cartridge : public Cartridge
{
public:
    using Cartridge::Cartridge;

    bool is_rom(const char *filename, uint32_t size) override;

    // MediaTypeROM finds the ROM's memory map, which goes first if there
    // is one; the image's CLOSE boots it.
    success_is_true mount(fnFile *fileh, const char *filename, uint32_t size,
                          fujiHost &host) override;

protected:
    enum : uint8_t
    {
        STREAM_IMAGE = 0,
        STREAM_MEMORY_MAP = 1,
    };

    size_t block_size() const override { return 512; }

    // Payload [stream id][size u32le], so the companion can reject an
    // oversized ROM up front.
    std::unique_ptr<FujiBusPacket> open(uint8_t stream_id, uint32_t size) override
    {
        struct __attribute__((packed))
        {
            uint8_t id;
            u32le_t size;
        } open_hdr;
        static_assert(sizeof(open_hdr) == 5, "OPEN header must not be padded");
        open_hdr.id = stream_id;
        open_hdr.size = size;
        return _bus.sendCommand(
            FUJI_DEVICEID::DBC, CMD::NET_OPEN,
            std::string(reinterpret_cast<const char *>(&open_hdr), sizeof(open_hdr)));
    }

    bool accepted(const FujiBusPacket *reply) const override
    {
        return reply != nullptr && reply->command() == CMD::FUJI_ACK;
    }

    // 0x01 aborts, so partial data isn't booted.
    success_is_true close(success_is_true complete) override
    {
        std::unique_ptr<FujiBusPacket> reply =
            complete.is_success() ? _bus.sendCommand(FUJI_DEVICEID::DBC, CMD::NET_CLOSE)
                     : _bus.sendCommand(FUJI_DEVICEID::DBC, CMD::NET_CLOSE, std::string(1, '\x01'));
        RETURN_SUCCESS_IF(accepted(reply.get()));
    }
};

#endif /* RS232CARTRIDGE_H */
