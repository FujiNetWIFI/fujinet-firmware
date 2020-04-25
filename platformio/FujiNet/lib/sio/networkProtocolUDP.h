#include <Arduino.h>
#include <WiFiUdp.h>

#include "sio.h"
#include "networkDeviceSpec.h"
#include "networkProtocol.h"

class networkProtocolUDP : public networkProtocol 
{
public:
    networkProtocolUDP();
    virtual ~networkProtocolUDP();

    virtual bool open(networkDeviceSpec* spec, cmdFrame_t* cmdFrame);
    virtual bool close();
    virtual bool read(byte* rx_buf, unsigned short len);
    virtual bool write(byte* tx_buf, unsigned short len);
    virtual bool status(byte* status_buf);
    virtual bool special(byte* sp_buf, unsigned short len, cmdFrame_t* cmdFrame);

    virtual bool special_supported_80_command(unsigned char comnd);

private:
    WiFiUDP udp;
    char dest[64];
    unsigned short port;
    byte* saved_rx_buffer;
    unsigned short* saved_rx_buffer_len;

    bool special_set_destination(byte* sp_buf, unsigned short len, unsigned short new_port);
};