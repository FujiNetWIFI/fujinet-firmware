#ifndef IWMNETWORK_H
#define IWMNETWORK_H

#include "NDevice.h"

class iwmNetwork : public NDevice
{
public:
    iwm_device_info_block_t create_dib_reply_packet() override;
    iwm_device_status_block_t create_status_reply_packet() override;

    void iwm_ctrl(const iwm_decoded_cmd_t &cmd) override;
    void iwm_status(const iwm_decoded_cmd_t &cmd) override;
    void iwm_read(const iwm_decoded_cmd_t &cmd) override;
    void iwm_write(const iwm_decoded_cmd_t &cmd) override;
};

#endif /* IWMNETWORK_H */
