#ifndef SIOCLOCK_H
#define SIOCLOCK_H

#include <optional>
#include <string>

#include "../fujiClock/fujiClock.h"

class sioClock : public fujiClock
{
protected:
    std::optional<std::string> fujidev_read_tz() override;
public:
    void sio_process(const FujiSIOPacket &packet) override { dispatch(packet); }
    void sio_status(const FujiSIOPacket &packet) override {};
};

extern sioClock platformClock;

#endif // SIOCLOCK_H
