#ifndef IECCLOCK_H
#define IECCLOCK_H

#include "../../bus/bus.h"

#define TC_SIZE 256 // size of returned time string.

class iecClock : public virtualDevice
{
    private:
    
    time_t ts;
    std::string tf;

    public:

    iecClock();
    ~iecClock();

    device_state_t process();

    void iec_open();
    void iec_close();
    void iec_reopen();
    void iec_reopen_talk();
    void iec_reopen_listen();

    void set_timestamp(std::string s);
    void set_timestamp_format(std::string s);

};

#endif /* IECCLOCK_H */