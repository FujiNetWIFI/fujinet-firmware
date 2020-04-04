#ifndef NETWORKPROTOCOL_H
#define NETWORKPROTOCOL_H

#include <Arduino.h>
#include "networkDeviceSpec.h"

class networkProtocol
{
public:
    virtual ~networkProtocol() { }

    bool connectionIsServer=false;

    virtual bool open(networkDeviceSpec* spec) = 0;
    virtual bool close() = 0;
    virtual bool read(byte* rx_buf, unsigned short len) = 0;
    virtual bool write(byte* tx_buf, unsigned short len) = 0;
    virtual bool status(byte* status_buf) = 0;
    virtual bool special(byte* sp_buf, unsigned short len, cmdFrame_t* cmdFrame) = 0;
};

#endif /* NETWORKPROTOCOL_H */