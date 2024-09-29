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
    Debug_println("Clock set TZ request");
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

void sioClock::sio_process(uint32_t commanddata, uint8_t checksum)
{
    cmdFrame.commanddata = commanddata;
    cmdFrame.checksum = checksum;

    switch (cmdFrame.comnd)
    {
    case SIO_APETIMECMD_GETTIME: {
        // this is a "no timezone" version, which is effectively UTC
        sio_ack();
        auto apeTime = Clock::get_current_time_apetime("UTC+0");
        bus_to_computer(apeTime.data(), apeTime.size(), false);
        break;
    }
    case SIO_APETIMECMD_SETTZ: {
        sio_late_ack();
        set_fn_tz();
        break;
    }
    case SIO_APETIMECMD_GETTZTIME: {
        sio_ack();
        auto apeTime = Clock::get_current_time_apetime(Config.get_general_timezone());
        bus_to_computer(apeTime.data(), apeTime.size(), false);
        break;
    }
    case 'T': {
        // Date and time, easy to be used by general programs
        sio_ack();
        auto simpleTime = Clock::get_current_time_simple(Config.get_general_timezone());
        bus_to_computer(simpleTime.data(), simpleTime.size(), false);
        break;
    }
    case 'P': {
        // Date and time, to be used by a ProDOS driver
        sio_ack();
        auto prodosTime = Clock::get_current_time_prodos(Config.get_general_timezone());
        bus_to_computer(prodosTime.data(), prodosTime.size(), false);
        break;
    }
    case 'S': {
        // Date and time, ASCII string in ISO format - This is a change from the original format: YYYYMMDDxHHMMSSxxx with 0 for every 'x' value, which was not TZ friendly, and I found no references to
        sio_ack();
        std::string utcTime = Clock::get_current_time_iso(Config.get_general_timezone());
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