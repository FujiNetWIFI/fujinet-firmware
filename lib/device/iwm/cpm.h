#ifndef IWMCPM_H
#define IWMCPM_H

#include "../cpm/cpm.h"

class iwmCPM : public cpmQueueDevice
{
private:

#ifdef ESP_PLATFORM // OS
    TaskHandle_t cpmTaskHandle = NULL;
#endif

    void boot();

public:

    void iwm_ctrl(iwm_decoded_cmd_t cmd) override;
    void iwm_open(iwm_decoded_cmd_t cmd) override;
    void iwm_close(iwm_decoded_cmd_t cmd) override;
    void iwm_read(iwm_decoded_cmd_t cmd) override;
    void iwm_write(iwm_decoded_cmd_t cmd) override;
    void iwm_status(iwm_decoded_cmd_t cmd) override;

    void shutdown() override;
    iwm_device_info_block_t create_dib_reply_packet() override;
    iwm_device_status_block_t create_status_reply_packet() override;
};

#endif /* IWMCPM_H */
