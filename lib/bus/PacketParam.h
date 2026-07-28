#ifndef PACKETPARAM_H
#define PACKETPARAM_H

#include <cassert>
#include <cstdint>
#include <type_traits>

struct PacketParam
{
    std::uint32_t value;
    std::uint8_t  size;   // 1, 2, or 4 bytes

    template<typename T>
    PacketParam(T v)
        : value(static_cast<std::uint32_t>(v))
        , size(static_cast<std::uint8_t>(sizeof(T)))
    {
        static_assert(
                      std::is_same_v<T, std::uint8_t>  ||
                      std::is_same_v<T, std::uint16_t> ||
                      std::is_same_v<T, std::uint32_t>,
                      "Param can only be uint8_t, uint16_t, or uint32_t"
                      );
    }

    PacketParam(std::uint32_t v, std::uint8_t s)
        : value(v)
        , size(s)
    {
        assert(s == 1 || s == 2 || s == 4);
    }
};

#endif /* PACKETPARAM_H */
