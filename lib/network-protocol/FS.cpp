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
    // call base class.
    NetworkProtocol::close();

    if (umount() == true)
        return true;

    switch (openMode)
    {
    case FILE:
        return close_file();
        break;
    case DIR:
        return close_dir();
        break;
    default:
        return true;
    }
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
    dir = path.substr(0, path.find_last_of("/") + 1);
    filename = path.substr(path.find_last_of("/") + 1);

    // transform the possible everything wildcards
    if (filename == "*.*" || filename == "-" || filename == "**" || filename == "*")
        filename = "*";
}

bool NetworkProtocolFS::chdir(uint8_t *sp_buf, unsigned short len)
{
    string pathSpec = string((char *)sp_buf, len);

    if (pathSpec == "..") // Devance path
    {
        vector<int> pathLocations;
        for (int i = 0; i < dir.size(); i++)
        {
            if (dir[i] == '/')
            {
                pathLocations.push_back(i);
            }
        }

        if (dir[dir.size() - 1] == '/')
        {
            // Get rid of last path segment.
            pathLocations.pop_back();
        }

        // truncate to that location.
        dir = dir.substr(0, pathLocations.back() + 1);
    }
    else if (pathSpec[0] == '/') // Overwrite path.
    {
        dir = pathSpec;
    }
    else // append to path.
    {
        dir += pathSpec;
    }

    return false;
}