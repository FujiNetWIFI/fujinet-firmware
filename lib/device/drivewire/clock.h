#ifndef DRIVEWIRE_CLOCK_H
#define DRIVEWIRE_CLOCK_H

#include <optional>
#include <string>

#include "bus.h"
#include "../../clock/Clock.h"

class drivewireClock : public virtualDevice
{
private:
    std::string alternate_tz = "";
    std::optional<std::string> read_tz_from_host();
    void set_fn_tz();
    void set_alternate_tz();

public:
    void process();
};

extern drivewireClock platformClock;

#endif // DRIVEWIRE_CLOCK_H
