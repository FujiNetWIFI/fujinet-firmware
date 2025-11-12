#ifndef APETIME_H
#define APETIME_H

#include "bus.h"

class rs232ApeTime : public virtualDevice
{
private:
    std::string ape_timezone;

    void _rs232_get_time(bool use_timezone);
    void _rs232_set_tz(std::string newTZ);

public:
    void rs232_process(FujiBusPacket &packet) override;
    void rs232_status(FujiStatusReq reqType) override {}
};

#endif // APETIME_H
