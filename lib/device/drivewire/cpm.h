
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

    void drivewire_process(uint32_t commanddata, uint8_t checksum);

public:
    bool cpmActive = false; 
    void init_cpm(int baud);
    void drivewire_handle_cpm();
    
};

#endif /* DRIVEWIRECPM_H */