#ifndef APETIME_H
#define APETIME_H

#include "sio.h"

class sioApeTime : public sioDevice
{
    public:
    void sio_process(uint32_t commanddata, uint8_t checksum) override;
    virtual void sio_status() override;

    private:

    union 
    {
        struct 
        {
            unsigned short gmt;
            unsigned short dst;    
        };
        uint8_t rawData[4];
    } tz;

    void sio_time();
    void sio_timezone();
};


#endif /* APETIME_H */