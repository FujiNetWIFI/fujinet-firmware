#ifdef BUILD_LYNX

#include "FujiLynxPacket.h"
#include "debug.h"

namespace {
    // write `size` bytes of `value` (1,2,4) in little-endian
    inline void write_le(ByteBuffer& buf, std::uint32_t value, std::size_t size)
    {
        for (std::size_t i = 0; i < size; ++i)
            buf.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF));
    }
} // namespace

uint32_t FujiLynxPacket::getParam(size_t index, size_t psize) const
{
    size_t count;

    assert(psize == 1 || psize == 2 || psize == 4);
    assert(_params.size() == 0 || _paramSize == psize);
    if (index >= _params.size()) {
        assert(_payload.has_value());
        assert(!_data.has_value());
        _paramSize = psize;
        count = index - _params.size() + 1;
        assert(count * psize <= _payload->size());
        fillParams(count, psize);
    }
    return _params[index].value;
}

void FujiLynxPacket::fillParams(size_t count, size_t psize) const
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
        _params.emplace_back(val, psize);
    }
}

fujiCommandID_t FujiLynxPacket::command() const
{
    if (!_command.has_value()) {
        _command = static_cast<fujiCommandID_t>((*_payload)[0]);
        _payload->erase(_payload->begin(), _payload->begin() + 1);
    }
    return *_command;
}

const std::optional<ByteBuffer>& FujiLynxPacket::data() const
{
  if (!_data.has_value())
    _data = _payload;
  return _data;
}

bool FujiLynxPacket::parse(const ByteBuffer& input)
{
    // Extract header from the front of input
    u16be_t len;
    memcpy(&len, input.data() + 1, sizeof(len));
    if (len != input.size() - 4)
        return false;

    // Verify checksum:
    // - ck1 is the transmitted checksum
    // - ck2 is computed with the checksum byte zeroed
    const std::uint8_t ck1 = input.back();
    ByteBuffer payload(input.begin() + 3, input.end() - 1);
    const std::uint8_t ck2 = calcChecksum(payload);

    if (ck1 != ck2)
        return false;

    _device  = static_cast<fujiDeviceID_t>(input.front());
    _payload = payload;
    _payload_checksum = ck1;

    return true;
}

ByteBuffer FujiLynxPacket::serialize() const
{
    ByteBuffer output;

    if (_command == FUJICMD_SEND_RESPONSE)
    {
        for (const auto& p : _params)
            write_le(output, p.value, p.size);

        if (_data)
            output.insert(output.end(), _data->begin(), _data->end());

        uint8_t ck = calcChecksum(output);
        u16be_t len;
        len = static_cast<uint16_t>(output.size());
        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(&len);
        output.insert(output.begin(), ptr, ptr + sizeof(len));
        output.push_back(ck);
    }
    else
        output.push_back(static_cast<uint8_t>(*_command));

    return output;
}

std::uint8_t FujiLynxPacket::calcChecksum(const ByteBuffer& buf) const
{
    uint8_t checksum = 0x00;

    for (unsigned idx = 0; idx < buf.size(); idx++)
        checksum ^= buf[idx];

    return checksum;
}

// ------------------ Factory ------------------
std::unique_ptr<FujiLynxPacket> FujiLynxPacket::fromSerialized(const ByteBuffer& input)
{
    auto packet = std::make_unique<FujiLynxPacket>();
    if (!packet->parse(input))
        return nullptr;
    return packet;
}

#endif /* BUILD_LYNX */
