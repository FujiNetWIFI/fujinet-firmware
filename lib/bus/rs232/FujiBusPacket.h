#ifndef FUJIBUSPACKET_H
#define FUJIBUSPACKET_H

#include "PacketParam.h"
#include "fujiDeviceID.h"
#include "fujiCommandID.h"

#include <vector>
#include <optional>
#include <string>
#include <memory>
#include <cassert>
#include <cstdint>
#include <type_traits>

enum {
    SLIP_END     = 0xC0,
    SLIP_ESCAPE  = 0xDB,
    SLIP_ESC_END = 0xDC,
    SLIP_ESC_ESC = 0xDD,
};

// Raw byte buffer for on-the-wire data
using ByteBuffer = std::vector<std::uint8_t>;

class FujiBusPacket
{
private:
    fujiDeviceID_t _device{};
    fujiCommandID_t _command{};
    std::vector<PacketParam> _params;
    std::optional<ByteBuffer> _data;   // raw payload bytes

    // Internal helpers now operate on byte buffers
    ByteBuffer decodeSLIP(const ByteBuffer& input) const;
    ByteBuffer encodeSLIP(const ByteBuffer& input) const;
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
    FujiBusPacket() = default;

    template<typename... Args>
    FujiBusPacket(fujiDeviceID_t dev, fujiCommandID_t cmd, Args&&... args)
        : _device(dev)
        , _command(cmd)
    {
        (processArg(std::forward<Args>(args)), ...);  // fold expression
    }

    // Parsing/serialization now explicitly use ByteBuffer
    static std::unique_ptr<FujiBusPacket> fromSerialized(const ByteBuffer& input);

    ByteBuffer serialize() const;

    // Accessors
    fujiDeviceID_t device() const { return _device; }
    fujiCommandID_t command() const { return _command; }

    std::uint32_t param(unsigned int index) const {
        return _params.at(index).value;
    }

    unsigned int paramCount() const {
        return static_cast<unsigned int>(_params.size());
    }

    const std::optional<ByteBuffer>& data() const {
        return _data;
    }

    std::optional<std::string> dataAsString() const
    {
        if (!_data) return std::nullopt;
        return std::string(_data->begin(), _data->end());
    }
};

#endif /* FUJIBUSPACKET_H */
