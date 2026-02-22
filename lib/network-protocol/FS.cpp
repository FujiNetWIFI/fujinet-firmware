/**
 * NetworkProtocolFS
 *
 * Implementation
 */

#include "FS.h"
#include "fujiCommandID.h"

#include <stdlib.h>
#include <string.h>

#include "../../include/debug.h"

#include "status_error_codes.h"
#include "utils.h"

#include <cstring>
#include <memory>
#include <iostream>
#include <vector>

#define ENTRY_BUFFER_SIZE 256

NetworkProtocolFS::NetworkProtocolFS(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf)
    : NetworkProtocol(rx_buf, tx_buf, sp_buf)
{
    fileSize = 0;
}

NetworkProtocolFS::~NetworkProtocolFS()
{
}

protocolError_t NetworkProtocolFS::open(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    // Call base class.
    NetworkProtocol::open(url, cmdFrame);
    fileSize = 0;

    update_dir_filename(opened_url);

    if (mount(url) != PROTOCOL_ERROR::NONE)
        return PROTOCOL_ERROR::UNSPECIFIED;

    if (cmdFrame->aux1 == NETPROTO_OPEN_DIRECTORY || cmdFrame->aux1 == NETPROTO_OPEN_DIRECTORY_ALT)
    {
        return open_dir();
    }
    else
    {
        return open_file();
    }
}

protocolError_t NetworkProtocolFS::open_file()
{
    update_dir_filename(opened_url);

    if (aux1_open == NETPROTO_OPEN_READ || aux1_open == NETPROTO_OPEN_WRITE)
        resolve();
    else
        stat();

    update_dir_filename(opened_url);

    openMode = FILE;

    if (opened_url->path.empty())
        return PROTOCOL_ERROR::UNSPECIFIED;

    return open_file_handle();
}

protocolError_t NetworkProtocolFS::open_dir()
{
    openMode = DIR;
#ifndef BUILD_ATARI
    this->setLineEnding("\r\n");
#endif /* BUILD_RS232 */
    dirBuffer.clear();
    dirBuffer.shrink_to_fit();
    update_dir_filename(opened_url);

    // assume everything if no filename.
    if (filename.empty())
        filename = "*";

#ifdef VERBOSE_PROTOCOL
    Debug_printf("NetworkProtocolFS::open_dir(%s)\r\n", opened_url->url.c_str());
#endif

    if (opened_url->path.empty())
    {
        return PROTOCOL_ERROR::UNSPECIFIED;
    }

    if (open_dir_handle() != PROTOCOL_ERROR::NONE)
    {
        fserror_to_error();
        return PROTOCOL_ERROR::UNSPECIFIED;
    }

    std::vector<uint8_t> entryBuffer(ENTRY_BUFFER_SIZE);

    while (read_dir_entry((char *)entryBuffer.data(), ENTRY_BUFFER_SIZE - 1) == PROTOCOL_ERROR::NONE)
    {
        if (entryBuffer.at(0) == '.' || entryBuffer.at(0) == '/')
            continue;

        if (aux2_open & NETPROTO_A2_FLAG)
        {
            // Long entry
            if (aux2_open == NETPROTO_A2_80COL) // Apple2 80 col format.
                dirBuffer += util_long_entry_apple2_80col((char *)entryBuffer.data(), fileSize, is_directory) + lineEnding;
            else
                dirBuffer += util_long_entry((char *)entryBuffer.data(), fileSize, is_directory) + lineEnding;
        }
        else
        {
            // 8.3 entry
            dirBuffer += util_entry(util_crunch((char *)entryBuffer.data()), fileSize, is_directory, is_locked) + lineEnding;
        }
        fserror_to_error();

        // Clearing the buffer for reuse
        std::fill(entryBuffer.begin(), entryBuffer.end(), 0); // fenrock was right.
    }

#ifdef BUILD_ATARI
    // Finally, drop a FREE SECTORS trailer.
    dirBuffer += "999+FREE SECTORS\x9b";
#endif /* BUILD_ATARI */

    if (error == NDEV_STATUS::END_OF_FILE)
        error = NDEV_STATUS::SUCCESS;

    return error == NDEV_STATUS::SUCCESS ? PROTOCOL_ERROR::NONE : PROTOCOL_ERROR::UNSPECIFIED;
}

void NetworkProtocolFS::update_dir_filename(PeoplesUrlParser *url)
{
    size_t found = url->path.find_last_of("/");

    dir = util_get_canonical_path(url->path.substr(0, found + 1));
    filename = url->path.substr(found + 1);

    // transform the possible everything wildcards
    if (filename == "*.*" || filename == "-" || filename == "**" || filename == "*")
        filename = "*";
}

