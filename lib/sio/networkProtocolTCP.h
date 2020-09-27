#ifndef NETWORKPROTOCOLTCP
#define NETWORKPROTOCOLTCP

#include "../tcpip/fnTcpClient.h"
#include "../tcpip/fnTcpServer.h"

#include "sio.h"
#include "EdUrlParser.h"
#include "networkProtocol.h"

class networkProtocolTCP : public networkProtocol
{
public:
    networkProtocolTCP();
    virtual ~networkProtocolTCP();

    virtual bool open(EdUrlParser* urlParser, cmdFrame_t* cmdFrame);
    virtual bool close();
    virtual bool read(uint8_t* rx_buf, unsigned short len);
    virtual bool write(uint8_t* tx_buf, unsigned short len);
    virtual bool status(uint8_t* status_buf);
    virtual bool special(uint8_t* sp_buf, unsigned short len, cmdFrame_t* cmdFrame);
    virtual bool special_supported_00_command(unsigned char comnd);
    virtual bool isConnected();
    virtual int available();
    
private:
    fnTcpClient client;
    fnTcpServer * server;
    uint8_t client_error_code;

    bool _isConnected;
    bool special_accept_connection();
};

#endif // NETWORKPROTOCOLTCP