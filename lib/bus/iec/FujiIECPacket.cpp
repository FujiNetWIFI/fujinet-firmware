#ifdef BUILD_IEC

#include "FujiIECPacket.h"

namespace {
    // write `size` bytes of `value` (1,2,4) in little-endian
    inline void write_le(ByteBuffer& buf, std::uint32_t value, std::size_t size)
    {
        for (std::size_t i = 0; i < size; ++i)
            buf.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF));
    }
} // namespace

uint32_t FujiIECPacket::getParam(size_t index, size_t psize) const
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

void FujiIECPacket::fillParams(size_t count, size_t psize) const
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

const std::optional<ByteBuffer>& FujiIECPacket::data() const
{
    if (!_data.has_value())
    {
        assert(_payload.has_value());
        _data = _payload;
    }
    return _data;
}

ByteBuffer FujiIECPacket::serialize() const
{
    ByteBuffer output;

    if (!_params.empty())
    {
        unsigned idx, val, lenParams;
        const PacketParam *param;


        for (idx = 0, lenParams = _params.size(); idx < lenParams; idx++)
        {
            param = &_params[idx];
            write_le(output, param->value, param->size);
        }
    }

    if (_data)
        output.insert(output.end(), _data->begin(), _data->end());

    return output;
}

void FujiIECPacket::setPayload(ByteBuffer &payload)
{
    assert(!_payload.has_value());
    _payload = payload;
    return;
}

#endif /* BUILD_IEC */
