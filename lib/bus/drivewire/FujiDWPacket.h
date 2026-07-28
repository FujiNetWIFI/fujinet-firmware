#ifndef FUJIDWPACKET_H
#define FUJIDWPACKET_H

#ifdef BUILD_COCO

#include "PacketParamProxy.h"
#include "opcode.h"
#include "fujiCommandID.h"
#include "global_types.h"

#include <optional>
#include <vector>
#include <cassert>
#include <string>

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

class FujiDWPacket
{
private:
    dwOpcode_t _opcode;
    mutable std::optional<uint8_t> _unit;
    mutable std::optional<fujiCommandID_t> _command;
    mutable std::vector<uint32_t> _params;
    mutable unsigned _paramSize;
    mutable std::optional<ByteBuffer> _data;

    using ParamProxy = PacketParamProxy<FujiDWPacket>;
    friend ParamProxy;

    uint32_t getParam(size_t index, size_t psize) const;
    void fillParams(size_t count, size_t psize) const;

public:
    FujiDWPacket(dwOpcode_t opcode) : _opcode(opcode) {};
    ~FujiDWPacket() {
        if (_opcode == OP::NET && _params.size() == 0) {
            // Read off the parameter bytes that were never consumed
            fillParams(2, 1);
        }
    }

    dwOpcode_t device() const { return _opcode; }
    fujiCommandID_t command() const;

    uint8_t unit() const;

    ParamProxy param(size_t index) const { return ParamProxy{ index, this }; }

    // Completes deserialization by reading the trailing data field once its
    // length has been determined from command-specific context.
    void setDataLength(const size_t len) const;

    const std::optional<ByteBuffer>& data() const {
        assert(_data.has_value());
        return _data;
    }

    const std::optional<std::string> dataAsString() const
    {
        if (!_data) return std::nullopt;
        return std::string(_data->begin(), _data->end());
    }

    // Explicit alternatives to the implicit ParamProxy conversions.
    // These may be preferred where the destination type is not obvious.
    uint8_t  param8(int idx)  const { return (uint8_t)param(idx); }
    uint16_t param16(int idx) const { return (uint16_t)param(idx); }
    uint32_t param32(int idx) const { return (uint32_t)param(idx); }

    // Delete copy semantics to prevent pass-by-value bugs
    FujiDWPacket(const FujiDWPacket&) = delete;
    FujiDWPacket& operator=(const FujiDWPacket&) = delete;
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

#endif /* BUILD_COCO */

#endif /* FUJIDWPACKET_H */
