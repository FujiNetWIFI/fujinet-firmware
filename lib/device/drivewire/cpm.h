
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
    bool processCommand(const FujiDWPacket &packet) override;
    void boot();
    void read(uint16_t len);
    void write(uint16_t len);
    void status();
};

extern drivewireCPM theCPM;
#endif /* ESP_PLATFORM */

#endif /* DRIVEWIRECPM_H */
