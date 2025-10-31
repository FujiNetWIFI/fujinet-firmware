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
    std::string decodeSLIP(std::string_view input);
    std::string encodeSLIP(std::string_view input);
    bool parse(std::string_view input);
    uint8_t calcChecksum(std::string_view buf);

public:
    unsigned int device, command;
    unsigned int fieldSize;
    std::vector<unsigned int> fields;
    std::optional<std::string> data = nullptr;

    FujiBusCommand(std::string_view input);
    std::string serialize();
    static std::unique_ptr<FujiBusCommand> fromSerialized(std::string_view input);
};

#endif /* FUJIBUSCOMMAND_H */
