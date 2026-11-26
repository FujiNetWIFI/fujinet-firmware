#ifndef CARTRIDGE_H
#define CARTRIDGE_H

#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <utility>
#include <vector>

#include "bus.h"
#include "fujiCommandID.h"
#include "global_types.h"
#include "rom/ROMMedia.h"

class Cartridge
{
public:
    static constexpr size_t BLOCK_SIZE = 512;

    explicit Cartridge(SystemBusBase &bus) : _bus(bus) {}

    error_is_true mount(ROMMedia &rom);

private:
    SystemBusBase &_bus;

    error_is_true pushMapperInfo(const std::pmr::vector<uint8_t> &info);
    error_is_true pushImage(ROMMedia &rom);
    error_is_true pushMapperLayout(const MapperLayout &layout);
    error_is_true closeSegment(error_is_true pushed);

    template <typename... Args>
    error_is_true send(fujiCommandID_t command, Args &&...args)
    {
        auto reply = _bus.sendCommand(FUJI_DEVICEID::DBC, command, std::forward<Args>(args)...);
        RETURN_ERROR_IF(!reply || reply->command() != CMD::FUJI_ACK);
    }
};

#endif /* CARTRIDGE_H */
