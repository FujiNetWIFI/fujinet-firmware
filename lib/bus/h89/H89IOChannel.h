#include "IOChannel.h"

class H89IOChannel : public IOChannel
{

protected:
    virtual size_t dataOut(const void *buffer, size_t length) = 0;
    virtual void updateFIFO() = 0;
};