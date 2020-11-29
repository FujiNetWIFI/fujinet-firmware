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

    if (cmdFrame->aux1 == 6)
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
    bool file_closed = false;
    // call base class.
    NetworkProtocol::close();

    switch (openMode)
    {
    case FILE:
        file_closed = close_file();
        break;
    case DIR:
        file_closed = close_dir();
        break;
    default:
        file_closed = false;
    }

    if (file_closed == false)
        fserror_to_error();

    if (umount() == true)
        return true;
    
    return false;
}

bool NetworkProtocolFS::read(unsigned short len)
{
    switch (openMode)
    {
    case FILE:
        return read_file(len);
        break;
    case DIR:
        return read_dir(len);
        break;
    default:
        return true;
    }
}

bool NetworkProtocolFS::write(unsigned short len)
{
    return write_file(len); // Do more here? not sure.
}

bool NetworkProtocolFS::status(NetworkStatus *status)
{
    switch (openMode)
    {
    case FILE:
        return status_file(status);
        break;
    case DIR:
        return status_dir(status);
        break;
    default:
        return true;
    }
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
    update_dir_filename(path);
    path = resolve(path);
    update_dir_filename(path);
    openMode = FILE;
    return error != NETWORK_ERROR_SUCCESS;
}

bool NetworkProtocolFS::open_dir(string path)
{
    openMode = DIR;
    dirBuffer.clear();
    update_dir_filename(path);

    // assume everything if no filename.
    if (filename.empty())
        filename = "*";

    return error != NETWORK_ERROR_SUCCESS;
}

void NetworkProtocolFS::update_dir_filename(string path)
{
    size_t found = path.find_last_of("/");

    dir = path.substr(0, found + 1);
    filename = path.substr(found + 1);

    // transform the possible everything wildcards
    if (filename == "*.*" || filename == "-" || filename == "**" || filename == "*")
        filename = "*";
}

bool NetworkProtocolFS::rename(uint8_t* sp_buf, unsigned short len)
{
    // Preprocessing routine to parse out comma position.

    size_t comma_pos = filename.find_first_of(",");

    if (comma_pos == string::npos)
        return false;

    destFilename = dir + filename.substr(comma_pos + 1);
    filename = dir + filename.substr(0, comma_pos);

    return comma_pos != string::npos;
}