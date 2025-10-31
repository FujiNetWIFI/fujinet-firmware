#ifndef FUJIBUSCOMMAND_H
#define FUJIBUSCOMMAND_H

#include <string_view>
#include <vector>
#include <optional>
#include <string>
#include <memory>

class FujiBusCommand
{
private:
    unsigned int _device, _command;
    unsigned int _fieldSize;
    std::vector<unsigned int> _fields;
    std::optional<std::string> _data = nullptr;

    std::string decodeSLIP(std::string_view input);
    std::string encodeSLIP(std::string_view input);
    bool parse(std::string_view input);
    uint8_t calcChecksum(std::string_view buf);

public:
    FujiBusCommand(std::string_view input);
    std::string serialize();
    static std::unique_ptr<FujiBusCommand> fromSerialized(std::string_view input);
};

#endif /* FUJIBUSCOMMAND_H */
