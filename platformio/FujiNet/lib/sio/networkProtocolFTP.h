#ifndef NETWORKPROTOCOLFTP
#define NETWORKPROTOCOLFTP

#include <Arduino.h>
#include "networkProtocol.h"
#include "sio.h"
#include "WiFiClient.h"

class networkProtocolFTP : public networkProtocol
{
public:
    networkProtocolFTP();
    virtual ~networkProtocolFTP();

    virtual bool open(EdUrlParser *urlParser, cmdFrame_t *cmdFrame);
    virtual bool close();
    virtual bool read(byte *rx_buf, unsigned short len);
    virtual bool write(byte *tx_buf, unsigned short len);
    virtual bool status(byte *status_buf);
    virtual bool special(byte *sp_buf, unsigned short len, cmdFrame_t *cmdFrame);

    virtual bool special_supported_00_command(unsigned char comnd);

protected:
    bool ftpExpect(string resultCode);
    unsigned short parsePort(string response);

private:
    string hostName;
    WiFiClient control;
    WiFiClient data;
    string controlResponse;
    long dataSize;
    unsigned short dataPort;

};

#endif /* NETWORKPROTOCOLFTP */