
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
    std::string response;

#ifdef ESP_PLATFORM
    TaskHandle_t cpmTaskHandle = NULL;
#endif /* ESP_PLATFORM */

public:
    drivewireCPM();
    // virtual ~drivewireCPM();
    virtual void process();
    virtual void ready();
    virtual void send_response();
    virtual void boot();
    virtual void read();
    virtual void write();
    virtual void status();
};

extern drivewireCPM theCPM;
#endif /* ESP_PLATFORM */

#endif /* DRIVEWIRECPM_H */