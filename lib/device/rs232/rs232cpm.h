
#ifndef RS232CPM_H
#define RS232CPM_H

#include "../cpm/cpm.h"

class rs232CPM : public cpmDevice
{
private:
    void rs232_status(FujiStatusReq reqType) override;
    void rs232_process(FujiBusPacket &packet) override;
};

#endif /* RS232CPM_H */
