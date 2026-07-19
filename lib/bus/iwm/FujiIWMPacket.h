#ifndef FUJIIWMPACKET_H
#define FUJIIWMPACKET_H

#include "spCommandID.h"
#include "spCode.h"
#include "fujiCommandID.h"
#include "global_types.h"

#include <optional>
#include <cassert>
#include <span>
#include <string>

struct SmartPortFrame
{
  spCommandID_t sp_command;
  uint8_t param_count;
  uint8_t sp_dev_id;
  uint8_t unknown;

  union {
    struct {
      union {
        spCode_t code;
        struct {
          fujiCommandID_t command;
          uint8_t network_unit;
        } fuji;
      };
    } control_status;
    struct {
      u24le_t num;
    } block_rw;
    struct {
      u16le_t length;
      union {
        u24le_t address;
        struct {
          uint8_t network_unit;
        } fuji;
      };
    } char_rw;
    // format, init, open, close do not have any parameters
  };
} __attribute__((packed));
static_assert(sizeof(SmartPortFrame) == 9, "SmartPortFrame must be 9 bytes");

class FujiIWMPacket
{
private:
  mutable std::vector<uint32_t> _params;
  mutable unsigned _paramSize;
  mutable ByteBuffer _decoded;
  mutable std::optional<std::span<const uint8_t>> _data;

  struct PacketParamProxy
  {
    size_t index;
    const FujiIWMPacket *packet;

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
  SmartPortFrame frame;

  FujiIWMPacket() = default;

  void decode(uint8_t *raw);

  uint8_t device() const { return frame.sp_dev_id; }
  fujiCommandID_t command() const { return frame.control_status.fuji.command; }

  PacketParamProxy param(size_t index) const { return PacketParamProxy{ index, this }; }

  const std::optional<std::span<const std::uint8_t>>& data() const;
  const std::optional<const std::string> dataAsString() const {
    auto d = data();
    return std::string(reinterpret_cast<const char *>(d->data()), d->size());
  }

  // Explicit alternatives to the implicit PacketParamProxy conversions.
  // These may be preferred where the destination type is not obvious.
  uint8_t  param8(size_t index)  const { return (uint8_t) param(index); }
  uint16_t param16(size_t index) const { return (uint16_t) param(index); }
  uint32_t param32(size_t index) const { return (uint32_t) param(index); }

  // Delete copy semantics to prevent pass-by-value bugs
  FujiIWMPacket(const FujiIWMPacket&) = delete;
  FujiIWMPacket& operator=(const FujiIWMPacket&) = delete;

};

#endif /* FUJIIWMPACKET_H */
