#ifdef BUILD_ATARI


#include <cstring>
#include <ctime>
#include <optional>

#include "clock.h"

#include "compat_string.h"
#include "../../include/debug.h"
#include "fnConfig.h"
#include "utils.h"

std::optional<std::string> sioClock::read_tz_from_host(const FujiSIOPacket &packet)
{
    Debug_println("sioClock read_tz_from_host");
    int bufsz = packet.param16(0);

    if (bufsz <= 0) {
        Debug_printv("ERROR: No timezone sent");
        SYSTEM_BUS.transaction_success();
        return std::nullopt; // Return an empty optional to indicate error
    }

    std::string timezone(bufsz, '\0'); // Create a string of size bufsz, filled with '\0'
    if (!SYSTEM_BUS.transaction_get(reinterpret_cast<uint8_t*>(&timezone[0]), bufsz)) {
        SYSTEM_BUS.transaction_error();
        return std::nullopt;
    } else {
        SYSTEM_BUS.transaction_success();
        return timezone;
    }
}

void sioClock::set_fn_tz(const FujiSIOPacket &packet)
{
    auto result = read_tz_from_host(packet);
    if (result) {
        Config.store_general_timezone(result->c_str());
    }
}

void sioClock::set_alternate_tz(const FujiSIOPacket &packet)
{
    auto result = read_tz_from_host(packet);
    if (result) {
        alternate_tz = result.value();
    }
}

void sioClock::sio_process(const FujiSIOPacket &packet)
{
    // packet.param(0) = 0x01 means use alternate TZ if it exists, any other value would use system TZ
    bool use_alternate_tz;
    switch (packet.command())
    {
    case APETIMECMD_GETTZTIME:
    case APETIMECMD_GETTIME:
    case APETIMECMD_SETTZ_ALT2:
    case APETIMECMD_GET_SIMPLE_HUNDREDTHS:
    case APETIMECMD_GET_PRODOS:
    case APETIMECMD_GET_SOS:
    case APETIMECMD_GET_ISO_LOCAL:
        use_alternate_tz = packet.param8(0) == 0x01;
        break;
    default:
        use_alternate_tz = false;
    }

    switch (packet.command())
    {
    ////////////////////////////////////////////////////////////////////////////////////
    // TIME FUNCTIONS
    ////////////////////////////////////////////////////////////////////////////////////
    case APETIMECMD_GETTZTIME:      // legacy was only required for APOD, which is being updated, but anyone out there still running it on new firmware, will still work
    case APETIMECMD_GETTIME: {
        // all commands for time now use the aux1 param to decide if the caller wanted the system tz (aux1 != 1), or the alternate tz (aux1 = 1)
        // this makes APETIMECMD_GETTIME and APETIMECMD_GETTZTIME behave the same so we can deprecate the latter, using aux values rather than separate commands
        // Note: APETIME.COM uses 0x93 with aux1=A0, aux2=EE, so we will always get the FN timezone in response, but fujinet-lib can use the alternate timezone functionality
        SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
        // for backwards compatibility, if we're sent APETIMECMD_GETTZTIME we always use the ALT timezone if set
        if (packet.command() == APETIMECMD_GETTZTIME) use_alternate_tz = true;

        auto apeTime = Clock::get_current_time_apetime(Clock::tz_to_use(use_alternate_tz, alternate_tz, Config.get_general_timezone()));
        SYSTEM_BUS.transaction_send(apeTime.data(), apeTime.size(), false);
        break;
    }
    case APETIMECMD_SETTZ_ALT2: {
        // Date and time, easy to be used by general programs
        SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
        auto simpleTime = Clock::get_current_time_simple(Clock::tz_to_use(use_alternate_tz, alternate_tz, Config.get_general_timezone()));
        SYSTEM_BUS.transaction_send(simpleTime.data(), simpleTime.size(), false);
        break;
    }
    case APETIMECMD_GET_SIMPLE_HUNDREDTHS: {
        SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
        auto hundredthsTime = Clock::get_current_time_simple_hundredths(Clock::tz_to_use(use_alternate_tz, alternate_tz, Config.get_general_timezone()));
        SYSTEM_BUS.transaction_send(hundredthsTime.data(), hundredthsTime.size(), false);
        break;
    }
    case APETIMECMD_GET_PRODOS: {
        // Date and time, to be used by a ProDOS driver
        SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
        auto prodosTime = Clock::get_current_time_prodos(Clock::tz_to_use(use_alternate_tz, alternate_tz, Config.get_general_timezone()));
        SYSTEM_BUS.transaction_send(prodosTime.data(), prodosTime.size(), false);
        break;
    }
    case APETIMECMD_GET_SOS: {
        // Date and time, ASCII string in Apple /// SOS format: YYYYMMDD0HHMMSS000
        SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
        std::string sosTime = Clock::get_current_time_sos(Clock::tz_to_use(use_alternate_tz, alternate_tz, Config.get_general_timezone()));
        SYSTEM_BUS.transaction_send((uint8_t *) sosTime.c_str(), sosTime.size() + 1, false);
        break;
    }
    case APETIMECMD_GET_ISO_LOCAL: {
        // Date and time, ASCII string in ISO format - making this consistent with APPLE code
        SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
        std::string utcTime = Clock::get_current_time_iso(Clock::tz_to_use(use_alternate_tz, alternate_tz, Config.get_general_timezone()));
        SYSTEM_BUS.transaction_send((uint8_t *) utcTime.c_str(), utcTime.size() + 1, false);
        break;
    }
    case APETIMECMD_GET_ISO_UTC: {
        // utc (zulu)
        SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
        std::string isoTime = Clock::get_current_time_iso("UTC+0");
        SYSTEM_BUS.transaction_send((uint8_t *) isoTime.c_str(), isoTime.size() + 1, false);
        break;
    }

    ////////////////////////////////////////////////////////////////////////////////////
    // TIMEZONE FUNCTIONS
    ////////////////////////////////////////////////////////////////////////////////////

    // for backwards compatibility with APOD, we use the 0x99 for this command
    case APETIMECMD_SETTZ: {
        SYSTEM_BUS.transaction_accept(TRANS_STATE::WILL_GET);
        set_alternate_tz(packet);
        break;
    }
    // can't use "T" as that's taken by getter
    case APETIMECMD_SETTZ_ALT: {
        SYSTEM_BUS.transaction_accept(TRANS_STATE::WILL_GET);
        set_fn_tz(packet);
        break;
    }
    case APETIMECMD_GET_GENERAL: {
        // Get current system timezone
        SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
        SYSTEM_BUS.transaction_send((uint8_t *) Config.get_general_timezone().c_str(), Config.get_general_timezone().size() + 1, false); // +1 for null terminator
        break;
    }
    case APETIMECMD_GETTZ_LEN: {
        // Get length of system TZ
        SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
        uint8_t len = Config.get_general_timezone().size() + 1;
        SYSTEM_BUS.transaction_send(&len, 1, false);
        break;
    }

    default:
        SYSTEM_BUS.transaction_error();
        break;
    };
}
#endif /* BUILD_ATARI */
