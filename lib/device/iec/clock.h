#ifndef IECCLOCK_H
#define IECCLOCK_H

#include "bus.h"
#include "../../bus/iec/IECFileDevice.h"

#define TC_SIZE 256 // size of returned time string.

class iecClock : public IECDevice
{
    private:

    time_t ts;
    std::string tf, payload, response;
    size_t responsePtr;

protected:
    virtual void talk(uint8_t secondary) override;
    virtual void listen(uint8_t secondary) override;
    virtual void untalk() override;
    virtual void unlisten() override;
    virtual int8_t canWrite() override;
    virtual int8_t canRead() override;
    virtual void write(uint8_t data, bool eoi) override;
    virtual uint8_t read() override;
    virtual void task() override;
    virtual void reset() override;

    public:

    iecClock(uint8_t devnr);
    ~iecClock();

    void set_timestamp(std::string s);
    void set_timestamp_format(std::string s);

};

#endif /* IECCLOCK_H */
