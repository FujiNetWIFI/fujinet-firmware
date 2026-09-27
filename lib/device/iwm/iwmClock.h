#ifndef IWMCLOCK_H
#define IWMCLOCK_H

#include <optional>
#include <string>

#include "../fujiClock/fujiClock.h"

class iwmClock : public fujiClock
{
protected:
    std::optional<std::string> fujidev_read_tz() override { return read_tz_from_payload(); }

    // Alternate timezone is selected by command byte case, never by parameter.
    bool fujidev_alt_requested() override { return false; }

    fujiCommandID_t fujidev_canonical_command(fujiCommandID_t command, bool &use_alt) override;

    // SmartPort replies carry their own length, so no trailing null.
    bool string_needs_null() const override { return false; }

    void reject_command(const iwm_decoded_cmd_t &cmd) override { iwm_return_badcmd(cmd); }

public:
    iwmClock() = default;

    void iwm_ctrl(const iwm_decoded_cmd_t &cmd) override { dispatch_write(cmd); }
    void iwm_status(const iwm_decoded_cmd_t &cmd) override { dispatch_read(cmd); }
    void iwm_open(const iwm_decoded_cmd_t &cmd) override { send_ok(); }
    void iwm_close(const iwm_decoded_cmd_t &cmd) override { send_ok(); }

    iwm_device_info_block_t create_dib_reply_packet() override;
    iwm_device_status_block_t create_status_reply_packet() override;
};

extern iwmClock platformClock;

#endif /* IWMCLOCK_H */
