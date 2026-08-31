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
    // ============ Time formatting ============
    static std::string fujicore_time_iso(const std::string &posixTimeZone);
    static std::vector<uint8_t> fujicore_time_simple(const std::string &posixTimeZone);
    static std::vector<uint8_t> fujicore_time_prodos(const std::string &posixTimeZone);
    static std::vector<uint8_t> fujicore_time_apetime(const std::string &posixTimeZone);
    static std::vector<uint8_t> fujicore_time_simple_hundredths(const std::string &posixTimeZone);
    static std::string fujicore_time_sos(const std::string &posixTimeZone);

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

    // For buses with a separate read and write namespace; a flat bus uses
    // processCommand().
    bool processReadCommand(const FUJI_COMMAND_PACKET &packet);
    bool processWriteCommand(const FUJI_COMMAND_PACKET &packet);

protected:
    std::string alternate_tz;
    const FUJI_COMMAND_PACKET *_packet = nullptr;

    std::string tz_for(bool use_alt) const;

    static bool command_takes_alt(fujiCommandID_t command);

    // Bus entry points; each refuses a command the table doesn't handle.
    void dispatch(const FUJI_COMMAND_PACKET &packet);
    void dispatch_read(const FUJI_COMMAND_PACKET &packet);
    void dispatch_write(const FUJI_COMMAND_PACKET &packet);

    // SmartPort picks its refusal code per command type.
    virtual void reject_command(const FUJI_COMMAND_PACKET &packet);

    // Accept and complete a command that sends no reply.
    void send_ok();

    // Only called for commands command_takes_alt() accepts; on some buses
    // reading a parameter consumes bus bytes.
    virtual bool fujidev_alt_requested();

    // Maps a bus's spelling of a command onto the shared one.
    virtual fujiCommandID_t fujidev_canonical_command(fujiCommandID_t command, bool &use_alt);

    virtual std::optional<std::string> fujidev_read_tz() = 0;

    std::optional<std::string> read_tz_from_payload();
    std::optional<std::string> read_tz_by_length(size_t len);

    void send_bytes(const std::vector<uint8_t> &b);
    void send_string(const std::string &s);

    virtual bool string_needs_null() const { return true; }

    void fujicmd_get_apetime(bool use_alt);
    void fujicmd_get_simple(bool use_alt);
    void fujicmd_get_simple_hundredths(bool use_alt);
    void fujicmd_get_prodos(bool use_alt);
    void fujicmd_get_sos(bool use_alt);
    void fujicmd_get_iso_local(bool use_alt);
    void fujicmd_get_iso_utc();
    void fujicmd_get_general_tz();
    void fujicmd_get_general_tz_len();
    void fujicmd_set_fn_tz(const std::string &tz);
    void fujicmd_set_alternate_tz(const std::string &tz);

    bool run_read_command(fujiCommandID_t command, bool use_alt);
    bool run_write_command(fujiCommandID_t command, bool use_alt);
    bool run_command(fujiCommandID_t command, bool use_alt);

private:
    static std::vector<uint8_t> build_simple(const std::chrono::system_clock::time_point &now,
                                             const std::string &posixTimeZone);
};

#endif /* FUJICLOCK_H */
