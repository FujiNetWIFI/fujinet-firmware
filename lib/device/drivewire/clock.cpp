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

std::optional<std::string> drivewireClock::read_tz_from_host()
{
    uint8_t lenl = SYSTEM_BUS.read();
    uint8_t lenh = SYSTEM_BUS.read();
    uint16_t bufsz = ((uint16_t)lenh << 8) | lenl;

    if (bufsz == 0) {
        Debug_printv("ERROR: No timezone sent");
        return std::nullopt;
    }

    std::string timezone;
    timezone.reserve(bufsz);
    for (uint16_t i = 0; i < bufsz; i++)
        timezone.push_back((char)SYSTEM_BUS.read());
    while (!timezone.empty() && timezone.back() == '\0')
        timezone.pop_back();
    return timezone;
}

void drivewireClock::set_fn_tz()
{
    auto result = read_tz_from_host();
    if (result)
        Config.store_general_timezone(result->c_str());
}

void drivewireClock::set_alternate_tz()
{
    auto result = read_tz_from_host();
    if (result)
        alternate_tz = result.value();
}

void drivewireClock::process()
{
    uint8_t cmd = SYSTEM_BUS.read();
    uint8_t aux1;
    bool use_alt;

    switch (cmd)
    {
    case APETIMECMD_GETTZTIME:
    case APETIMECMD_GETTIME: {
        aux1 = SYSTEM_BUS.read();
        use_alt = (cmd == APETIMECMD_GETTZTIME) || (aux1 == 0x01);
        auto t = Clock::get_current_time_apetime(
            Clock::tz_to_use(use_alt, alternate_tz, Config.get_general_timezone()));
        SYSTEM_BUS.write(t.data(), t.size());
        break;
    }
    case APETIMECMD_SETTZ_ALT2: {
        aux1 = SYSTEM_BUS.read();
        use_alt = (aux1 == 0x01);
        auto t = Clock::get_current_time_simple(
            Clock::tz_to_use(use_alt, alternate_tz, Config.get_general_timezone()));
        SYSTEM_BUS.write(t.data(), t.size());
        break;
    }
    case APETIMECMD_GET_PRODOS: {
        aux1 = SYSTEM_BUS.read();
        use_alt = (aux1 == 0x01);
        auto t = Clock::get_current_time_prodos(
            Clock::tz_to_use(use_alt, alternate_tz, Config.get_general_timezone()));
        SYSTEM_BUS.write(t.data(), t.size());
        break;
    }
    case APETIMECMD_GET_SOS: {
        aux1 = SYSTEM_BUS.read();
        use_alt = (aux1 == 0x01);
        std::string s = Clock::get_current_time_sos(
            Clock::tz_to_use(use_alt, alternate_tz, Config.get_general_timezone()));
        SYSTEM_BUS.write((const uint8_t *)s.c_str(), s.size() + 1);
        break;
    }
    case APETIMECMD_GET_ISO_LOCAL: {
        aux1 = SYSTEM_BUS.read();
        use_alt = (aux1 == 0x01);
        std::string s = Clock::get_current_time_iso(
            Clock::tz_to_use(use_alt, alternate_tz, Config.get_general_timezone()));
        SYSTEM_BUS.write((const uint8_t *)s.c_str(), s.size() + 1);
        break;
    }
    case APETIMECMD_GET_ISO_UTC: {
        SYSTEM_BUS.read();
        std::string s = Clock::get_current_time_iso("UTC+0");
        SYSTEM_BUS.write((const uint8_t *)s.c_str(), s.size() + 1);
        break;
    }
    case APETIMECMD_SETTZ:
        set_alternate_tz();
        break;
    case APETIMECMD_SETTZ_ALT:
        set_fn_tz();
        break;
    case APETIMECMD_GET_GENERAL: {
        const std::string &tz = Config.get_general_timezone();
        SYSTEM_BUS.write((const uint8_t *)tz.c_str(), tz.size() + 1);
        break;
    }
    case APETIMECMD_GETTZ_LEN: {
        uint8_t len = Config.get_general_timezone().size() + 1;
        SYSTEM_BUS.write(len);
        break;
    }
    default:
        Debug_printv("Unknown drivewire clock cmd: %02x", cmd);
        break;
    }
}

#endif /* BUILD_COCO */
