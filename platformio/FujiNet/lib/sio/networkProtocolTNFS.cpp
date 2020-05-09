#include "networkProtocolTNFS.h"
#include "../../include/debug.h"

networkProtocolTNFS::networkProtocolTNFS()
{

}

networkProtocolTNFS::~networkProtocolTNFS()
{

}

bool networkProtocolTNFS::open(EdUrlParser *urlParser, cmdFrame_t *cmdFrame)
{
    return false;
}

bool networkProtocolTNFS::close()
{
    return false;
}

bool networkProtocolTNFS::read(byte *rx_buf, unsigned short len)
{
    return false;
}

bool networkProtocolTNFS::write(byte *tx_buf, unsigned short len)
{
    return false;
}

bool networkProtocolTNFS::status(byte *status_buf)
{
    return false;
}

bool networkProtocolTNFS::special(byte* sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    return false;
}

bool networkProtocolTNFS::special_supported_00_command(unsigned char comnd)
{
    return false;
}