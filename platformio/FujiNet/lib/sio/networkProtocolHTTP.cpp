#include "networkProtocolHTTP.h"

// ctor
networkProtocolHTTP::networkProtocolHTTP()
{

}

networkProtocolHTTP::~networkProtocolHTTP()
{
    
}

bool networkProtocolHTTP::open(networkDeviceSpec *spec)
{
    return false;
}

bool networkProtocolHTTP::close()
{
    return false;
}

bool networkProtocolHTTP::read(byte *rx_buf, unsigned short len)
{
    return false;
}

bool networkProtocolHTTP::write(byte *tx_buf, unsigned short len)
{
    return false;
}

bool networkProtocolHTTP::status(byte *status_buf)
{
    return false;
}

bool networkProtocolHTTP::special(byte *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    return false;
}