protocolError_t NetworkProtocolFS::close()
{
    protocolError_t err;
    // call base class.
    NetworkProtocol::close();

    switch (openMode)
    {
    case FILE:
        err = close_file();
        break;
    case DIR:
        err = close_dir();
        break;
    default:
        err = PROTOCOL_ERROR::UNSPECIFIED;
    }

    if (err != PROTOCOL_ERROR::NONE)
        fserror_to_error();

    if (umount() != PROTOCOL_ERROR::NONE)
        return PROTOCOL_ERROR::UNSPECIFIED;

    return PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolFS::close_file()
{
    return close_file_handle();
}

protocolError_t NetworkProtocolFS::close_dir()
{
    return close_dir_handle();
}

protocolError_t NetworkProtocolFS::read(unsigned short len)
{
    protocolError_t ret;

    is_write = false;

    switch (openMode)
    {
    case FILE:
        ret =  read_file(len);
        break;
    case DIR:
        ret = read_dir(len);
        break;
    default:
        ret = PROTOCOL_ERROR::UNSPECIFIED;
    }

    return ret;
}

protocolError_t NetworkProtocolFS::read_file(unsigned short len)
{
    std::vector<uint8_t> buf = std::vector<uint8_t>(len);

#ifdef VERBOSE_HTTP
    Debug_printf("NetworkProtocolFS::read_file(%u)\r\n", len);
#endif

    if (receiveBuffer->length() == 0)
    {
        // Do block read.
        if (read_file_handle(buf.data(), len) != PROTOCOL_ERROR::NONE)
        {
#ifdef VERBOSE_PROTOCOL
            Debug_printf("Nothing new from adapter, bailing.\n");
#endif
            return PROTOCOL_ERROR::UNSPECIFIED;
        }

        // Append to receive buffer.
        receiveBuffer->insert(receiveBuffer->end(), buf.begin(), buf.end());
        fileSize -= len;
    }
    else
        error = NDEV_STATUS::SUCCESS;

    // Pass back to base class for translation.
    return NetworkProtocol::read(len);
}

protocolError_t NetworkProtocolFS::read_dir(unsigned short len)
{
    protocolError_t ret;

    if (receiveBuffer->length() == 0)
    {
        *receiveBuffer = dirBuffer.substr(0, len);
        dirBuffer.erase(0, len);
        dirBuffer.shrink_to_fit();
    }

    ret = NetworkProtocol::read(len);

    return ret;
}

protocolError_t NetworkProtocolFS::write(unsigned short len)
{
    is_write = true;
    len = translate_transmit_buffer();
    return write_file(len); // Do more here? not sure.
}

protocolError_t NetworkProtocolFS::write_file(unsigned short len)
{
    if (write_file_handle((uint8_t *)transmitBuffer->data(), len) != PROTOCOL_ERROR::NONE)
        return PROTOCOL_ERROR::UNSPECIFIED;

    transmitBuffer->erase(0, len);
    return PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolFS::status(NetworkStatus *status)
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
        return PROTOCOL_ERROR::UNSPECIFIED;
    }
}

#define WAITING_CAP 65534

