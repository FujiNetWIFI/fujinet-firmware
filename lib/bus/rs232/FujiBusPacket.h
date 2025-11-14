#ifndef FUJIBUSPACKET_H
#define FUJIBUSPACKET_H

#include "fujiDeviceID.h"
#include "fujiCommandID.h"

#include <vector>
#include <optional>
#include <string>
#include <memory>

enum {
    SLIP_END     = 0xC0,
    SLIP_ESCAPE  = 0xDB,
    SLIP_ESC_END = 0xDC,
    SLIP_ESC_ESC = 0xDD,
};

struct PacketParam {
    uint32_t value;
    uint8_t  size;

    template<typename T>
    PacketParam(T v) : value(static_cast<uint32_t>(v)), size(sizeof(T)) {
        static_assert(std::is_same_v<T, uint8_t> ||
                      std::is_same_v<T, uint16_t> ||
                      std::is_same_v<T, uint32_t>,
                      "Param can only be uint8_t, uint16_t, or uint32_t");
    }
    PacketParam(uint32_t v, uint8_t s) : value(v), size(s) {
        if (s != 1 && s != 2 && s != 4)
            throw std::invalid_argument("Param size must be 1, 2, or 4");
    }
};

class FujiBusPacket
{
private:
    fujiDeviceID_t _device;
    fujiCommandID_t _command;
    std::vector<PacketParam> _params;
    std::optional<std::string> _data = std::nullopt;

    std::string decodeSLIP(const std::string &input);
    std::string encodeSLIP(const std::string &input);
    bool parse(const std::string &input);
    uint8_t calcChecksum(const std::string &buf);

    void processArg(uint8_t v)  { _params.emplace_back(v); }
    void processArg(uint16_t v) { _params.emplace_back(v); }
    void processArg(uint32_t v) { _params.emplace_back(v); }

    void processArg(const std::string& s) { _data = s; }

public:
    FujiBusPacket() = default;

    template<typename... Args>
    FujiBusPacket(fujiDeviceID_t dev, fujiCommandID_t cmd, Args&&... args)
        : _device(dev), _command(cmd)
    {
        (processArg(std::forward<Args>(args)), ...);  // fold expression
    }

    static std::unique_ptr<FujiBusPacket> fromSerialized(const std::string &input);

    std::string serialize();

    fujiDeviceID_t device() { return _device; }
    fujiCommandID_t command() { return _command; }
    uint32_t param(unsigned int index) { return _params[index].value; }
    unsigned int paramCount() { return _params.size(); }
    std::optional<std::string> data() const { return _data; }
};

#endif /* FUJIBUSPACKET_H */
