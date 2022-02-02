
#ifndef SIOCPM_H
#define SIOCPM_H

#include "bus.h"


#define FOLDERCHAR '/'

// Silly typedefs that runcpm uses
typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;

class sioCPM : public sioDevice
{
private:

    void sio_status() override;
    void sio_process(uint32_t commanddata, uint8_t checksum) override;

public:
    bool cpmActive = false; 
    void init_cpm(int baud);
    void sio_handle_cpm();
    
};

#endif /* SIOCPM_H */