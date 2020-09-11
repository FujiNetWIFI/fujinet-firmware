#ifndef NETWORKPROTOCOLHTTP
#define NETWORKPROTOCOLHTTP

#include "networkProtocol.h"

#include "../http/fnHttpClient.h"

#include "EdUrlParser.h"
#include "sio.h"

class DAVEntry
{
public:
    string filename;
    size_t filesize;
};

class networkProtocolHTTP : public networkProtocol
{
public:
    networkProtocolHTTP();
    virtual ~networkProtocolHTTP();

    virtual bool open(EdUrlParser *urlParser, cmdFrame_t *cmdFrame, enable_interrupt_t enable_interrupt);
    virtual bool close(enable_interrupt_t enable_interrupt);
    virtual bool read(uint8_t *rx_buf, unsigned short len);
    virtual bool write(uint8_t *tx_buf, unsigned short len);
    virtual bool status(uint8_t *status_buf);
    virtual bool special(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame);

    virtual bool special_supported_00_command(unsigned char comnd);
    virtual void special_header_toggle(unsigned char a);
    virtual void special_collect_headers_toggle(unsigned char a);
    virtual void special_ca_toggle(unsigned char a);
    virtual bool del(EdUrlParser *urlParser, cmdFrame_t *cmdFrame);
    virtual bool rename(EdUrlParser *urlParser, cmdFrame_t *cmdFrame);
    virtual bool mkdir(EdUrlParser *urlParser, cmdFrame_t *cmdFrame);
    virtual bool rmdir(EdUrlParser *urlParser, cmdFrame_t *cmdFrame);

    virtual bool isConnected();

private:
    virtual bool startConnection(uint8_t *buf, unsigned short len);
    void parseDir();

    //HTTPClient client;
    fnHttpClient client;
    //fnTcpClient *c;

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
    unsigned char aux1;
    unsigned char aux2;
    string dirString;
    vector<DAVEntry> dirEntries;

};

#endif /* NETWORKPROTOCOLHTTP */
