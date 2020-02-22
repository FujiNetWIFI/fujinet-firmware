#include <Arduino.h>
#include "networkDeviceSpec.h"
#include "networkProtocol.h"

class networkProtocolTCP : public networkProtocol
{
public:
    networkProtocolTCP();

    virtual bool open(networkDeviceSpec* spec);
    virtual bool close();
    virtual bool read(byte* rx_buf, unsigned short len);
    virtual bool write(byte* tx_buf, unsigned short len);
    virtual bool status(byte* status_buf);
};