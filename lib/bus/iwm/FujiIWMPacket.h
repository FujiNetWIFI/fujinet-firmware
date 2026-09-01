#ifndef FUJIIWMPACKET_H
#define FUJIIWMPACKET_H

#include "PacketParamProxy.h"
#include "spCommandID.h"
#include "spCode.h"
#include "fujiCommandID.h"
#include "global_types.h"

#include <optional>
#include <cassert>
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
  mutable std::optional<ByteBuffer> _data;

  using ParamProxy = PacketParamProxy<FujiIWMPacket>;
  friend ParamProxy;

  uint32_t getParam(size_t index, size_t psize) const;
  void fillParams(size_t count, size_t psize) const;

public:
  SmartPortFrame frame;

  FujiIWMPacket() = default;

  void decode(uint8_t *raw);

  uint8_t device() const { return frame.sp_dev_id; }
  fujiCommandID_t command() const { return frame.control_status.fuji.command; }

  uint8_t unit() const;

  ParamProxy param(size_t index) const { return ParamProxy{ index, this }; }

  const std::optional<ByteBuffer>& data() const;
  const std::optional<const std::string> dataAsString() const {
    auto d = data();
    return std::string(reinterpret_cast<const char *>(d->data()), d->size());
  }

  // Explicit alternatives to the implicit ParamProxy conversions.
  // These may be preferred where the destination type is not obvious.
  uint8_t  param8(size_t index)  const { return (uint8_t) param(index); }
  uint16_t param16(size_t index) const { return (uint16_t) param(index); }
  uint32_t param32(size_t index) const { return (uint32_t) param(index); }

  // Delete copy semantics to prevent pass-by-value bugs
  FujiIWMPacket(const FujiIWMPacket&) = delete;
  FujiIWMPacket& operator=(const FujiIWMPacket&) = delete;
};

#endif /* FUJIIWMPACKET_H */
