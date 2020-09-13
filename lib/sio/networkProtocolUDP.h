#ifndef NETWORKPROTOCOLUDP
#define NETWORKPROTOCOLUDP
#include "../tcpip/fnUDP.h"

#include "sio.h"
#include "networkProtocol.h"
#include "EdUrlParser.h"

class networkProtocolUDP : public networkProtocol 
{
public:
    networkProtocolUDP();
    virtual ~networkProtocolUDP();

    virtual bool open(EdUrlParser* urlParser, cmdFrame_t* cmdFrame, enable_interrupt_t enable_interrupt);
    virtual bool close(enable_interrupt_t enable_interrupt);
    virtual bool read(uint8_t* rx_buf, unsigned short len);
    virtual bool write(uint8_t* tx_buf, unsigned short len);
    virtual bool status(uint8_t* status_buf);
    virtual bool special(uint8_t* sp_buf, unsigned short len, cmdFrame_t* cmdFrame);
    virtual int available();

    virtual bool special_supported_80_command(unsigned char comnd);

private:
    //WiFiUDP udp;
    fnUDP udp;
    char dest[64];
    unsigned short port;
    char saved_rx_buffer[512];
    unsigned short saved_rx_buffer_len=0;

    bool special_set_destination(uint8_t* sp_buf, unsigned short len);
};

#endif // NETWORKPROTOCOLUDP
