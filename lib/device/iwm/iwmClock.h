#ifndef IWMCLOCK_H
#define IWMCLOCK_H

#include <optional>
#include <string>

#include "../fujiClock/fujiClock.h"

class iwmClock : public fujiClock
{
protected:
    std::optional<std::string> read_tz() override;
    bool alt_requested() override;

    // SmartPort replies carry their own length, so no trailing null.
    void send_string(const std::string &s) override;

public:
    iwmClock();

    void iwm_ctrl(const iwm_decoded_cmd_t &cmd) override;
    void iwm_status(const iwm_decoded_cmd_t &cmd) override;
    void iwm_open(const iwm_decoded_cmd_t &cmd) override;
    void iwm_close(const iwm_decoded_cmd_t &cmd) override;

    void shutdown() override;
    iwm_device_info_block_t create_dib_reply_packet() override;
    iwm_device_status_block_t create_status_reply_packet() override;
};

extern iwmClock platformClock;

#endif /* IWMCLOCK_H */
