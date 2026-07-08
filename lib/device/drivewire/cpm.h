
#ifndef DRIVEWIRECPM_H
#define DRIVEWIRECPM_H

#ifdef ESP_PLATFORM

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

public:
    drivewireCPM();
    // virtual ~drivewireCPM();
    void process(fujiCommandID_t cmd);
    void boot();
    void read();
    void write();
    void status();
};

extern drivewireCPM theCPM;
#endif /* ESP_PLATFORM */

#endif /* DRIVEWIRECPM_H */