protocolError_t NetworkProtocolFS::status_file(NetworkStatus *status)
{
    unsigned int remaining;

    if (aux1_open == 8) {
        remaining = fileSize;
    }
    else {
        remaining = fileSize + receiveBuffer->length();
    }

    status->connected = remaining > 0 ? 1 : 0;
    if (is_write)
        status->error = NDEV_STATUS::SUCCESS;
    else
        status->error = remaining > 0 ? error : NDEV_STATUS::END_OF_FILE;

#if 0
    // This will reset the status->rxBytesWaiting that we just calculated above
    NetworkProtocol::status(status);
#endif

    return PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolFS::status_dir(NetworkStatus *status)
{
    status->connected = dirBuffer.length() > 0 ? 1 : 0;
    status->error = dirBuffer.length() > 0 ? error : NDEV_STATUS::END_OF_FILE;

    NetworkProtocol::status(status);

    return PROTOCOL_ERROR::NONE;
}

AtariSIODirection NetworkProtocolFS::special_inquiry(fujiCommandID_t cmd)
{
    AtariSIODirection ret;

    switch (cmd)
    {
    default:
        ret = SIO_DIRECTION_INVALID; // Not implemented.
    }

    return ret;
}

protocolError_t NetworkProtocolFS::special_00(cmdFrame_t *cmdFrame)
{
    switch (cmdFrame->comnd)
    {
    default:
        error = NDEV_STATUS::NOT_IMPLEMENTED;
        return PROTOCOL_ERROR::UNSPECIFIED;
    }
}

protocolError_t NetworkProtocolFS::special_40(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    switch (cmdFrame->comnd)
    {
    default:
        error = NDEV_STATUS::NOT_IMPLEMENTED;
        return PROTOCOL_ERROR::UNSPECIFIED;
    }
}

protocolError_t NetworkProtocolFS::special_80(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    switch (cmdFrame->comnd)
    {
    default:
        error = NDEV_STATUS::NOT_IMPLEMENTED;
        return PROTOCOL_ERROR::UNSPECIFIED;
    }
}

void NetworkProtocolFS::resolve()
{
#ifdef VERBOSE_PROTOCOL
    Debug_printf("NetworkProtocolFS::resolve(%s,%s,%s)\r\n", opened_url->path.c_str(), dir.c_str(), filename.c_str());
#endif

    if (stat() != PROTOCOL_ERROR::NONE)
    {
        // File wasn't found, let's try resolving against the crunched filename
        std::string crunched_filename = util_crunch(filename);

        char e[256]; // current entry.

        filename = "*"; // Temporarily reset filename to search for all files.

        if (open_dir_handle() != PROTOCOL_ERROR::NONE) // couldn't open dir, return path as is.
        {
            fserror_to_error();
            return;
        }

        while (read_dir_entry(e, 255) == PROTOCOL_ERROR::NONE)
        {
            std::string current_entry = std::string(e);
            std::string crunched_entry = util_crunch(current_entry);

#ifdef VERBOSE_PROTOCOL
            Debug_printf("current entry \"%s\" crunched entry \"%s\"\r\n", current_entry.c_str(), crunched_entry.c_str());
#endif

            if (crunched_filename == crunched_entry)
            {
                opened_url->path = dir + current_entry;
                stat(); // TODO: see if this assumption of success holds true in all cases?
                break;
            }
        }
        // We failed to resolve. clear, if we're reading, otherwise pass back original path.
        close_dir_handle();
    }

#ifdef VERBOSE_PROTOCOL
    Debug_printf("Resolved to %s\r\n", opened_url->url.c_str());
#endif

    // Clear file size, if resolved to write and not append.
    if (aux1_open == 8)
        fileSize = 0;

}

protocolError_t NetworkProtocolFS::perform_idempotent_80(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
#ifdef VERBOSE_PROTOCOL
    Debug_printf("NetworkProtocolFS::perform_idempotent_80, url: %s cmd: 0x%02X\r\n", url->url.c_str(), cmdFrame->comnd);
#endif
    switch (cmdFrame->comnd)
    {
    case NETCMD_RENAME:
        return rename(url, cmdFrame);
    case NETCMD_DELETE:
        return del(url, cmdFrame);
    case NETCMD_LOCK:
        return lock(url, cmdFrame);
    case NETCMD_UNLOCK:
        return unlock(url, cmdFrame);
    case NETCMD_MKDIR:
        return mkdir(url, cmdFrame);
    case NETCMD_RMDIR:
        return rmdir(url, cmdFrame);
    default:
#ifdef VERBOSE_PROTOCOL
        Debug_printf("Uncaught idempotent command: 0x%02X\r\n", cmdFrame->comnd);
#endif
        return PROTOCOL_ERROR::UNSPECIFIED;
    }
}

protocolError_t NetworkProtocolFS::rename(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    update_dir_filename(url);

    // Preprocessing routine to parse out comma position.

    size_t comma_pos = filename.find_first_of(",");

    // No comma found, return invalid devicespec error.
    if (comma_pos == std::string::npos)
    {
        error = NDEV_STATUS::INVALID_DEVICESPEC;
        return PROTOCOL_ERROR::UNSPECIFIED;
    }

    destFilename = dir + filename.substr(comma_pos + 1);
    filename = dir + filename.substr(0, comma_pos);

#ifdef VERBOSE_PROTOCOL
    Debug_printf("RENAME destfilename, %s, filename, %s\r\n", destFilename.c_str(), filename.c_str());
#endif

    return PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolFS::del(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    return PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolFS::mkdir(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    return PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolFS::rmdir(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    return PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolFS::lock(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    return PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolFS::unlock(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    return PROTOCOL_ERROR::NONE;
}

size_t NetworkProtocolFS::available()
{
    size_t avail;


    switch (openMode)
    {
    case FILE:
        if (aux1_open == 8)
            return 0;
        avail = std::min<size_t>(fileSize + receiveBuffer->length(), WAITING_CAP);
        break;
    case DIR:
        avail = receiveBuffer->length();
        if (!avail)
            avail = dirBuffer.length();
        break;
    default:
        avail = 0;
    }

    return avail;
}
