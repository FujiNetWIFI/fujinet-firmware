#ifdef BUILD_ADAM

#include "adamClock.h"

#include "../../include/debug.h"

adamClock platformClock;

AdamNetStatus adamClock::deviceStatus()
{
    AdamNetStatus status;

    status.length = 1024;
    status.devtype = ADAMNET_DEVTYPE::CHAR;
    status.status = 0;

    return status;
}

std::optional<std::string> adamClock::fujidev_read_tz()
{
    // Params and payload share one buffer; reading the length prefix off
    // leaves data() holding just the timezone.
    _packet->param16(0);

    return read_tz_from_payload();
}

#endif /* BUILD_ADAM */
