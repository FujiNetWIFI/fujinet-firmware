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
    // Call base class.
    NetworkProtocol::open(url, cmdFrame);

    update_dir_filename(url->path);

    if (mount(url->hostName, dir) == true)
        return true;

    if (cmdFrame->aux2 == 6)
    {
        return open_dir(url->path);
    }
    else
    {
        return open_file(url->path);
    }
}

bool NetworkProtocolFS::close()
{
    if (umount() == true)
        return true;

    return NetworkProtocol::close();
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
    switch (cmdFrame->comnd)
    {
    default:
        error = NETWORK_ERROR_NOT_IMPLEMENTED;
        return true;
    }
}

bool NetworkProtocolFS::special_40(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    switch (cmdFrame->comnd)
    {
    default:
        error = NETWORK_ERROR_NOT_IMPLEMENTED;
        return true;
    }
}

bool NetworkProtocolFS::special_80(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    switch (cmdFrame->comnd)
    {
    default:
        error = NETWORK_ERROR_NOT_IMPLEMENTED;
        return true;
    }
}

bool NetworkProtocolFS::open_file(string path)
{
    path = resolve(path);
    update_dir_filename(path);
    openMode = FILE;
    return error != NETWORK_ERROR_SUCCESS;
}

bool NetworkProtocolFS::open_dir(string path)
{
    openMode = DIR;
    return error != NETWORK_ERROR_SUCCESS;
}

void NetworkProtocolFS::update_dir_filename(string path)
{
    dir = path.substr(0, path.find_last_of("/") + 1);
    filename = path.substr(path.find_last_of("/") + 1);
}