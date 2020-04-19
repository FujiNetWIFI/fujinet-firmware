#ifndef NETWORKPROTOCOLHTTP
#define NETWORKPROTOCOLHTTP

#include <Arduino.h>
#include <HTTPClient.h>
#include "networkProtocol.h"
#include "networkDeviceSpec.h"
#include "sio.h"

class networkProtocolHTTP : public networkProtocol
{
public:
    networkProtocolHTTP();
    virtual ~networkProtocolHTTP();

    virtual bool open(networkDeviceSpec *spec, cmdFrame_t *cmdFrame);
    virtual bool close();
    virtual bool read(byte *rx_buf, unsigned short len);
    virtual bool write(byte *tx_buf, unsigned short len);
    virtual bool status(byte *status_buf);
    virtual bool special(byte *sp_buf, unsigned short len, cmdFrame_t *cmdFrame);

    virtual bool special_supported_00_command(unsigned char comnd);
    virtual void special_header_toggle(unsigned char a);
    virtual void special_collect_headers_toggle(unsigned char a);
    virtual void special_ca_toggle(unsigned char a);

private:
    virtual bool startConnection(byte *buf, unsigned short len);

    HTTPClient client;
    WiFiClient* c;

    char* headerCollection[16];
    size_t headerCollectionIndex=0;

    bool requestStarted = false;
    enum
    {
        GET,
        POST,
        PUT
    } openMode;
    int resultCode;
    int headerIndex = 0;
    int numHeaders = 0;

    enum
    {
        DATA,
        HEADERS,
        COLLECT_HEADERS,
        CA
    } httpState;

    char cert[2048];
    String openedURL;

};

#endif /* NETWORKPROTOCOLHTTP */