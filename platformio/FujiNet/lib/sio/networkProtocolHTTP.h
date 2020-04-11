#include <Arduino.h>

#ifdef ESP8266
#include <ESP8266WiFi.h>
#endif

#ifdef ESP32
#include <WiFi.h>
#include <WiFiClient.h>
#endif

#include "sio.h"
#include "networkDeviceSpec.h"
#include "networkProtocol.h"

class networkProtocolHTTP : public networkProtocol
{
public:
    networkProtocolHTTP();
    virtual ~networkProtocolHTTP();

    virtual bool open(networkDeviceSpec* spec);
    virtual bool close();
    virtual bool read(byte* rx_buf, unsigned short len);
    virtual bool write(byte* tx_buf, unsigned short len);
    virtual bool status(byte* status_buf);
    virtual bool special(byte* sp_buf, unsigned short len, cmdFrame_t* cmdFrame);
    virtual bool special_supported_00_command(unsigned char comnd);

private:

    byte client_error_code;

    bool special_accept_connection();
};