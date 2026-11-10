#ifndef IECCLOCK_H
#define IECCLOCK_H

#include "../fujiClock/fujiClock.h"
#include <optional>
#include <string>

class iecClock : public fujiClock
{
protected:
    std::optional<std::string> fujidev_read_tz() override;

public:
    iecClock(uint8_t devnr) : virtualDevice(devnr) {}
};

#endif /* IECCLOCK_H */
