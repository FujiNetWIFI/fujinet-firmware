
#ifndef RS232CPM_H
#define RS232CPM_H

#include "bus.h"


#define FOLDERCHAR '/'

// Silly typedefs that runcpm uses
typedef unsigned char   uint8;
typedef unsigned short  uint16;
typedef unsigned int    uint32;

class rs232CPM : public virtualDevice
{
private:

    void rs232_status(FujiStatusReq reqType) override;
    void rs232_process(FujiBusCommand& command) override;

public:
    bool cpmActive = false;
    void init_cpm(int baud);
    void rs232_handle_cpm();

};

#endif /* RS232CPM_H */
