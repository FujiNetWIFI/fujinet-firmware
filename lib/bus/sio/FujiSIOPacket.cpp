#ifdef BUILD_ATARI

#include "FujiSIOPacket.h"
#include "sio.h"
#include "fnSystem.h"

uint16_t FujiSIOPacket::getParam(size_t index, size_t psize) const
{
    size_t count;

    assert(psize == 1 || psize == 2);
    assert(_params.size() == 0 || _paramSize == psize);
    if (index >= _params.size())
    {
        _paramSize = psize;
        if (_paramSize == 1)
        {
            _params.push_back(frame.aux1);
            _params.push_back(frame.aux2);
        }
        else
            _params.push_back(frame.aux12);
    }
    return _params[index];
}

error_is_true FujiSIOPacket::setDataLength(const size_t len) const
{
    size_t rlen;


    assert(!_data.has_value());

#ifndef ESP_PLATFORM
    if (SYSTEM_BUS.isBoIP())
    {
        SYSTEM_BUS.netsio_write_size(len); // set hint for NetSIO
    }
#endif /* ! ESP_PLATFORM */

    _data.emplace(len, 0);
    rlen = SYSTEM_BUS.read(_data->data(), _data->size());
    if (rlen != len)
        _data->resize(rlen);

    // Wait for checksum
    while (SYSTEM_BUS.available() <= 0)
        fnSystem.yield();
    uint8_t ck_rcv = SYSTEM_BUS.read();

    uint8_t ck_tst = sio_checksum(_data->data(), _data->size());

    fnSystem.delay_microseconds(DELAY_T4);

    if (ck_rcv != ck_tst)
    {
        SYSTEM_BUS._sio_error();
        RETURN_ERROR_AS_TRUE();
    }

    SYSTEM_BUS._sio_ack();

    RETURN_SUCCESS_AS_FALSE();
}

#endif /* BUILD_ATARI */
