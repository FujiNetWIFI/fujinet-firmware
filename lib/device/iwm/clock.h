#ifndef IWMCLOCK_H
#define IWMCLOCK_H

#include "bus.h"

class iwmClock : public iwmDevice
{

public:
    iwmClock();

    void process(iwm_decoded_cmd_t cmd) override;

    void iwm_status(iwm_decoded_cmd_t cmd) override;

    void shutdown() override;
    void send_status_reply_packet() override;
    void send_extended_status_reply_packet() override{};
    void send_status_dib_reply_packet() override;
    void send_extended_status_dib_reply_packet() override{};
    
};

#endif /* IWMCLOCK_H */
