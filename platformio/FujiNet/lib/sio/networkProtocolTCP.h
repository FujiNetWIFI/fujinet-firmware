#include <Arduino.h>

#ifdef ESP8266
#include <esp8266wifi.h>
#endif

#ifdef ESP32
#include <Wifi.h>
#include <WiFiClient.h>
#endif

#include "sio.h"
#include "networkDeviceSpec.h"
#include "networkProtocol.h"

class networkProtocolTCP : public networkProtocol
{
public:
    networkProtocolTCP();
    ~networkProtocolTCP();

    virtual bool open(networkDeviceSpec* spec);
    virtual bool close();
    virtual bool read(byte* rx_buf, unsigned short len);
    virtual bool write(byte* tx_buf, unsigned short len);
    virtual bool status(byte* status_buf);
    virtual bool special(byte* sp_buf, unsigned short len, cmdFrame_t* cmdFrame);

private:
    WiFiClient client;
    WiFiServer* server;
    
    bool special_accept_connection();
};