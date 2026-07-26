
#ifndef SIOCPM_H
#define SIOCPM_H

#include "bus.h"


#define FOLDERCHAR '/'

// Silly typedefs that runcpm uses
typedef unsigned char   uint8;
typedef unsigned short  uint16;
typedef unsigned int    uint32;

class sioCPM : public virtualDevice
{
private:

    void sio_status(const FujiSIOPacket &packet) override;
    void sio_process(const FujiSIOPacket &packet) override;

public:
    bool cpmActive = false;
    void init_cpm(int baud);
    void sio_handle_cpm();

};

#endif /* SIOCPM_H */
