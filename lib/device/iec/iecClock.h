#ifndef IECCLOCK_H
#define IECCLOCK_H

#include "../fujiClock/fujiClock.h"
#include <ctime>
#include <optional>
#include <string>

#define TC_SIZE 256 // size of returned time string.

// Device 29. The FujiNet clock commands come on channel 15, through fujiClock.
// Any other channel is a raw clock: write a strftime format, or a Unix time
// for the next read, and read the time as text.
class iecClock : public fujiClock
{
    private:

    time_t ts;
    std::string tf, payload, response;
    size_t responsePtr;
    bool rawChannel = false; // this TALK or LISTEN isn't on channel 15

protected:
    std::optional<std::string> fujidev_read_tz() override;

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
