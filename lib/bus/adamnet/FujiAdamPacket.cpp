#ifdef BUILD_ADAM

#include "FujiAdamPacket.h"
#include "bus.h"

uint32_t FujiAdamPacket::getParam(size_t index, size_t psize) const
{
  size_t count;

  assert(psize == 1 || psize == 2 || psize == 4);
  assert(_params.size() == 0 || _paramSize == psize);
  if (index >= _params.size()) {
    assert(!_data.has_value());
    _paramSize = psize;
    count = index - _params.size() + 1;
    assert(count * psize <= _payload->size());
    fillParams(count, psize);
  }
  return _params[index];
}

void FujiAdamPacket::fillParams(size_t count, size_t psize) const
{
  uint32_t val;
  size_t idx;

  for (idx = 0; idx < count; idx++)
  {
    switch (psize) {
    case 1:
        val = (*_payload)[0];
      _payload->erase(_payload->begin(), _payload->begin() + 1);
      break;
    case 2:
      {
        uint16_t ev;
        __builtin_memcpy(&ev, _payload->data(), sizeof(ev));
        val = le16toh(ev);
        _payload->erase(_payload->begin(), _payload->begin() + sizeof(ev));
      }
      break;
    case 4:
      {
        uint32_t ev;
        __builtin_memcpy(&ev, _payload->data(), sizeof(ev));
        val = le32toh(ev);
        _payload->erase(_payload->begin(), _payload->begin() + sizeof(ev));
      }
      break;
    }
    _params.push_back(val);
  }
}

fujiCommandID_t FujiAdamPacket::command() const
{
    if (!_command.has_value()) {
        u16be_t len;
        SYSTEM_BUS.read(&len, sizeof(len));
        _command = static_cast<fujiCommandID_t>(SYSTEM_BUS.read());
        setPayloadLength(len - 1);
    }
    return *_command;
}

void FujiAdamPacket::setPayloadLength(const size_t len) const
{
    size_t rlen;


    assert(!_payload.has_value());

    _payload.emplace(len, 0);
    rlen = SYSTEM_BUS.read(_payload->data(), _payload->size());
    if (rlen != len)
        _payload->resize(rlen);
    _payload_checksum = SYSTEM_BUS.read();
    // FIXME - do something if checksum mismatch?
}

const std::optional<ByteBuffer>& FujiAdamPacket::data() const
{
  if (!_data.has_value())
    _data = _payload;
  return _data;
}

#endif /* BUILD_ADAM */
