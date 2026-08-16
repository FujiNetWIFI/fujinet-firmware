#ifndef FUJIIECPACKET_H
#define FUJIIECPACKET_H

#ifdef BUILD_IEC

#include "PacketParam.h"
#include "PacketParamProxy.h"
#include "fujiCommandID.h"
#include "global_types.h"

#include <optional>
#include <string>

class FujiIECPacket
{
private:
    uint8_t _device{};
    fujiCommandID_t _command{};
    mutable unsigned _paramSize;
    mutable std::vector<PacketParam> _params;
    mutable std::optional<ByteBuffer> _payload;
    mutable std::optional<ByteBuffer> _data;

    using ParamProxy = PacketParamProxy<FujiIECPacket>;
    friend ParamProxy;

    std::uint32_t getParam(size_t index, size_t psize) const;
    void fillParams(size_t count, size_t psize) const;

public:
    FujiIECPacket() = default;
    FujiIECPacket(uint8_t dev, fujiCommandID_t cmd, std::optional<ByteBuffer>
                  buf=std::nullopt) : _device(dev), _command(cmd), _payload(buf) {}

    ByteBuffer serialize() const;

    uint8_t device() const { return _device; }
    fujiCommandID_t command() const { return _command; }

    ParamProxy param(size_t index) const { return ParamProxy{ index, this }; }

    void setPayload(ByteBuffer &payload);

    const std::optional<ByteBuffer>& data() const;
    const std::optional<const std::string> dataAsString() const {
        auto d = data();
        return std::string(reinterpret_cast<const char *>(d->data()), d->size());
    }

    // Explicit alternatives to the implicit PacketParamProxy conversions.
    // These may be preferred where the destination type is not obvious.
    uint8_t  param8(size_t index)  const { return (uint8_t) param(index); }
    uint16_t param16(size_t index) const { return (uint16_t) param(index); }
};

#endif /* BUILD_IEC */

#endif /* FUJIIECPACKET_H */
