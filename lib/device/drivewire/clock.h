#ifndef DRIVEWIRE_CLOCK_H
#define DRIVEWIRE_CLOCK_H

#include <optional>
#include <string>

#include "../fujiClock/fujiClock.h"

class drivewireClock : public fujiClock
{
protected:
    std::optional<std::string> read_tz() override;
    bool alt_requested() override;
};

extern drivewireClock platformClock;

#endif // DRIVEWIRE_CLOCK_H
