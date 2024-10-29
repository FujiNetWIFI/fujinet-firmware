#ifndef IECCLOCK_H
#define IECCLOCK_H

#include "../../bus/bus.h"

#define TC_SIZE 256 // size of returned time string.

class iecClock : public virtualDevice
{
    private:

    time_t ts;
    std::string tf;

protected:
    virtual device_state_t openChannel(/*int chan, IECPayload &payload*/) override;
    virtual device_state_t closeChannel(/*int chan*/) override;
    virtual device_state_t readChannel(/*int chan*/) override;
    virtual device_state_t writeChannel(/*int chan, IECPayload &payload*/) override;

    public:

    iecClock();
    ~iecClock();

#if 0
    device_state_t process();
#endif

    void iec_open();
    void iec_close();
    void iec_reopen();
    void iec_reopen_talk();
    void iec_reopen_listen();

    void set_timestamp(std::string s);
    void set_timestamp_format(std::string s);

};

#endif /* IECCLOCK_H */
