#ifndef APETIME_H
#define APETIME_H

#include "bus.h"

class rs232ApeTime : public virtualDevice
{
private:
    void _rs232_get_time(bool use_timezone);
    void _rs232_set_tz();

public:
    void rs232_process(cmdFrame_t *cmd_ptr) override;
    virtual void rs232_status() override {};
};

#endif // APETIME_H
