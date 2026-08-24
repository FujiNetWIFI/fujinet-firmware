#ifndef RS232CLOCK_H
#define RS232CLOCK_H

#include <optional>
#include <string>

#include "../fujiClock/fujiClock.h"

class rs232Clock : public fujiClock
{
protected:
    std::optional<std::string> read_tz() override;
    bool alt_requested() override;

public:
    void rs232_process(const FujiBusPacket &packet) override;
    void rs232_status(FujiStatusReq reqType) override {}
};

extern rs232Clock platformClock;

#endif // RS232CLOCK_H
