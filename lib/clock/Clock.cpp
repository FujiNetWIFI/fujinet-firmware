#include "Clock.h"
#include <ctime>
#include <cstdlib>
#include <cstring>

#include "../../include/debug.h"
#include "utils.h"

// Cross-platform function to set environment variable
static void set_timezone_env(const std::string& name, const std::string& value) {
#ifdef _WIN32
    _putenv_s(name.c_str(), value.c_str());
#else
    setenv(name.c_str(), value.c_str(), 1);
#endif
    tzset();
}

// Helper function to format time into ISO 8601 string
static std::string format_iso8601(const std::tm& time) {
    char buffer[30];
    // Format: YYYY-MM-DDTHH:MM:SS+HHMM for local time. Zulu time is not used (i.e. appending Z without a timezone offset), instead simpler +0000 is same thing, and keeps string same length.
    std::strftime(buffer, sizeof(buffer), "%FT%T%z", &time);
    return std::string(buffer);
}

std::string Clock::get_current_time_iso(const std::string& posixTimeZone) {
    set_timezone_env("TZ", posixTimeZone);
    std::time_t now = std::time(nullptr);
    std::tm* localTime = std::localtime(&now);

    std::string isoString = format_iso8601(*localTime);
    // Debug_printf("isoStringTZ time: %s, %s\r\n", isoString.c_str(), posixTimeZone.c_str());

    return isoString;
}

std::vector<uint8_t> Clock::get_current_time_simple(const std::string& posixTimeZone) {
    set_timezone_env("TZ", posixTimeZone);
    std::time_t now = std::time(nullptr);
    std::tm* localTime = std::localtime(&now);

    // A simple binary format in 7 bytes for clients to consume directly into bytes
    std::vector<uint8_t> simpleTime(7);
    simpleTime[0] = static_cast<uint8_t>((localTime->tm_year)/100 + 19);
    simpleTime[1] = static_cast<uint8_t>(localTime->tm_year % 100);
    simpleTime[2] = static_cast<uint8_t>(localTime->tm_mon + 1);
    simpleTime[3] = static_cast<uint8_t>(localTime->tm_mday);
    simpleTime[4] = static_cast<uint8_t>(localTime->tm_hour);
    simpleTime[5] = static_cast<uint8_t>(localTime->tm_min);
    simpleTime[6] = static_cast<uint8_t>(localTime->tm_sec);

    // std::string dstring = util_hexdump(simpleTime.data(), 7);
    // Debug_printf("simple time: %s, %s\r\n", dstring.c_str(), posixTimeZone.c_str());

    return simpleTime;
}

std::vector<uint8_t> Clock::get_current_time_prodos(const std::string& posixTimeZone) {
    set_timezone_env("TZ", posixTimeZone);
    std::time_t now = std::time(nullptr);
    std::tm* localTime = std::localtime(&now);

    // Format the time into ProDOS format
    std::vector<uint8_t> prodosTime(4); // ProDOS time uses 4 bytes, see https://prodos8.com/docs/techref/adding-routines-to-prodos/
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
    prodosTime[0] = static_cast<uint8_t>(localTime->tm_mday + ((localTime->tm_mon + 1) << 5));
    prodosTime[1] = static_cast<uint8_t>(((localTime->tm_year % 100) << 1) + ((localTime->tm_mon + 1) >> 3));
    prodosTime[2] = static_cast<uint8_t>(localTime->tm_min);
    prodosTime[3] = static_cast<uint8_t>(localTime->tm_hour);

    // std::string dstring = util_hexdump(prodosTime.data(), 4);
    // Debug_printf("prodos time: %s, %s\r\n", dstring.c_str(), posixTimeZone.c_str());

    return prodosTime;
}

std::vector<uint8_t> Clock::get_current_time_apetime(const std::string& posixTimeZone) {
    set_timezone_env("TZ", posixTimeZone);
    std::time_t now = std::time(nullptr);
    std::tm* localTime = std::localtime(&now);

    // Format the time into ApeTime Atari format
    std::vector<uint8_t> apeTime(6);
    apeTime[0] = static_cast<uint8_t>(localTime->tm_mday);
    apeTime[1] = static_cast<uint8_t>(localTime->tm_mon + 1);    // change to 1 based month from 0
    apeTime[2] = static_cast<uint8_t>(localTime->tm_year - 100); // add 1900 to the year to get YYYY, so 124 + 1900 = 2024, but 124 - 100 = 24 for just YY
    apeTime[3] = static_cast<uint8_t>(localTime->tm_hour);
    apeTime[4] = static_cast<uint8_t>(localTime->tm_min);
    apeTime[5] = static_cast<uint8_t>(localTime->tm_sec);

    // std::string dstring = util_hexdump(apeTime.data(), 6);
    // Debug_printf("apetime: %s, %s\r\n", dstring.c_str(), posixTimeZone.c_str());

    return apeTime;
}