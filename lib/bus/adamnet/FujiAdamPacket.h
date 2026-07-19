#ifdef BUILD_ADAM

#ifndef DRIVEWIREPACKET_H
#define DRIVEWIREPACKET_H

#include "fujiDeviceID.h"
#include "fujiCommandID.h"
#include "global_types.h"

#include <optional>
#include <vector>
#include <cassert>
#include <string>

typedef enum class APT {
    MN_RESET   = 0x00,  // command.control  (reset)
    MN_STATUS  = 0x01,  // command.control  (status)
    MN_ACK     = 0x02,  // command.control  (ack)
    MN_CLR     = 0x03,  // command.control  (clr) (aka CTS)
    MN_RECEIVE = 0x04,  // command.control  (receive)
    MN_CANCEL  = 0x05,  // command.control  (cancel)
    MN_SEND    = 0x06,  // command.control  (send)
    MN_NACK    = 0x07,  // command.control  (nack)
    MN_READY   = 0x0D,  // command.control  (ready)

    NM_STATUS  = 0x08,  // response.control (status)
    NM_ACK     = 0x09,  // response.control (ack)
    NM_CANCEL  = 0x0A,  // response.control (cancel)
    NM_SEND    = 0x0B,  // response.data    (send)
    NM_NACK    = 0x0C,  // response.control (nack)
} adamPacketType_t;

/**
 * DriveWire implementation of the RS232 FujiBusPacket interface.
 *
 * This class provides the same logical interface as FujiBusPacket
 * (command(), param(), data(), and dataAsString()) so device handlers
 * can process DriveWire packets using the same abstractions as other
 * buses.
 *
 * Unlike other packet formats, DriveWire does not encode enough
 * structural information to deserialize a complete packet up front.
 * Parameters and data are therefore deserialized lazily as they are
 * requested. Parameter values are read from the bus on first access
 * and cached for subsequent accesses.
 *
 * The only API difference from FujiBusPacket is setDataLength(). The
 * trailing data field has no self-describing length, so its size must
 * be supplied before data() or dataAsString() can be used. Code using
 * the transaction_* helpers does not need to call setDataLength()
 * directly, as those helpers determine the length automatically.
 */

class FujiAdamPacket
{
private:
    fujiDeviceID_t _device;
    adamPacketType_t _type;
    mutable std::optional<ByteBuffer> _payload;
    mutable uint8_t _payload_checksum;
    mutable std::optional<uint8_t> _unit;
    mutable std::optional<fujiCommandID_t> _command;
    mutable std::vector<uint32_t> _params;
    mutable unsigned _paramSize;
    mutable std::optional<ByteBuffer> _data;

    struct PacketParamProxy
    {
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
         *   - Convert DriveWire's big-endian encoding to native byte order.
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
         *
         * The param8(), param16(), and param32() methods provide explicit
         * alternatives when the implicit conversion is undesirable or the
         * destination type is not obvious.
         */

        size_t index;
        const FujiAdamPacket *packet;

        // These tell the compiler: "Run this code if the destination matches my type"
        operator bool() const {
            return static_cast<uint8_t>(*this) != 0;
        }

        // These tell the compiler exactly how to handle direct equality checks
        // against any integer type without triggering conversion rule debates.

        bool operator==(uint8_t val) const {
            return static_cast<uint8_t>(*this) == val;
        }

        bool operator==(uint16_t val) const {
            return static_cast<uint16_t>(*this) == val;
        }

        inline operator uint8_t() const {
            return packet->getParam(index, sizeof(uint8_t));
        }

        inline operator uint16_t() const {
            return packet->getParam(index, sizeof(uint16_t));
        }

        inline operator uint32_t() const {
            return packet->getParam(index, sizeof(uint32_t));
        }
    };

    friend PacketParamProxy;

    uint32_t getParam(size_t index, size_t psize) const;
    void fillParams(size_t count, size_t psize) const;

public:
    FujiAdamPacket(uint8_t dest) : _device(static_cast<fujiDeviceID_t>(dest & 0x0F)),
                                   _type(static_cast<adamPacketType_t>(dest >> 4)) {};

    fujiDeviceID_t device() const { return _device; }
    adamPacketType_t type() const { return _type; }
    fujiCommandID_t command() const;

    PacketParamProxy param(size_t index) const { return PacketParamProxy{ index, this }; }

    // Completes deserialization by reading the trailing data field once its
    // length has been determined from command-specific context.
    void setPayloadLength(const size_t len) const;

    const std::optional<ByteBuffer>& data() const;
    const std::optional<const std::string> dataAsString() const {
        auto d = data();
        return std::string(reinterpret_cast<const char *>(d->data()), d->size());
    }

    // Explicit alternatives to the implicit PacketParamProxy conversions.
    // These may be preferred where the destination type is not obvious.
    uint8_t  param8(int idx)  const { return (uint8_t)param(idx); }
    uint16_t param16(int idx) const { return (uint16_t)param(idx); }
    uint32_t param32(int idx) const { return (uint32_t)param(idx); }

    // Delete copy semantics to prevent pass-by-value bugs
    FujiAdamPacket(const FujiAdamPacket&) = delete;
    FujiAdamPacket& operator=(const FujiAdamPacket&) = delete;
};

/**
 * Design notes:
 *
 * - Parameters are decoded lazily and cached on first access. Parameters
 *   may be requested in any order prior to calling data(). If a later
 *   parameter is requested first, all preceding parameters are
 *   automatically decoded and cached as needed.
 *
 * - This allows parameters to be passed directly to a function without
 *   first storing them in temporary variables, e.g.
 *
 *       device_method(frame.param(0), frame.param(1), frame.param(2));
 *
 *   The packet guarantees that parameters are read from the bus in
 *   protocol order regardless of the compiler's argument evaluation
 *   order.
 *
 * - Protocol-specific packet variations are handled transparently. For
 *   example, packet types that include a unit byte before the command
 *   byte are decoded correctly regardless of whether unit() or
 *   command() is accessed first.
 *
 * - Multi-byte parameters are automatically converted from DriveWire's
 *   big-endian encoding to the native host byte order.
 */

#endif /* DRIVEWIREPACKET_H */

#endif /* BUILD_ADAM */
