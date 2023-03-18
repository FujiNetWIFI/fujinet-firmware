
#ifndef RC2014CPM_H
#define RC2014CPM_H

#include "bus.h"


#define FOLDERCHAR '/'

// Silly typedefs that runcpm uses
typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;

class rc2014CPM : public virtualDevice
{
private:

    void rc2014_status();
    void rc2014_process(uint32_t commanddata, uint8_t checksum) override;

public:
    bool cpmActive = false; 
    void init_cpm(int baud);
    void rc2014_handle_cpm();
    
};

#endif /* RC2014CPM_H */
