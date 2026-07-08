#ifdef BUILD_COCO

#include "FujiDWPacket.h"

#include "bus.h"

uint32_t FujiDWPacket::getParam(size_t index, size_t psize)
{
    size_t count;

    assert(psize == 1 || psize == 2 || psize == 4);
    assert(_params.size() == 0 || _paramSize == psize);
    if (!_command.has_value()) {
        // Need to read command byte before reading params
        command();
    }
    if (index >= _params.size()) {
        assert(!_data.has_value());
        _paramSize = psize;
        count = index - _params.size() + 1;
        if (_opcode == OP::NET && _params.size() == 0 && count * psize < 2) {
            // Need to make sure we get both parameter bytes on network packets
            count = 2 / psize;
        }
        fillParams(count, psize);
    }
    return _params[index];
}

void FujiDWPacket::fillParams(size_t count, size_t psize)
{
    uint32_t val;
    size_t idx;

    for (idx = 0; idx < count; idx++)
    {
        switch (psize) {
        case 1:
            val = SYSTEM_BUS.read();
            break;
        case 2:
            {
                uint16_t ev;
                SYSTEM_BUS.read(&ev, sizeof(ev));
                val = be16toh(ev);
            }
            break;
        case 4:
            {
                uint32_t ev;
                SYSTEM_BUS.read(&ev, sizeof(ev));
                val = be32toh(ev);
            }
            break;
        }
        _params.push_back(val);
    }
}

fujiCommandID_t FujiDWPacket::command()
{
    if (!_command.has_value()) {
        assert(!_data.has_value() && _params.size() == 0);
        if (_opcode == OP::NET && !_unit.has_value()) {
            // Need to read unit before command
            unit();
        }
        _command = static_cast<fujiCommandID_t>(SYSTEM_BUS.read());
    }
    return *_command;
}

uint8_t FujiDWPacket::unit()
{
    if (!_unit.has_value()) {
        assert(_opcode == OP::NET && !_command.has_value());
        _unit = SYSTEM_BUS.read();
    }
    return *_unit;
}

void FujiDWPacket::setDataLength(const size_t len)
{
    size_t rlen;


    assert(!_data.has_value());

    if (_opcode == OP::NET && _params.size() == 0) {
        // Read off the parameter bytes that occur before data
        fillParams(2, 1);
    }

    _data.emplace(len, 0);
    rlen = SYSTEM_BUS.read(_data->data(), _data->size());
    if (rlen != len)
        _data->resize(rlen);
}

#endif /* BUILD_COCO */
