
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

public:
    bool cpmActive = false; 
    void init_cpm(int baud);
    virtual void sio_status();
    void sio_handle_cpm();
    
};

#endif /* IWMCPM_H */