#ifndef PACKETPARAMPROXY_H
#define PACKETPARAMPROXY_H

#include <cstdint>

/**
 * Proxy object returned by param().
 *
 * The proxy exists because parameter decoding requires two pieces of
 * information that are not available at the same time:
 *
 *   - param(index) knows which parameter is being requested, but not
 *     the integer width the caller expects.
 *   - The conversion operator knows the requested integer width, but
 *     would otherwise have no way to identify which parameter to decode.
 *
 * The proxy preserves the parameter index until the compiler selects
 * the appropriate conversion operator. At that point the packet knows
 * both the parameter index and the requested width, allowing it to:
 *
 *   - Read the correct number of bytes from the bus.
 *   - Convert Adam's big-endian encoding to native byte order.
 *   - Cache the decoded value for subsequent accesses.
 *
 * Example:
 *
 *   uint16_t length = packet.param(0);
 *   uint8_t  mode   = packet.param(1);
 *
 * The compiler selects the conversion operator based on the destination
 * type (or an explicit cast). Subsequent accesses return the cached
 * value rather than reading from the bus again.
 */

#include <cstddef>

template <typename Packet>
class PacketParamProxy
{
private:
    std::size_t _index;
    const Packet *_packet;

    PacketParamProxy(std::size_t index, const Packet *packet)
        : _index(index)
        , _packet(packet)
    {
    }

public:
    operator std::uint8_t() const {
        return _packet->getParam(_index, sizeof(std::uint8_t));
    }

    operator std::uint16_t() const {
        return _packet->getParam(_index, sizeof(std::uint16_t));
    }

    operator std::uint32_t() const {
        return _packet->getParam(_index, sizeof(std::uint32_t));
    }

    operator bool() const {
        return static_cast<uint8_t>(*this) != 0;
    }

    bool operator==(std::uint8_t value) const {
        return static_cast<std::uint8_t>(*this) == value;
    }

    bool operator==(std::uint16_t value) const {
        return static_cast<std::uint16_t>(*this) == value;
    }

    bool operator==(std::uint32_t value) const {
        return static_cast<std::uint32_t>(*this) == value;
    }

    template <typename>
    friend class PacketParamProxy;

    friend Packet;
};

#endif /* PACKETPARAMPROXY_H */
