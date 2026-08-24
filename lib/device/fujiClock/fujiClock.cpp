#include "fujiClock.h"

#include <cstdlib>
#include <cstring>
#include <ctime>

#include "fnConfig.h"
#include "fujiCommandID.h"
#include "../../include/debug.h"

#ifdef _WIN32
#define setenv(name, value, overwrite) _putenv_s(name, value)
#define unsetenv(name) _putenv_s(name, "")
#endif /* _WIN32 */

// Helper function to set timezone and restore it after use
class ScopedTimezone {
    std::string oldTz;
    bool hadOldTz;
public:
    explicit ScopedTimezone(const std::string& newTz) {
        // Save the old TZ value
        const char* oldTzEnv = std::getenv("TZ");
        hadOldTz = (oldTzEnv != nullptr);
        if (hadOldTz) {
            oldTz = oldTzEnv;
        }

        // Set the new timezone
        setenv("TZ", newTz.c_str(), 1);
        tzset();
    }

    ~ScopedTimezone() {
        // Restore the old timezone
        if (hadOldTz) {
            setenv("TZ", oldTz.c_str(), 1);
        } else {
            unsetenv("TZ");
        }
        tzset();
    }
};

// Helper function to format time into ISO 8601 string
// Format: YYYY-MM-DDTHH:MM:SS+HHMM for local time
// Zulu time is not used (i.e. appending Z without a timezone offset),
// instead simpler +0000 is same thing, and keeps string same length.
static std::string format_iso8601(const std::chrono::system_clock::time_point& tp, const std::string& posixTimeZone) {
    ScopedTimezone tz(posixTimeZone);

    time_t tt = std::chrono::system_clock::to_time_t(tp);
    std::tm tm = *std::localtime(&tt);

    char buffer[30];
    std::strftime(buffer, sizeof(buffer), "%FT%T%z", &tm);
    return std::string(buffer);
}

// Helper function to get local time in specific timezone
static std::tm get_local_time(const std::string& posixTimeZone) {
    ScopedTimezone tz(posixTimeZone);
    auto now = std::chrono::system_clock::now();
    time_t tt = std::chrono::system_clock::to_time_t(now);
    return *std::localtime(&tt);
}

std::string fujiClock::get_current_time_iso(const std::string& posixTimeZone) {
    return format_iso8601(std::chrono::system_clock::now(), posixTimeZone);
}

std::vector<uint8_t> fujiClock::build_simple(const std::chrono::system_clock::time_point& now,
                                             const std::string& posixTimeZone) {
    ScopedTimezone tz(posixTimeZone);
    time_t tt = std::chrono::system_clock::to_time_t(now);
    auto localTime = *std::localtime(&tt);

    std::vector<uint8_t> result(7);
    result[0] = static_cast<uint8_t>(localTime.tm_year / 100 + 19);
    result[1] = static_cast<uint8_t>(localTime.tm_year % 100);
    result[2] = static_cast<uint8_t>(localTime.tm_mon + 1);
    result[3] = static_cast<uint8_t>(localTime.tm_mday);
    result[4] = static_cast<uint8_t>(localTime.tm_hour);
    result[5] = static_cast<uint8_t>(localTime.tm_min);
    result[6] = static_cast<uint8_t>(localTime.tm_sec);
    return result;
}

std::vector<uint8_t> fujiClock::get_current_time_simple(const std::string& posixTimeZone) {
    return build_simple(std::chrono::system_clock::now(), posixTimeZone);
}

std::vector<uint8_t> fujiClock::get_current_time_prodos(const std::string& posixTimeZone) {
    auto localTime = get_local_time(posixTimeZone);

    // ProDOS time uses 4 bytes, see https://prodos8.com/docs/techref/adding-routines-to-prodos/
    /*
                7 6 5 4 3 2 1 0   7 6 5 4 3 2 1 0
                +-+-+-+-+-+-+-+-+ +-+-+-+-+-+-+-+-+
         DATE:  |    year     |  month  |   day   |
                +-+-+-+-+-+-+-+-+ +-+-+-+-+-+-+-+-+
                7 6 5 4 3 2 1 0   7 6 5 4 3 2 1 0
                +-+-+-+-+-+-+-+-+ +-+-+-+-+-+-+-+-+
         TIME:  |    hour       | |    minute     |
                +-+-+-+-+-+-+-+-+ +-+-+-+-+-+-+-+-+
    */
    std::vector<uint8_t> prodosTime(4);
    prodosTime[0] = static_cast<uint8_t>(localTime.tm_mday + ((localTime.tm_mon + 1) << 5));
    prodosTime[1] = static_cast<uint8_t>(((localTime.tm_year % 100) << 1) + ((localTime.tm_mon + 1) >> 3));
    prodosTime[2] = static_cast<uint8_t>(localTime.tm_min);
    prodosTime[3] = static_cast<uint8_t>(localTime.tm_hour);

    return prodosTime;
}

std::vector<uint8_t> fujiClock::get_current_time_apetime(const std::string& posixTimeZone) {
    auto localTime = get_local_time(posixTimeZone);

    // Format the time into ApeTime Atari format
    std::vector<uint8_t> apeTime(6);
    apeTime[0] = static_cast<uint8_t>(localTime.tm_mday);
    apeTime[1] = static_cast<uint8_t>(localTime.tm_mon + 1);    // change to 1 based month from 0
    apeTime[2] = static_cast<uint8_t>(localTime.tm_year - 100); // add 1900 to the year to get YYYY, so 124 + 1900 = 2024, but 124 - 100 = 24 for just YY
    apeTime[3] = static_cast<uint8_t>(localTime.tm_hour);
    apeTime[4] = static_cast<uint8_t>(localTime.tm_min);
    apeTime[5] = static_cast<uint8_t>(localTime.tm_sec);

    return apeTime;
}

