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
    virtual bool read(byte* rx_buf, unsigned short len);
    virtual bool write(byte* tx_buf, unsigned short len);
    virtual bool status(byte* status_buf);
    virtual bool special(byte* sp_buf, unsigned short len, cmdFrame_t* cmdFrame);
    virtual bool special_supported_00_command(unsigned char comnd);
    virtual bool isConnected();
    
private:
    fnTcpClient client;
    fnTcpServer * server;
    byte client_error_code;

    bool special_accept_connection();
};

#endif // NETWORKPROTOCOLTCP