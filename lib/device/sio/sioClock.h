#ifndef SIOCLOCK_H
#define SIOCLOCK_H

#include <optional>
#include <string>

#include "../fujiClock/fujiClock.h"

class sioClock : public fujiClock
{
protected:
    std::optional<std::string> read_tz() override;
    bool alt_requested() override;

public:
    void sio_process(const FujiSIOPacket &packet) override;
    void sio_status(const FujiSIOPacket &packet) override {};
};

extern sioClock platformClock;

#endif // SIOCLOCK_H
