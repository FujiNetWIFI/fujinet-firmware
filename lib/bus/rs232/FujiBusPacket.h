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

class FujiBusPacket
{
private:
    fujiDeviceID_t _device;
    fujiCommandID_t _command;
    unsigned int _fieldSize;
    std::vector<unsigned int> _params;
    std::optional<std::string> _data = std::nullopt;

    std::string decodeSLIP(const std::string &input);
    std::string encodeSLIP(const std::string &input);
    bool parse(const std::string &input);
    uint8_t calcChecksum(const std::string &buf);

public:
    FujiBusPacket(const std::string &slipEncoded);
    FujiBusPacket(fujiDeviceID_t dev, fujiCommandID_t cmd, const std::string &dbuf) :
        _device(dev), _command(cmd), _data(dbuf) {}

    static std::unique_ptr<FujiBusPacket> fromSerialized(const std::string &input);

    std::string serialize();

    fujiDeviceID_t device() { return _device; }
    fujiCommandID_t command() { return _command; }
    unsigned int param(unsigned int index) { return _params[index]; }
    std::optional<std::string> data() const { return _data; }
};

#endif /* FUJIBUSPACKET_H */
