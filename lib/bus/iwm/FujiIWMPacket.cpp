#ifdef BUILD_APPLE

#include "FujiIWMPacket.h"
#include "iwm.h"

void FujiIWMPacket::decode(uint8_t *raw)
{
  smartport.decode_data_packet(raw, reinterpret_cast<uint8_t *>(&frame));
  _params.clear();
  _data = std::nullopt;

  switch (frame.sp_command) {
  case SP_CMD_CONTROL:
  case SP_CMD_WRITE:
  case SP_CMD_WRITEBLOCK:
  case SP_ECMD_CONTROL:
  case SP_ECMD_WRITE:
  case SP_ECMD_WRITEBLOCK:
  _decoded.resize(MAX_DATA_LEN);
  _decoded.resize(smartport.decode_data_packet(_decoded.data()));
    break;
  default:
    break;
  }
}

uint32_t FujiIWMPacket::getParam(size_t index, size_t psize) const
{
  size_t count;

  assert(psize == 1 || psize == 2 || psize == 4);
  assert(_params.size() == 0 || _paramSize == psize);
  if (index >= _params.size()) {
    assert(!_data.has_value());
    _paramSize = psize;
    count = index - _params.size() + 1;
    assert(count * psize <= _decoded.size());
    fillParams(count, psize);
  }
  return _params[index];
}

void FujiIWMPacket::fillParams(size_t count, size_t psize) const
{
  uint32_t val;
  size_t idx;

  for (idx = 0; idx < count; idx++)
  {
    switch (psize) {
    case 1:
      val = _decoded[0];
      _decoded.erase(_decoded.begin(), _decoded.begin() + 1);
      break;
    case 2:
      {
        uint16_t ev;
        __builtin_memcpy(&ev, &_decoded[0], sizeof(ev));
        val = le16toh(ev);
        _decoded.erase(_decoded.begin(), _decoded.begin() + sizeof(ev));
      }
      break;
    case 4:
      {
        uint32_t ev;
        __builtin_memcpy(&ev, &_decoded[0], sizeof(ev));
        val = le32toh(ev);
        _decoded.erase(_decoded.begin(), _decoded.begin() + sizeof(ev));
      }
      break;
    }
    _params.push_back(val);
  }
}

const std::optional<std::span<const std::uint8_t>>& FujiIWMPacket::data() const
{
  if (!_data.has_value())
    _data = _decoded;
  return _data;
}

#endif /* BUILD_APPLE */
