#ifndef FUJILYNXPACKET_H
#define FUJILYNXPACKET_H

#ifdef BUILD_LYNX

#include "PacketParam.h"
#include "PacketParamProxy.h"
#include "fujiDeviceID.h"
#include "fujiCommandID.h"
#include "global_types.h"

#include <optional>
#include <vector>
#include <cassert>
#include <string>
#include <memory>

/**
 * Lynx implementation of the RS232 FujiBusPacket interface.
 *
 * This class provides the same logical interface as FujiBusPacket
 * (command(), param(), data(), and dataAsString()) so device handlers
 * can process Lynx packets using the same abstractions as other
 * buses.
 *
 * Unlike other packet formats, Lynx does not encode enough
 * structural information to deserialize a complete packet up front.
 * Parameters and data are therefore deserialized lazily as they are
 * requested. Parameter values are read from the bus on first access
 * and cached for subsequent accesses.
 */

class FujiLynxPacket
{
private:
    fujiDeviceID_t _device;
    mutable std::optional<ByteBuffer> _payload;
    mutable std::uint8_t _payload_checksum;
    mutable std::optional<fujiCommandID_t> _command;
    mutable unsigned _paramSize;
    mutable std::vector<PacketParam> _params;
    mutable std::optional<ByteBuffer> _data;

    using ParamProxy = PacketParamProxy<FujiLynxPacket>;
    friend ParamProxy;

    std::uint32_t getParam(size_t index, size_t psize) const;
    void fillParams(size_t count, size_t psize) const;
    bool parse(const ByteBuffer& input);
    std::uint8_t calcChecksum(const ByteBuffer& buf) const;

    // Variadic constructor helpers for parameters
    void processArg(std::uint8_t v)  { _params.emplace_back(v); }
    void processArg(std::uint16_t v) { _params.emplace_back(v); }
    void processArg(std::uint32_t v) { _params.emplace_back(v); }

    // Payload helpers
    void processArg(const ByteBuffer& buf) { _data = buf; }
    void processArg(ByteBuffer&& buf)      { _data = std::move(buf); }

    // Convenience: allow passing a std::string payload; it’s treated as raw bytes
    void processArg(const std::string& s) {
        ByteBuffer buf(s.begin(), s.end());
        _data = std::move(buf);
    }

public:
    FujiLynxPacket() = default;
    template<typename... Args>
    FujiLynxPacket(fujiCommandID_t command, Args&&... args)
        : _command(command)
    {
        (processArg(std::forward<Args>(args)), ...);  // fold expression
    }

    static std::unique_ptr<FujiLynxPacket> fromSerialized(const ByteBuffer& input);
    ByteBuffer serialize() const;

    fujiDeviceID_t device() const { return _device; }
    fujiCommandID_t command() const;

    ParamProxy param(size_t index) const { return ParamProxy{ index, this }; }

    const std::optional<ByteBuffer>& data() const;
    const std::optional<const std::string> dataAsString() const {
        auto d = data();
        return std::string(reinterpret_cast<const char *>(d->data()), d->size());
    }

    // Explicit alternatives to the implicit ParamProxy conversions.
    // These may be preferred where the destination type is not obvious.
    std::uint8_t  param8(int idx)  const { return (std::uint8_t)param(idx); }
    std::uint16_t param16(int idx) const { return (std::uint16_t)param(idx); }
    std::uint32_t param32(int idx) const { return (std::uint32_t)param(idx); }

    // Delete copy semantics to prevent pass-by-value bugs
    FujiLynxPacket(const FujiLynxPacket&) = delete;
    FujiLynxPacket& operator=(const FujiLynxPacket&) = delete;
};

#endif /* BUILD_LYNX */

#endif /* FUJILYNXPACKET_H */
