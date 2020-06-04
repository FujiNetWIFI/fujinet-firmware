#ifndef NETWORKPROTOCOLHTTP
#define NETWORKPROTOCOLHTTP

//#include <Arduino.h>
#include <HTTPClient.h>
#include "networkProtocol.h"
#include "../tcpip/fnTcpClient.h"
#include "EdUrlParser.h"
#include "sio.h"

class networkProtocolHTTP : public networkProtocol
{
public:
    networkProtocolHTTP();
    virtual ~networkProtocolHTTP();

    virtual bool open(EdUrlParser *urlParser, cmdFrame_t *cmdFrame);
    virtual bool close();
    virtual bool read(byte *rx_buf, unsigned short len);
    virtual bool write(byte *tx_buf, unsigned short len);
    virtual bool status(byte *status_buf);
    virtual bool special(byte *sp_buf, unsigned short len, cmdFrame_t *cmdFrame);

    virtual bool special_supported_00_command(unsigned char comnd);
    virtual void special_header_toggle(unsigned char a);
    virtual void special_collect_headers_toggle(unsigned char a);
    virtual void special_ca_toggle(unsigned char a);
    virtual bool del(EdUrlParser *urlParser, cmdFrame_t *cmdFrame);
    virtual bool rename(EdUrlParser *urlParser, cmdFrame_t *cmdFrame);
    virtual bool isConnected();
    
private:
    virtual bool startConnection(byte *buf, unsigned short len);

    HTTPClient client;
    fnTcpClient *c;

    char *headerCollection[16];
    size_t headerCollectionIndex = 0;

    bool requestStarted = false;
    enum
    {
        GET,
        POST,
        PUT,
        DIR
    } openMode;
    int resultCode;
    int headerIndex = 0;
    int numHeaders = 0;

    enum
    {
        DATA,
        HEADERS,
        COLLECT_HEADERS,
        CA,
        CMD
    } httpState;

    char cert[2048];
    EdUrlParser *openedUrlParser;
    string openedUrl;
    FILE* fpPUT;
    char nPUT[32];
    
    string rnFrom;
    string rnTo;
    string destURL;
    size_t comma_pos;
};

#endif /* NETWORKPROTOCOLHTTP */
