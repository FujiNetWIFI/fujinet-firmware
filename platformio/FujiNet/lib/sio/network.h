#ifndef NETWORK_H
#define NETWORK_H

#include <Arduino.h>

#ifdef ESP8266
#include <ESP8266Wifi.h>
#endif

#include "sio.h"
#include "networkDeviceSpec.h"
#include "networkProtocol.h"

#define NUM_DEVICES 8
#define INPUT_BUFFER_SIZE 2048
#define OUTPUT_BUFFER_SIZE 2048
#define DEVICESPEC_SIZE 256

#define OPEN_STATUS_NOT_CONNECTED 128
#define OPEN_STATUS_DEVICE_ERROR 144
#define OPEN_STATUS_INVALID_DEVICESPEC 165

class sioNetwork : public sioDevice
{

private:
    bool allocate_buffers();
    bool open_protocol();

protected:
    
    networkDeviceSpec deviceSpec;
    networkProtocol* protocol;

    unsigned char err;
    byte ck;
    byte* rx_buf;
    byte* tx_buf;
    unsigned short rx_buf_len;
    unsigned short tx_buf_len;

    union 
    {
        struct
        {
            unsigned short rx_buf_len;
            unsigned char connection_status;
            unsigned char error;
        };
        byte rawData[4];
    } status_buf;
    

public:
    virtual void open();
    virtual void close();
    virtual void read();
    virtual void write();
    virtual void status();
};

#endif /* NETWORK_H */