std::vector<uint8_t> fujiClock::get_current_time_simple_hundredths(const std::string& posixTimeZone) {
    auto now = std::chrono::system_clock::now();
    uint8_t hundredths = static_cast<uint8_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count() % 1000 / 10);
    auto result = build_simple(now, posixTimeZone);
    result.push_back(hundredths);
    return result;
}

std::string fujiClock::get_current_time_sos(const std::string& posixTimeZone) {
    auto localTime = get_local_time(posixTimeZone);

    // Format: YYYYMMDD0HHMMSS000 - raw time, no timezone info supported
    // ref SOS Reference Manual: https://archive.org/details/apple-iii-sos-reference-manual-volume-2/page/94/mode/2up
    char buffer[30];
    std::strftime(buffer, sizeof(buffer), "%Y%m%d0%H%M%S000", &localTime);
    return std::string(buffer);
}

bool fujiClock::valid_timezone(const std::string &tz)
{
    if (tz.empty())
        return false;

    for (char c : tz)
        if (c < 0x20 || c > 0x7E)
            return false;

    return true;
}

std::string fujiClock::system_tz()
{
    std::string tz = Config.get_general_timezone();
    return valid_timezone(tz) ? tz : "UTC";
}

std::string fujiClock::tz_for(bool use_alt) const
{
    return tz_to_use(use_alt, alternate_tz, system_tz());
}

void fujiClock::send_bytes(const std::vector<uint8_t> &b)
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    SYSTEM_BUS.transaction_send(b);
}

void fujiClock::send_string(const std::string &s)
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    SYSTEM_BUS.transaction_send(s + '\0');
}

void fujiClock::get_apetime(bool use_alt)
{
    send_bytes(get_current_time_apetime(tz_for(use_alt)));
}

void fujiClock::get_simple(bool use_alt)
{
    send_bytes(get_current_time_simple(tz_for(use_alt)));
}

void fujiClock::get_simple_hundredths(bool use_alt)
{
    send_bytes(get_current_time_simple_hundredths(tz_for(use_alt)));
}

void fujiClock::get_prodos(bool use_alt)
{
    send_bytes(get_current_time_prodos(tz_for(use_alt)));
}

void fujiClock::get_sos(bool use_alt)
{
    send_string(get_current_time_sos(tz_for(use_alt)));
}

void fujiClock::get_iso_local(bool use_alt)
{
    send_string(get_current_time_iso(tz_for(use_alt)));
}

void fujiClock::get_iso_utc()
{
    send_string(get_current_time_iso("UTC+0"));
}

void fujiClock::get_general_tz()
{
    send_string(system_tz());
}

void fujiClock::get_general_tz_len()
{
    send_bytes({ (uint8_t)(system_tz().size() + 1) });
}

void fujiClock::set_fn_tz()
{
    auto tz = read_tz();
    if (!tz)
        return;

    if (!valid_timezone(*tz))
    {
        Debug_printv("Rejecting malformed timezone");
        return;
    }

    Config.store_general_timezone(tz->c_str());
    Config.save();
    Debug_printf("fujiClock: system tz set to <%s>\n", tz->c_str());
}

void fujiClock::set_alternate_tz()
{
    auto tz = read_tz();
    if (!tz)
        return;

    if (!valid_timezone(*tz))
    {
        Debug_printv("Rejecting malformed alternate timezone");
        return;
    }

    alternate_tz = tz.value();
    Debug_printf("fujiClock: alternate tz set to <%s>\n", alternate_tz.c_str());
}

bool fujiClock::run_command(uint8_t command, bool use_alt)
{
    switch (command)
    {
    case APETIMECMD_GETTZTIME:
        get_apetime(true);
        break;
    case APETIMECMD_GETTIME:
        get_apetime(use_alt);
        break;
    case APETIMECMD_SETTZ_ALT2:
        get_simple(use_alt);
        break;
    case APETIMECMD_GET_SIMPLE_HUNDREDTHS:
        get_simple_hundredths(use_alt);
        break;
    case APETIMECMD_GET_PRODOS:
        get_prodos(use_alt);
        break;
    case APETIMECMD_GET_SOS:
        get_sos(use_alt);
        break;
    case APETIMECMD_GET_ISO_LOCAL:
        get_iso_local(use_alt);
        break;
    case APETIMECMD_GET_ISO_UTC:
        get_iso_utc();
        break;
    case APETIMECMD_GET_GENERAL:
        get_general_tz();
        break;
    case APETIMECMD_GETTZ_LEN:
        get_general_tz_len();
        break;
    case APETIMECMD_SETTZ:
        set_alternate_tz();
        break;
    case APETIMECMD_SETTZ_ALT:
        set_fn_tz();
        break;
    default:
        return false;
    }

    return true;
}

bool fujiClock::command_takes_alt(uint8_t command)
{
    switch (command)
    {
    case APETIMECMD_GETTZTIME:
    case APETIMECMD_GETTIME:
    case APETIMECMD_SETTZ_ALT2:
    case APETIMECMD_GET_SIMPLE_HUNDREDTHS:
    case APETIMECMD_GET_PRODOS:
    case APETIMECMD_GET_SOS:
    case APETIMECMD_GET_ISO_LOCAL:
    case APETIMECMD_GET_ISO_UTC:
        return true;
    default:
        return false;
    }
}

bool fujiClock::processCommand(const FUJI_COMMAND_PACKET &packet)
{
    _packet = &packet;

    bool use_alt = command_takes_alt(packet.command()) && alt_requested();
    bool handled = run_command(packet.command(), use_alt);

    _packet = nullptr;

    if (!handled)
        Debug_printv("Unknown clock cmd: %02x", packet.command());

    return handled;
}
