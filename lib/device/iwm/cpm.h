#ifndef IWMCPM_H
#define IWMCPM_H

#include "bus.h"


#define FOLDERCHAR '/'

// Silly typedefs that runcpm uses
typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;

class iwmCPM : public iwmDevice
{
private:

    TaskHandle_t cpmTaskHandle;    

    void boot();

public:
    iwmCPM();

    void process(iwm_decoded_cmd_t cmd) override;

    void iwm_ctrl(iwm_decoded_cmd_t cmd) override;
    void iwm_open(iwm_decoded_cmd_t cmd) override;
    void iwm_close(iwm_decoded_cmd_t cmd) override;
    void iwm_read(iwm_decoded_cmd_t cmd) override;
    void iwm_write(iwm_decoded_cmd_t cmd) override;
    void iwm_status(iwm_decoded_cmd_t cmd) override;

    void shutdown() override;
    void send_status_reply_packet() override;
    void send_extended_status_reply_packet() override{};
    void send_status_dib_reply_packet() override;
    void send_extended_status_dib_reply_packet() override{};
    bool cpmActive = false; 
    void init_cpm(int baud);
    virtual void sio_status();
    void sio_handle_cpm();
    
};

#endif /* IWMCPM_H */