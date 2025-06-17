#ifndef CLASS_CLOCK_H
#define CLASS_CLOCK_H

#include <cstdint>
#include <string>
#include <vector>

class Clock {
public:

    // current local time in ISO 8601 format
    static std::string get_current_time_iso(const std::string& posixTimeZone);

    // current local time in a Simple binary time format
    static std::vector<uint8_t> get_current_time_simple(const std::string& posixTimeZone);

    // current local time in ProDOS time format
    static std::vector<uint8_t> get_current_time_prodos(const std::string& posixTimeZone);

    // current local time in ApeTime format
    static std::vector<uint8_t> get_current_time_apetime(const std::string& posixTimeZone);

    // current local time in SOS set_time format
    static std::string get_current_time_sos(const std::string& posixTimeZone);

    static std::string tz_to_use(bool use_alternate_tz, const std::string& alternate_tz, const std::string& default_tz)
    {
        return use_alternate_tz ? (alternate_tz.empty() ? default_tz : alternate_tz) : default_tz;
    }

};

#endif // CLASS_CLOCK_H