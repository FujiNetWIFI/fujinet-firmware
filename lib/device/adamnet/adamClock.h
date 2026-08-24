#ifndef ADAMCLOCK_H
#define ADAMCLOCK_H

#include <optional>
#include <string>

#include "../fujiClock/fujiClock.h"

class adamClock : public fujiClock
{
protected:
    std::optional<std::string> read_tz() override;
    bool alt_requested() override;

public:
    void adamnet_control_send(const FujiAdamPacket &packet) override;
    AdamNetStatus deviceStatus() override;
};

extern adamClock platformClock;

#endif // ADAMCLOCK_H
