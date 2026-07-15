#ifndef IWMCPM_H
#define IWMCPM_H

#include "bus.h"


#define FOLDERCHAR '/'

// Silly typedefs that runcpm uses
typedef unsigned char   uint8;
typedef unsigned short  uint16;
typedef unsigned int    uint32;

class iwmCPM : public virtualDevice
{
private:

#ifdef ESP_PLATFORM // OS
    TaskHandle_t cpmTaskHandle = NULL;
#endif

    void boot();

public:
    iwmCPM();

    void iwm_ctrl(const iwm_decoded_cmd_t &cmd) override;
    void iwm_open(const iwm_decoded_cmd_t &cmd) override;
    void iwm_close(const iwm_decoded_cmd_t &cmd) override;
    void iwm_read(const iwm_decoded_cmd_t &cmd) override;
    void iwm_write(const iwm_decoded_cmd_t &cmd) override;
    void iwm_status(const iwm_decoded_cmd_t &cmd) override;

    void shutdown() override;
    iwm_device_info_block_t create_dib_reply_packet() override;
    iwm_device_status_block_t create_status_reply_packet() override;
    bool cpmActive = false;
    void init_cpm(int baud);
    virtual void sio_status();
    void sio_handle_cpm();

};

#endif /* IWMCPM_H */
