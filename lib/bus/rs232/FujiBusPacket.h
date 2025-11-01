#ifndef FUJIBUSPACKET_H
#define FUJIBUSPACKET_H

#include "fuji_devices.h"
#include "fuji_commands.h"

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
    std::string decodeSLIP(const std::string &input);
    std::string encodeSLIP(const std::string &input);
    bool parse(const std::string &input);
    uint8_t calcChecksum(const std::string &buf);

public:
    FujiDeviceID device;
    FujiCommandID command;
    unsigned int fieldSize;
    std::vector<unsigned int> fields;
    std::optional<std::string> data = std::nullopt;

    FujiBusPacket(const std::string &input);
    std::string serialize();
    static std::unique_ptr<FujiBusPacket> fromSerialized(const std::string &input);
};

#endif /* FUJIBUSPACKET_H */
