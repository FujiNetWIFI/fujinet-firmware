#ifdef BUILD_ADAM

#include "FujiAdamPacket.h"

namespace {
    // write `size` bytes of `value` (1,2,4) in little-endian
    inline void write_le(ByteBuffer& buf, std::uint32_t value, std::size_t size)
    {
        for (std::size_t i = 0; i < size; ++i)
            buf.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF));
    }
} // namespace

uint32_t FujiAdamPacket::getParam(size_t index, size_t psize) const
{
    size_t count;

    assert(psize == 1 || psize == 2 || psize == 4);
    assert(index < _params.size() || _params.size() == 0 || _paramSize == psize);
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
        _params.emplace_back(val, psize);
    }
}

fujiCommandID_t FujiAdamPacket::command() const
{
    if (!_command.has_value()) {
        _command = static_cast<fujiCommandID_t>((*_payload)[0]);
        _payload->erase(_payload->begin(), _payload->begin() + 1);
    }
    return *_command;
}

error_is_true FujiAdamPacket::setPayload(ByteBuffer &payload, uint8_t checksum)
{
    assert(_type == APT::MN_SEND);
    assert(!_payload.has_value());

    _payload = payload;
    _payload_checksum = checksum;
    RETURN_ERROR_IF(_payload_checksum != calcChecksum(_payload.value()));
}

const std::optional<ByteBuffer>& FujiAdamPacket::data() const
{
    if (!_data.has_value())
        _data = _payload;
    return _data;
}

ByteBuffer FujiAdamPacket::serialize() const
{
    size_t paramsSize = 0;
    for (const auto& p : _params)
        paramsSize += p.size;

    size_t dataSize = _data ? _data->size() : 0;
    size_t payloadSize = paramsSize + dataSize;
    if (_command.has_value())
        payloadSize++;
    bool hasPayload = payloadSize > 0;
    bool hasLen = hasPayload && (_type != APT::NM_STATUS);

    size_t totalSize = 1 /*dest*/
        + (hasLen ? 2 : 0)
        + payloadSize
        + (hasPayload ? 1 : 0 /*checksum*/);

    ByteBuffer output;
    output.reserve(totalSize);
    output.push_back(static_cast<uint8_t>((static_cast<uint8_t>(_type) << 4)
                                          | static_cast<uint8_t>(_device)));

    if (hasLen)
    {
        u16be_t len;
        len = static_cast<uint16_t>(payloadSize);
        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(&len);
        output.insert(output.end(), ptr, ptr + sizeof(len));
    }

    ByteBuffer payload;
    payload.reserve(payloadSize);

    if (_command.has_value())
        payload.push_back(static_cast<uint8_t>(*_command));

    for (const auto& p : _params)
        write_le(payload, p.value, p.size);

    if (_data)
        payload.insert(payload.end(), _data->begin(), _data->end());

    if (hasPayload)
    {
        output.insert(output.end(), payload.begin(), payload.end());
        output.push_back(calcChecksum(payload));
    }

    return output;
}

std::uint8_t FujiAdamPacket::calcChecksum(const ByteBuffer& buf) const
{
    uint8_t checksum = 0x00;
    size_t len = buf.size();

    for (size_t idx = 0; idx < len; idx++)
        checksum ^= buf[idx];

    return checksum;
}

#endif /* BUILD_ADAM */
