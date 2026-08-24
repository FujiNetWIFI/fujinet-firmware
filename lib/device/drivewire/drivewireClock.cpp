#ifdef BUILD_COCO

#include "drivewireClock.h"

#include <algorithm>
#include <optional>

#include "../../bus/drivewire/drivewire.h"
#include "../../include/debug.h"

drivewireClock platformClock;

std::optional<std::string> drivewireClock::read_tz()
{
    uint16_t bufsz = be16toh(_packet->param(0));

    if (bufsz == 0) {
        Debug_printv("ERROR: No timezone sent");
        return std::nullopt;
    }

    std::string timezone(bufsz, 0);
    SYSTEM_BUS.transaction_accept(TRANS_STATE::WILL_GET);
    SYSTEM_BUS.transaction_get(timezone.data(), timezone.size());
    timezone.resize(std::min(timezone.find('\0'), timezone.size()));
    SYSTEM_BUS.transaction_success();
    return timezone;
}

bool drivewireClock::alt_requested()
{
    return _packet->param8(0) == 0x01;
}

#endif /* BUILD_COCO */
