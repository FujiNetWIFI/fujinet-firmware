#ifdef BUILD_COCO

#include "drivewireClock.h"

#include <optional>

#include "../../bus/drivewire/drivewire.h"
#include "../../include/debug.h"

drivewireClock platformClock;

std::optional<std::string> drivewireClock::fujidev_read_tz()
{
    uint16_t bufsz = be16toh(_packet->param(0));

    return read_tz_by_length(bufsz);
}

#endif /* BUILD_COCO */
