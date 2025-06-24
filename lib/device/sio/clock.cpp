#ifdef BUILD_ATARI


#include <cstring>
#include <ctime>
#include <optional>

#include "clock.h"

#include "compat_string.h"
#include "../../include/debug.h"
#include "fnConfig.h"
#include "utils.h"

#define SIO_APETIMECMD_GETTIME 0x93
#define SIO_APETIMECMD_SETTZ 0x99
#define SIO_APETIMECMD_GETTZTIME 0x9A

std::optional<std::string> sioClock::read_tz_from_host()
{
    Debug_println("sioClock read_tz_from_host");
    int bufsz = sio_get_aux();

    if (bufsz <= 0) {
        Debug_printv("ERROR: No timezone sent");
        sio_complete();
        return std::nullopt; // Return an empty optional to indicate error
    }

    std::string timezone(bufsz, '\0'); // Create a string of size bufsz, filled with '\0'
    uint8_t ck = bus_to_peripheral(reinterpret_cast<uint8_t*>(&timezone[0]), bufsz);

    if (sio_checksum(reinterpret_cast<uint8_t*>(&timezone[0]), bufsz) != ck) {
        sio_error();
        return std::nullopt;
    } else {
        sio_complete();
        return timezone;
    }
}

void sioClock::set_fn_tz()
{
    auto result = read_tz_from_host();
    if (result) {
        Config.store_general_timezone(result->c_str());
    }
}

void sioClock::set_alternate_tz()
{
    auto result = read_tz_from_host();
    if (result) {
        alternate_tz = result.value();
    }
}

void sioClock::sio_process(uint32_t commanddata, uint8_t checksum)
{
    cmdFrame.commanddata = commanddata;
    cmdFrame.cksum = checksum;

    // cmdFrame.aux1 = 0x01 means use alternate TZ if it exists, any other value would use system TZ
    bool use_alternate_tz = cmdFrame.aux1 == 0x01;

    switch (cmdFrame.comnd)
    {
    ////////////////////////////////////////////////////////////////////////////////////
    // TIME FUNCTIONS
    ////////////////////////////////////////////////////////////////////////////////////
    case SIO_APETIMECMD_GETTZTIME:      // legacy was only required for APOD, which is being updated, but anyone out there still running it on new firmware, will still work
    case SIO_APETIMECMD_GETTIME: {
        // all commands for time now use the aux1 param to decide if the caller wanted the system tz (aux1 != 1), or the alternate tz (aux1 = 1)
        // this makes SIO_APETIMECMD_GETTIME and SIO_APETIMECMD_GETTZTIME behave the same so we can deprecate the latter, using aux values rather than separate commands
        // Note: APETIME.COM uses 0x93 with aux1=A0, aux2=EE, so we will always get the FN timezone in response, but fujinet-lib can use the alternate timezone functionality
        sio_ack();
        // for backwards compatibility, if we're sent SIO_APETIMECMD_GETTZTIME we always use the ALT timezone if set
        if (cmdFrame.comnd == SIO_APETIMECMD_GETTZTIME) use_alternate_tz = true;

        auto apeTime = Clock::get_current_time_apetime(Clock::tz_to_use(use_alternate_tz, alternate_tz, Config.get_general_timezone()));
        bus_to_computer(apeTime.data(), apeTime.size(), false);
        break;
    }
    case 'T': {
        // Date and time, easy to be used by general programs
        sio_ack();
        auto simpleTime = Clock::get_current_time_simple(Clock::tz_to_use(use_alternate_tz, alternate_tz, Config.get_general_timezone()));
        bus_to_computer(simpleTime.data(), simpleTime.size(), false);
        break;
    }
    case 'P': {
        // Date and time, to be used by a ProDOS driver
        sio_ack();
        auto prodosTime = Clock::get_current_time_prodos(Clock::tz_to_use(use_alternate_tz, alternate_tz, Config.get_general_timezone()));
        bus_to_computer(prodosTime.data(), prodosTime.size(), false);
        break;
    }
    case 'S': {
        // Date and time, ASCII string in Apple /// SOS format: YYYYMMDD0HHMMSS000
        std::string sosTime = Clock::get_current_time_sos(Clock::tz_to_use(use_alternate_tz, alternate_tz, Config.get_general_timezone()));
        bus_to_computer((uint8_t *) sosTime.c_str(), sosTime.size() + 1, false);
        break;
    }
    case 'I': {
        // Date and time, ASCII string in ISO format - making this consistent with APPLE code
        sio_ack();
        std::string utcTime = Clock::get_current_time_iso(Clock::tz_to_use(use_alternate_tz, alternate_tz, Config.get_general_timezone()));
        bus_to_computer((uint8_t *) utcTime.c_str(), utcTime.size() + 1, false);
        break;
    }
    case 'Z': {
        // utc (zulu)
        sio_ack();
        std::string isoTime = Clock::get_current_time_iso("UTC+0");
        bus_to_computer((uint8_t *) isoTime.c_str(), isoTime.size() + 1, false);
        break;
    }

    ////////////////////////////////////////////////////////////////////////////////////
    // TIMEZONE FUNCTIONS
    ////////////////////////////////////////////////////////////////////////////////////

    // for backwards compatibility with APOD, we use the 0x99 for this command
    case SIO_APETIMECMD_SETTZ: {
        sio_late_ack();
        set_alternate_tz();
        break;
    }
    // can't use "T" as that's taken by getter
    case 't': {
        sio_late_ack();
        set_fn_tz();
        break;
    }
    case 'G': {
        // Get current system timezone
        sio_ack();
        bus_to_computer((uint8_t *) Config.get_general_timezone().c_str(), Config.get_general_timezone().size() + 1, false); // +1 for null terminator
        break;
    }
    case 'L': {
        // Get length of system TZ
        sio_ack();
        uint8_t len = Config.get_general_timezone().size() + 1;
        bus_to_computer(&len, 1, false);
        break;
    }

    default:
        sio_nak();
        break;
    };
}
#endif /* BUILD_ATARI */
