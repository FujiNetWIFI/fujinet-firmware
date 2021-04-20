#ifndef IEC_APETIME_H
#define IEC_APETIME_H

#include "iecBus.h"
#include "iecDevice.h"

class iecApeTime : public iecDevice
{
public:
    void _process(void) override;
    virtual void _status() override {};
};

#endif // APETIME_H
