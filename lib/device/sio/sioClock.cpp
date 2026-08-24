#ifdef BUILD_ATARI

#include "sioClock.h"

#include <optional>

#include "../../include/debug.h"

sioClock platformClock;

std::optional<std::string> sioClock::read_tz()
{
    Debug_println("sioClock read_tz");

    SYSTEM_BUS.transaction_accept(TRANS_STATE::WILL_GET);

    int bufsz = _packet->param16(0);
    if (bufsz <= 0) {
        Debug_printv("ERROR: No timezone sent");
        SYSTEM_BUS.transaction_success();
        return std::nullopt;
    }

    std::string timezone(bufsz, '\0');
    if (!SYSTEM_BUS.transaction_get(reinterpret_cast<uint8_t *>(&timezone[0]), bufsz)) {
        SYSTEM_BUS.transaction_error();
        return std::nullopt;
    }

    SYSTEM_BUS.transaction_success();
    return timezone;
}

bool sioClock::alt_requested()
{
    return _packet->param8(0) == 0x01;
}

void sioClock::sio_process(const FujiSIOPacket &packet)
{
    if (!processCommand(packet))
        SYSTEM_BUS.transaction_error();
}

#endif /* BUILD_ATARI */
