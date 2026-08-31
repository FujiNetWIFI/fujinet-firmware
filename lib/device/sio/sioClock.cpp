#ifdef BUILD_ATARI

#include "sioClock.h"

#include <optional>

#include "../../include/debug.h"

sioClock platformClock;

std::optional<std::string> sioClock::fujidev_read_tz()
{
    Debug_println("sioClock fujidev_read_tz");

    return read_tz_by_length(_packet->param16(0));
}

#endif /* BUILD_ATARI */
