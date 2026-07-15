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
    std::optional<std::string> read_tz_from_host(uint16_t bufsz);
    void set_fn_tz(uint16_t bufsz);
    void set_alternate_tz(uint16_t bufsz);

public:
    bool processCommand(const FujiDWPacket &packet) override;
};

extern drivewireClock platformClock;

#endif // DRIVEWIRE_CLOCK_H
