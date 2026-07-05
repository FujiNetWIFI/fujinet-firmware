
#ifndef DRIVEWIRECPM_H
#define DRIVEWIRECPM_H

#ifdef ESP_PLATFORM

#include <string>

#include "../cpm/cpm.h"

class drivewireCPM : public cpmQueueDevice
{
private:
    std::string response;

    TaskHandle_t cpmTaskHandle = NULL;

public:
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
