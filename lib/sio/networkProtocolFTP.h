#ifndef NETWORKPROTOCOLFTP
#define NETWORKPROTOCOLFTP

#include "networkProtocol.h"
#include "../tcpip/fnTcpClient.h"
#include "sio.h"
#include "EdUrlParser.h"

class networkProtocolFTP : public networkProtocol
{
public:
    networkProtocolFTP();
    virtual ~networkProtocolFTP();

    virtual bool open(EdUrlParser *urlParser, cmdFrame_t *cmdFrame, enable_interrupt_t enable_interrupt);
    virtual bool close(enable_interrupt_t enable_interrupt);
    virtual bool read(uint8_t *rx_buf, unsigned short len);
    virtual bool write(uint8_t *tx_buf, unsigned short len);
    virtual bool status(uint8_t *status_buf);
    virtual bool special(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame);
    virtual bool del(EdUrlParser *urlParser, cmdFrame_t *cmdFrame);
    virtual bool rename(EdUrlParser *urlParser, cmdFrame_t *cmdFrame);
    virtual bool mkdir(EdUrlParser *urlParser, cmdFrame_t *cmdFrame);
    virtual bool rmdir(EdUrlParser *urlParser, cmdFrame_t *cmdFrame);
    virtual int available();
    
    virtual bool special_supported_00_command(unsigned char comnd);

private:
    string hostName;
    fnTcpClient control;
    fnTcpClient data;
    string controlResponse;
    long dataSize;
    unsigned short dataPort;
    unsigned char aux1;

    bool ftpLogin(EdUrlParser *urlParser);
    string ftpResult();
    unsigned short parsePort(string response);
};

#endif /* NETWORKPROTOCOLFTP */
