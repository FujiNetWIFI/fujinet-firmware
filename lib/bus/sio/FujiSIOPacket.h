#ifndef FUJISIOPACKET_H
#define FUJISIOPACKET_H

#include "cmdFrame.h"

#include <optional>
#include <cassert>
#include <span>
#include <string>

class FujiSIOPacket
{
private:
    mutable std::vector<uint16_t> _params;
    mutable std::optional<ByteBuffer> _data;
    mutable unsigned _paramSize;

    struct PacketParamProxy
    {
        size_t index;
        const FujiSIOPacket *packet;

        // These tell the compiler: "Run this code if the destination matches my type"
        operator bool() const {
            return static_cast<uint8_t>(*this) != 0;
        }

        // These tell the compiler exactly how to handle direct equality checks
        // against any integer type without triggering conversion rule debates.

        bool operator==(uint8_t val) const {
            return static_cast<uint8_t>(*this) == val;
        }

        bool operator==(uint16_t val) const {
            return static_cast<uint16_t>(*this) == val;
        }

        inline operator uint8_t() const {
            return packet->getParam(index, sizeof(uint8_t));
        }

        inline operator uint16_t() const {
            return packet->getParam(index, sizeof(uint16_t));
        }
    };

    friend PacketParamProxy;

    uint16_t getParam(size_t index, size_t psize) const;

public:
    cmdFrame_t frame;

    FujiSIOPacket() = default;

    void decode(uint8_t *raw);

    fujiDeviceID_t device() const { return frame.device; }
    fujiCommandID_t command() const { return frame.comnd; }

    PacketParamProxy param(size_t index) const { return PacketParamProxy{ index, this }; }

    // Completes deserialization by reading the trailing data field once its
    // length has been determined from command-specific context.
    error_is_true setDataLength(const size_t len) const;

    const std::optional<ByteBuffer>& data() const {
        assert(_data.has_value());
        return _data;
    }
    const std::optional<const std::string> dataAsString() const {
        auto d = data();
        return std::string(reinterpret_cast<const char *>(d->data()), d->size());
    }

    // Explicit alternatives to the implicit PacketParamProxy conversions.
    // These may be preferred where the destination type is not obvious.
    uint8_t  param8(size_t index)  const { return (uint8_t) param(index); }
    uint16_t param16(size_t index) const { return (uint16_t) param(index); }

    // Delete copy semantics to prevent pass-by-value bugs
    FujiSIOPacket(const FujiSIOPacket&) = delete;
    FujiSIOPacket& operator=(const FujiSIOPacket&) = delete;

};

#endif /* FUJISIOPACKET_H */
