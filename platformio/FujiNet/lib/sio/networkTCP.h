#ifndef NETWORKTCP_H
#define NETWORKTCP_H

#include <Arduino.h>

#ifdef ESP32
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <WiFiServer.h>
#endif

#ifdef ESP8266
#endif

#include "sio.h"
#include "network.h"

class sioNetworkTCP : public sioNetwork
{

public:
    virtual void open();
    virtual void close();
    virtual void read();
    virtual void write();
    virtual void status();   
};

#endif /* NETWORKTCP_H */