#include <Arduino.h>
#include "networkDeviceSpec.h"

class networkProtocol
{
public:
    virtual bool open(networkDeviceSpec* spec) = 0;
    virtual bool close() = 0;
    virtual bool read(byte* rx_buf, unsigned short len) = 0;
    virtual bool write(byte* tx_buf, unsigned short len) = 0;
    virtual bool status(byte* status_buf) = 0;
};