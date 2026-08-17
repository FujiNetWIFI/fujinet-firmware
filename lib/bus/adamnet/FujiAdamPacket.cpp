#ifdef BUILD_ADAM

#include "FujiAdamPacket.h"
#include "bus.h"

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
    SYSTEM_BUS.start_time = GET_TIMESTAMP();
    SYSTEM_BUS.write((((unsigned) APT::NM_ACK) << 4) | _device);
    SYSTEM_BUS.flush();
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
    bool hasPayload = payloadSize > 0;
    bool hasLen = hasPayload && (_type != APT::NM_STATUS);

    size_t totalSize = 1 /*dest*/
        + (hasLen ? 2 : 0)
                + payloadSize
        + (hasPayload ? 1 : 0 /*checksum*/);

    ByteBuffer output;
    output.reserve(totalSize);
    output.push_back(static_cast<uint8_t>((static_cast<uint8_t>(_type) << 4) | _device));

    if (hasLen)
    {
        u16be_t len;
        len = static_cast<uint16_t>(payloadSize);
        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(&len);
        output.insert(output.end(), ptr, ptr + sizeof(len));
    }

    size_t payloadStart = output.size();

    for (const auto& p : _params)
        write_le(output, p.value, p.size);

    if (_data)
    {
        output.insert(output.end(), _data->begin(), _data->end());
    }

    if (hasPayload)
    {
        uint8_t ck = 0;
        size_t idx, end;
        for (idx = payloadStart, end = output.size(); idx < end; idx++)
            ck ^= output[idx];
        output.push_back(ck);
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
