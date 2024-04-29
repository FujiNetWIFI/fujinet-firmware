
#ifndef DRIVEWIRECPM_H
#define DRIVEWIRECPM_H

#include "bus.h"


#define FOLDERCHAR '/'

// Silly typedefs that runcpm uses
typedef unsigned char   uint8;
typedef unsigned short  uint16;
typedef unsigned int    uint32;

class drivewireCPM : public virtualDevice
{
private:

#ifdef ESP_PLATFORM
    TaskHandle_t cpmTaskHandle = NULL;
#endif /* ESP_PLATFORM */

    void boot();

public:
    drivewireCPM();
    virtual ~drivewireCPM();
    
};

#endif /* DRIVEWIRECPM_H */