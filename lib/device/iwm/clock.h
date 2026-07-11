#ifndef IWMCLOCK_H
#define IWMCLOCK_H

#include "bus.h"
#include "../../clock/Clock.h"

class iwmClock : public virtualDevice
{
private:
    void set_tz(const iwm_decoded_cmd_t &cmd);
    void set_alternate_tz(const iwm_decoded_cmd_t &cmd);
    std::string alternate_tz = "";
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

#endif /* IWMCLOCK_H */
