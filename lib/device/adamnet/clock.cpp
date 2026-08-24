#ifdef BUILD_ADAM

#include "clock.h"

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

std::optional<std::string> adamClock::read_tz()
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);

    const auto &d = _packet->data();
    if (!d.has_value())
    {
        Debug_printv("ERROR: No timezone sent");
        SYSTEM_BUS.transaction_error();
        return std::nullopt;
    }

    std::string tz(reinterpret_cast<const char *>(d->data()), d->size());
    while (!tz.empty() && tz.back() == '\0')
        tz.pop_back();

    SYSTEM_BUS.transaction_success();
    return tz;
}

bool adamClock::alt_requested()
{
    return _packet->param8(0) == 0x01;
}

void adamClock::adamnet_control_send(const FujiAdamPacket &packet)
{
    if (!processCommand(packet))
        SYSTEM_BUS.transaction_error();
}

#endif /* BUILD_ADAM */
