#ifdef BUILD_RS232

#include "clock.h"

#include "../../include/debug.h"

rs232Clock platformClock;

std::optional<std::string> rs232Clock::read_tz()
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);

    auto s = _packet->dataAsString();
    if (!s || s->empty()) {
        Debug_printv("ERROR: No timezone sent");
        SYSTEM_BUS.transaction_success();
        return std::nullopt;
    }

    std::string tz = s.value();
    while (!tz.empty() && tz.back() == '\0')
        tz.pop_back();

    SYSTEM_BUS.transaction_success();
    return tz;
}

bool rs232Clock::alt_requested()
{
    return _packet->paramCount() > 0 && _packet->param(0) == 0x01;
}

void rs232Clock::rs232_process(const FujiBusPacket &packet)
{
    if (!processCommand(packet))
        SYSTEM_BUS.transaction_error();
}

#endif /* BUILD_RS232 */
