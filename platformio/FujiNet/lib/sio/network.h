#ifndef NETWORK_H
#define NETWORK_H

#include <Arduino.h>

#ifdef ESP8266
#include <ESP8266Wifi.h>
#endif

#include "sio.h"

#define NUM_DEVICES 8
#define INPUT_BUFFER_SIZE 2048
#define OUTPUT_BUFFER_SIZE 2048
#define DEVICESPEC_SIZE 256

#define OPEN_STATUS_DEVICE_ERROR 144
#define OPEN_STATUS_INVALID_DEVICESPEC 165

class sioNetwork : public sioDevice
{

private:
    bool allocate_buffers();

protected:
    union {
        struct
        {
            char device[4];
            char protocol[16];
            char path[234];
            unsigned short port;
        };
        char rawData[DEVICESPEC_SIZE];
    } deviceSpec;

    unsigned char err;
    byte ck;
    byte* rx_buf;
    byte* tx_buf;

    union {
        struct 
        {
            unsigned char errorCode;
            unsigned char reserved1;
            unsigned char reserved2;
            unsigned char reserved3;
        } openStatus;
        unsigned char rawData[4];
    };

public:
    virtual void open();
    virtual void close();
    virtual void read();
    virtual void write();
    virtual void status();

    bool parse_deviceSpec(char *tmp);
};

#endif /* NETWORK_H */