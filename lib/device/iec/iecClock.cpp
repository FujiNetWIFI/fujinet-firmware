#ifdef BUILD_IEC

#include "iecClock.h"

// fujinet-lib sends the time zone's length as a 16-bit param, then the name
std::optional<std::string> iecClock::fujidev_read_tz()
{
    std::optional<std::string> tz = read_tz_by_length(_packet->param16(0));

    if (tz)
        tz = SYSTEM_BUS.nativeTextToUnicode(*tz);
    return tz;
}

#endif /* BUILD_IEC */
