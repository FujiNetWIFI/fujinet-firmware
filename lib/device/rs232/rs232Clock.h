#ifndef RS232CLOCK_H
#define RS232CLOCK_H

#include <optional>
#include <string>

#include "../fujiClock/fujiClock.h"

class rs232Clock : public fujiClock
{
protected:
    std::optional<std::string> fujidev_read_tz() override;
    // Unlike the other buses, param() throws when the parameter is absent.
    bool fujidev_alt_requested() override
    {
        return _packet->paramCount() > 0 && _packet->param(0) == 0x01;
    }

public:
    void rs232_process(const FujiBusPacket &packet) override { dispatch(packet); }
};

extern rs232Clock platformClock;

#endif // RS232CLOCK_H
