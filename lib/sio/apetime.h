#ifndef APETIME_H
#define APETIME_H

#include "sio.h"

class sioApeTime : public sioDevice
{
public:
    void sio_process(uint32_t commanddata, uint8_t checksum) override;
    virtual void sio_status() override;

private:
    struct 
    {
        unsigned short gmt;
        unsigned short dst;    
    } _tz;

    void _sio_time();
    void _sio_timezone();
};

#endif // APETIME_H
