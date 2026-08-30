#ifndef FUJIADAMPACKET_H
#define FUJIADAMPACKET_H

#ifdef BUILD_ADAM

#include "PacketParam.h"
#include "PacketParamProxy.h"
#include "fujiDeviceID.h"
#include "fujiCommandID.h"
#include "global_types.h"

#include <optional>
#include <vector>
#include <cassert>
#include <string>

typedef enum class APT {
    MN_RESET   = 0x00,  // command.control  (reset)
    MN_STATUS  = 0x01,  // command.control  (status)
    MN_ACK     = 0x02,  // command.control  (ack)
    MN_CLR     = 0x03,  // command.control  (clr) (aka CTS)
    MN_RECEIVE = 0x04,  // command.control  (receive)
    MN_CANCEL  = 0x05,  // command.control  (cancel)
    MN_SEND    = 0x06,  // command.control  (send)
    MN_NAK     = 0x07,  // command.control  (nak)
    MN_READY   = 0x0D,  // command.control  (ready)

    NM_STATUS  = 0x08,  // response.control (status)
    NM_ACK     = 0x09,  // response.control (ack)
    NM_CANCEL  = 0x0A,  // response.control (cancel)
    NM_SEND    = 0x0B,  // response.data    (send)
    NM_NAK     = 0x0C,  // response.control (nak)
} adamPacketType_t;

/**
 * AdamNet implementation of the RS232 FujiBusPacket interface.
 *
 * This class provides the same logical interface as FujiBusPacket
 * (command(), param(), data(), and dataAsString()) so device handlers
 * can process AdamNet packets using the same abstractions as other
 * buses.
 *
 * Unlike other packet formats, AdamNet does not encode enough
 * structural information to deserialize a complete packet up front.
 * Parameters and data are therefore deserialized lazily as they are
 * requested. Parameter values are read from the bus on first access
 * and cached for subsequent accesses.
 */

class FujiAdamPacket
{
private:
    fujiDeviceID_t _device;
    adamPacketType_t _type;
    mutable std::optional<ByteBuffer> _payload;
    mutable std::uint8_t _payload_checksum;
    mutable std::optional<std::uint8_t> _unit;
    mutable std::optional<fujiCommandID_t> _command;
    mutable std::vector<PacketParam> _params;
    mutable unsigned _paramSize;
    mutable std::optional<ByteBuffer> _data;

    using ParamProxy = PacketParamProxy<FujiAdamPacket>;
    friend ParamProxy;

    std::uint32_t getParam(size_t index, size_t psize) const;
    void fillParams(size_t count, size_t psize) const;
    std::uint8_t calcChecksum(const ByteBuffer& buf) const;

    // Variadic constructor helpers for parameters
    void processArg(std::uint8_t v)  { _params.emplace_back(v); }
    void processArg(std::uint16_t v) { _params.emplace_back(v); }
    void processArg(std::uint32_t v) { _params.emplace_back(v); }

    // Payload helpers
    void processArg(const ByteBuffer& buf) { _data = buf; }
    void processArg(ByteBuffer&& buf)      { _data = std::move(buf); }
    void processArg(fujiCommandID_t cmd)   { _command = cmd; }

    // Convenience: allow passing a std::string payload; it’s treated as raw bytes
    void processArg(const std::string& s) {
        ByteBuffer buf(s.begin(), s.end());
        _data = std::move(buf);
    }

public:
    FujiAdamPacket(std::uint8_t dest) : _device(static_cast<fujiDeviceID_t>(dest & 0x0F)),
                                   _type(static_cast<adamPacketType_t>(dest >> 4)) {};
    template<typename... Args>
    FujiAdamPacket(fujiDeviceID_t source, adamPacketType_t type, Args&&... args)
        : _device(source)
        , _type(type)
    {
        (processArg(std::forward<Args>(args)), ...);  // fold expression
    }

    bool isReply(void) const {
        switch (_type)
        {
        case APT::NM_STATUS:  // 0x08
        case APT::NM_ACK:     // 0x09
        case APT::NM_CANCEL:  // 0x0a
        case APT::NM_SEND:    // 0x0b
        case APT::NM_NAK:     // 0x0c
            return true;

        default:
            return false;
        }
    }

    ByteBuffer serialize() const;

    fujiDeviceID_t device() const { return _device; }
    adamPacketType_t type() const { return _type; }
    fujiCommandID_t command() const;

    ParamProxy param(size_t index) const { return ParamProxy{ index, this }; }

    error_is_true setPayload(ByteBuffer &payload, std::uint8_t checksum);

    const std::optional<ByteBuffer>& data() const;
    const std::optional<const std::string> dataAsString() const {
        auto d = data();
        return std::string(reinterpret_cast<const char *>(d->data()), d->size());
    }

    // A C string field arrives padded out to the driver's fixed buffer size.
    const std::optional<const std::string> dataAsCString() const {
        auto s = dataAsString();
        return s->substr(0, s->find('\0'));
    }

    // Explicit alternatives to the implicit ParamProxy conversions.
    // These may be preferred where the destination type is not obvious.
    std::uint8_t  param8(int idx)  const { return (std::uint8_t)param(idx); }
    std::uint16_t param16(int idx) const { return (std::uint16_t)param(idx); }
    std::uint32_t param32(int idx) const { return (std::uint32_t)param(idx); }

    // Delete copy semantics to prevent pass-by-value bugs
    FujiAdamPacket(const FujiAdamPacket&) = delete;
    FujiAdamPacket& operator=(const FujiAdamPacket&) = delete;
};

#endif /* BUILD_ADAM */

#endif /* FUJIADAMPACKET_H */
