#include "networkProtocolHTTP.h"

networkProtocolHTTP::networkProtocolHTTP()
{

}

networkProtocolHTTP::~networkProtocolHTTP()
{

}

bool networkProtocolHTTP::open(networkDeviceSpec *spec, cmdFrame_t* cmdFrame)
{
    String url = "http://" + String(spec->path);
    return client.begin(url);
}

bool networkProtocolHTTP::close()
{
    client.end();
    return true;
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