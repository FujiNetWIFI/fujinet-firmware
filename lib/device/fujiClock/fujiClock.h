#ifndef FUJICLOCK_H
#define FUJICLOCK_H

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "bus.h"

class fujiClock : public virtual virtualDevice
{
public:
    static std::string get_current_time_iso(const std::string &posixTimeZone);
    static std::vector<uint8_t> get_current_time_simple(const std::string &posixTimeZone);
    static std::vector<uint8_t> get_current_time_prodos(const std::string &posixTimeZone);
    static std::vector<uint8_t> get_current_time_apetime(const std::string &posixTimeZone);
    static std::vector<uint8_t> get_current_time_simple_hundredths(const std::string &posixTimeZone);
    static std::string get_current_time_sos(const std::string &posixTimeZone);

    // Timezones are printable ASCII; anything else is a malformed host write.
    static bool valid_timezone(const std::string &tz);

    // Stored config can hold garbage from an older firmware; never hand
    // that to the host.
    static std::string system_tz();

    static std::string tz_to_use(bool use_alternate_tz, const std::string &alternate_tz, const std::string &default_tz)
    {
        return use_alternate_tz ? (alternate_tz.empty() ? default_tz : alternate_tz) : default_tz;
    }

    virtual bool processCommand(const FUJI_COMMAND_PACKET &packet);

protected:
    std::string alternate_tz;
    const FUJI_COMMAND_PACKET *_packet = nullptr;

    std::string tz_for(bool use_alt) const;

    static bool command_takes_alt(fujiCommandID_t command);

    // Only called for commands command_takes_alt() accepts; on some buses
    // reading a parameter consumes bus bytes.
    virtual bool alt_requested() = 0;

    virtual std::optional<std::string> read_tz() = 0;
    virtual void send_bytes(const std::vector<uint8_t> &b);
    virtual void send_string(const std::string &s);

    void get_apetime(bool use_alt);
    void get_simple(bool use_alt);
    void get_simple_hundredths(bool use_alt);
    void get_prodos(bool use_alt);
    void get_sos(bool use_alt);
    void get_iso_local(bool use_alt);
    void get_iso_utc();
    void get_general_tz();
    void get_general_tz_len();
    void set_fn_tz();
    void set_alternate_tz();

    bool run_command(fujiCommandID_t command, bool use_alt);

private:
    static std::vector<uint8_t> build_simple(const std::chrono::system_clock::time_point &now,
                                             const std::string &posixTimeZone);
};

#endif /* FUJICLOCK_H */
