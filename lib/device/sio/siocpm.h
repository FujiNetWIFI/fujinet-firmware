
#ifndef SIOCPM_H
#define SIOCPM_H

#include "../cpm/cpm.h"

class sioCPM : public cpmDevice
{
private:
    void sio_status() override;
    void sio_process(uint32_t commanddata, uint8_t checksum) override;

    // Console endpoint: the CP/M console is the raw Atari SIO link.
    int     ep_kbhit() override;
    uint8_t ep_getch() override;
    void    ep_putch(uint8_t ch) override;

public:
    void init_cpm(int baud) override;
};

#endif /* SIOCPM_H */
