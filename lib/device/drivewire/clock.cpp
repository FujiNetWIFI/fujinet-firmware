#ifdef BUILD_COCO

#include "clock.h"

#include <cstring>
#include <ctime>
#include <optional>

#include "fnConfig.h"
#include "fujiCommandID.h"
#include "../../bus/drivewire/drivewire.h"
#include "../../include/debug.h"

drivewireClock platformClock;

std::optional<std::string> drivewireClock::read_tz_from_host(uint16_t bufsz)
{
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

void drivewireClock::set_fn_tz(uint16_t bufsz)
{
    auto result = read_tz_from_host(bufsz);
    if (result)
        Config.store_general_timezone(result->c_str());
}

void drivewireClock::set_alternate_tz(uint16_t bufsz)
{
    auto result = read_tz_from_host(bufsz);
    if (result)
        alternate_tz = result.value();
}

void drivewireClock::processCommand(FujiDWPacket &packet)
{
    uint8_t aux1;
    bool use_alt;

    switch (packet.command())
    {
    case APETIMECMD_GETTZTIME:
    case APETIMECMD_GETTIME: {
        aux1 = packet.param(0);
        use_alt = (packet.command() == APETIMECMD_GETTZTIME) || (aux1 == 0x01);
        auto t = Clock::get_current_time_apetime(
            Clock::tz_to_use(use_alt, alternate_tz, Config.get_general_timezone()));
        SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
        SYSTEM_BUS.transaction_send(t);
        break;
    }
    case APETIMECMD_SETTZ_ALT2: {
        aux1 = packet.param(0);
        use_alt = (aux1 == 0x01);
        auto t = Clock::get_current_time_simple(
            Clock::tz_to_use(use_alt, alternate_tz, Config.get_general_timezone()));
        SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
        SYSTEM_BUS.transaction_send(t);
        break;
    }
    case APETIMECMD_GET_SIMPLE_HUNDREDTHS: {
        aux1 = packet.param(0);
        use_alt = (aux1 == 0x01);
        auto t = Clock::get_current_time_simple_hundredths(
            Clock::tz_to_use(use_alt, alternate_tz, Config.get_general_timezone()));
        SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
        SYSTEM_BUS.transaction_send(t);
        break;
    }
    case APETIMECMD_GET_PRODOS: {
        aux1 = packet.param(0);
        use_alt = (aux1 == 0x01);
        auto t = Clock::get_current_time_prodos(
            Clock::tz_to_use(use_alt, alternate_tz, Config.get_general_timezone()));
        SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
        SYSTEM_BUS.transaction_send(t);
        break;
    }
    case APETIMECMD_GET_SOS: {
        aux1 = packet.param(0);
        use_alt = (aux1 == 0x01);
        std::string s = Clock::get_current_time_sos(
            Clock::tz_to_use(use_alt, alternate_tz, Config.get_general_timezone())) + '\0';
        SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
        SYSTEM_BUS.transaction_send(s);
        break;
    }
    case APETIMECMD_GET_ISO_LOCAL: {
        aux1 = packet.param(0);
        use_alt = (aux1 == 0x01);
        std::string s = Clock::get_current_time_iso(
            Clock::tz_to_use(use_alt, alternate_tz, Config.get_general_timezone())) + '\0';
        SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
        SYSTEM_BUS.transaction_send(s);
        break;
    }
    case APETIMECMD_GET_ISO_UTC: {
        aux1 = packet.param(0);
        std::string s = Clock::get_current_time_iso("UTC+0") + '\0';
        SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
        SYSTEM_BUS.transaction_send(s);
        break;
    }
    case APETIMECMD_SETTZ:
        set_alternate_tz(be16toh(packet.param(0)));
        break;
    case APETIMECMD_SETTZ_ALT:
        set_fn_tz(be16toh(packet.param(0)));
        break;
    case APETIMECMD_GET_GENERAL: {
        const std::string &tz = Config.get_general_timezone() + '\0';
        SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
        SYSTEM_BUS.transaction_send(tz);
        break;
    }
    case APETIMECMD_GETTZ_LEN: {
        uint8_t len = Config.get_general_timezone().size() + 1;
        SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
        SYSTEM_BUS.transaction_send(&len, sizeof(len));
        break;
    }
    default:
        Debug_printv("Unknown drivewire clock cmd: %02x", packet.command());
        break;
    }
}

#endif /* BUILD_COCO */
