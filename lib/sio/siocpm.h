
#ifndef SIOCPM_H
#define SIOCPM_H

#include "sio.h"

class sioCPM : public sioDevice
{
private:

    void sio_status() override;
    void sio_process(uint32_t commanddata, uint8_t checksum) override;

public:
    bool cpmActive = false; 

    void sio_handle_cpm();
    
};

#endif /* SIOCPM_H */