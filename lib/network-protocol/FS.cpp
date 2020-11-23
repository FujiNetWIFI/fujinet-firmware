/**
 * NetworkProtocolFS
 * 
 * Implementation
 */

#include "FS.h"
#include "status_error_codes.h"

NetworkProtocolFS::NetworkProtocolFS(string *rx_buf, string *tx_buf, string *sp_buf)
    : NetworkProtocol(rx_buf, tx_buf, sp_buf)
{
}

NetworkProtocolFS::~NetworkProtocolFS()
{
}

bool NetworkProtocolFS::open(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    if (cmdFrame->aux2 == 6)
    {
        return open_dir(url, cmdFrame);
    }
    else
    {
        return open_file(url, cmdFrame);
    }
}

bool NetworkProtocolFS::close()
{
    error = NETWORK_ERROR_NOT_IMPLEMENTED;
    return true; // error until proven guilty.
}

bool NetworkProtocolFS::read(unsigned short len)
{
    error = NETWORK_ERROR_NOT_IMPLEMENTED;
    return true;
}

bool NetworkProtocolFS::write(unsigned short len)
{
    error = NETWORK_ERROR_NOT_IMPLEMENTED;
    return true;
}

bool NetworkProtocolFS::status(NetworkStatus *status)
{
    error = NETWORK_ERROR_NOT_IMPLEMENTED;
    return true;
}

uint8_t NetworkProtocolFS::special_inquiry(uint8_t cmd)
{
    uint8_t ret;

    switch (cmd)
    {
        default:
            ret = 0xFF; // Not implemented.
    }

    return ret;
}

bool NetworkProtocolFS::special_00(cmdFrame_t *cmdFrame)
{
    switch(cmdFrame->comnd)
    {
        default:
            error = NETWORK_ERROR_NOT_IMPLEMENTED;
            return true;
    }
}

bool NetworkProtocolFS::special_40(uint8_t* sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    switch(cmdFrame->comnd)
    {
        default:
            error = NETWORK_ERROR_NOT_IMPLEMENTED;
            return true;
    }
}

bool NetworkProtocolFS::special_80(uint8_t* sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    switch(cmdFrame->comnd)
    {
        default:
            error = NETWORK_ERROR_NOT_IMPLEMENTED;
            return true;
    }
}
