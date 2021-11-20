#ifndef APETIME_H
#define APETIME_H

#include "bus.h"

class sioApeTime : public sioDevice
{
private:
    void _sio_get_time(bool use_timezone);
    void _sio_set_tz();

public:
    void sio_process(uint32_t commanddata, uint8_t checksum) override;
    virtual void sio_status() override {};
};

#endif // APETIME_H
