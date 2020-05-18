#include "networkProtocolFTP.h"
#include "../../include/debug.h"

networkProtocolFTP::networkProtocolFTP()
{

}

networkProtocolFTP::~networkProtocolFTP()
{

}

bool networkProtocolFTP::open(EdUrlParser *urlParser, cmdFrame_t *cmdFrame)
{
    return false;
}

bool networkProtocolFTP::close()
{
    return false;
}

bool networkProtocolFTP::read(byte *rx_buf, unsigned short len)
{
    return false;
}

bool networkProtocolFTP::write(byte *tx_buf, unsigned short len)
{
    return false;
}

bool networkProtocolFTP::status(byte *status_buf)
{
    return false;
}

bool networkProtocolFTP::special(byte* sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    return false;
}

bool networkProtocolFTP::special_supported_00_command(unsigned char comnd)
{
    return false;
}