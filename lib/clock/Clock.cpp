#include "Clock.h"
#include <chrono>
#include <ctime>
#include <cstdlib>
#include <cstring>

#include "../../include/debug.h"
#include "../config/fnConfig.h"
#include "utils.h"

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

std::string Clock::get_current_time_iso(const std::string& posixTimeZone) {
    return format_iso8601(std::chrono::system_clock::now(), posixTimeZone);
}

std::vector<uint8_t> Clock::get_current_time_simple(const std::string& posixTimeZone) {
    auto localTime = get_local_time(posixTimeZone);

    // A simple binary format in 7 bytes for clients to consume directly into bytes
    std::vector<uint8_t> simpleTime(7);
    simpleTime[0] = static_cast<uint8_t>((localTime.tm_year)/100 + 19);
    simpleTime[1] = static_cast<uint8_t>(localTime.tm_year % 100);
    simpleTime[2] = static_cast<uint8_t>(localTime.tm_mon + 1);
    simpleTime[3] = static_cast<uint8_t>(localTime.tm_mday);
    simpleTime[4] = static_cast<uint8_t>(localTime.tm_hour);
    simpleTime[5] = static_cast<uint8_t>(localTime.tm_min);
    simpleTime[6] = static_cast<uint8_t>(localTime.tm_sec);

    return simpleTime;
}

std::vector<uint8_t> Clock::get_current_time_prodos(const std::string& posixTimeZone) {
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

std::vector<uint8_t> Clock::get_current_time_apetime(const std::string& posixTimeZone) {
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

std::string Clock::get_current_time_sos(const std::string& posixTimeZone) {
    auto localTime = get_local_time(posixTimeZone);
    
    // Format: YYYYMMDD0HHMMSS000 - raw time, no timezone info supported
    // ref SOS Reference Manual: https://archive.org/details/apple-iii-sos-reference-manual-volume-2/page/94/mode/2up
    char buffer[30];
    std::strftime(buffer, sizeof(buffer), "%Y%m%d0%H%M%S000", &localTime);
    return std::string(buffer);
}