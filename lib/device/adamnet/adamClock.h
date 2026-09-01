#ifndef ADAMCLOCK_H
#define ADAMCLOCK_H

#include <optional>
#include <string>

#include "../fujiClock/fujiClock.h"

class adamClock : public fujiClock
{
protected:
    std::optional<std::string> fujidev_read_tz() override;

public:
    void adamnet_control_send(const FujiAdamPacket &packet) override { dispatch(packet); }
    AdamNetStatus deviceStatus() override;
};

extern adamClock platformClock;

#endif // ADAMCLOCK_H
