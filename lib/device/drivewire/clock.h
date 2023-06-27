#ifndef APETIME_H
#define APETIME_H

#include "bus.h"

class drivewireClock : public virtualDevice
{
private:
    void _sio_get_time(bool use_timezone);
    void _sio_set_tz();

public:
    void drivewire_process(uint32_t commanddata, uint8_t checksum);
};

#endif // APETIME_H
