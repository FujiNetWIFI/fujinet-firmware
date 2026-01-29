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

netProtoErr_t NetworkProtocolFS::open(PeoplesUrlParser *urlParser,
                                      netProtoOpenMode_t accessMode,
                                      netProtoTranslation_t translate)
{
    // Call base class.
    NetworkProtocol::open(urlParser, accessMode, translate);
    fileSize = 0;

    update_dir_filename(opened_url);

    if (mount(urlParser) == true)
        return NETPROTO_ERR_UNSPECIFIED;

    if (accessMode == NETPROTO_OPEN_DIRECTORY || accessMode == NETPROTO_OPEN_DIRECTORY_ALT)
    {
        return open_dir(translate);
    }

    return open_file(accessMode);
}

netProtoErr_t NetworkProtocolFS::open_file(netProtoOpenMode_t accessMode)
{
    update_dir_filename(opened_url);

    if (accessMode == NETPROTO_OPEN_READ || accessMode == NETPROTO_OPEN_WRITE)
        resolve();
    else
        stat();

    update_dir_filename(opened_url);

    openMode = OpenMode::FILE;

    if (opened_url->path.empty())
        return NETPROTO_ERR_UNSPECIFIED;

    return open_file_handle(accessMode);
}

netProtoErr_t NetworkProtocolFS::open_dir(netProtoTranslation_t a2mode)
{
    openMode = OpenMode::DIR;
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
        return NETPROTO_ERR_UNSPECIFIED;
    }

    if (open_dir_handle() == true)
    {
        fserror_to_error();
        return NETPROTO_ERR_UNSPECIFIED;
    }

    std::vector<uint8_t> entryBuffer(ENTRY_BUFFER_SIZE);

    while (read_dir_entry((char *)entryBuffer.data(), ENTRY_BUFFER_SIZE - 1) == false)
    {
        if (entryBuffer.at(0) == '.' || entryBuffer.at(0) == '/')
            continue;

        if (a2mode & NETPROTO_A2_FLAG)
        {
            // Long entry
            if (a2mode == NETPROTO_A2_80COL) // Apple2 80 col format.
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

    if (error == NETWORK_ERROR_END_OF_FILE)
        error = NETWORK_ERROR_SUCCESS;

    return error == NETWORK_ERROR_SUCCESS ? NETPROTO_ERR_NONE : NETPROTO_ERR_UNSPECIFIED;
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

netProtoErr_t NetworkProtocolFS::close()
{
    bool file_closed = false;
    // call base class.
    NetworkProtocol::close();

    switch (openMode)
    {
    case OpenMode::FILE:
        file_closed = close_file();
        break;
    case OpenMode::DIR:
        file_closed = close_dir();
        break;
    default:
        file_closed = false;
    }

    if (file_closed == false)
        fserror_to_error();

    if (umount() == true)
        return NETPROTO_ERR_UNSPECIFIED;

    return NETPROTO_ERR_NONE;
}

netProtoErr_t NetworkProtocolFS::close_file()
{
    return close_file_handle();
}

netProtoErr_t NetworkProtocolFS::close_dir()
{
    return close_dir_handle();
}

netProtoErr_t NetworkProtocolFS::read(unsigned short len)
{
    netProtoErr_t ret;

    is_write = false;

    switch (openMode)
    {
    case OpenMode::FILE:
        ret =  read_file(len);
        break;
    case OpenMode::DIR:
        ret = read_dir(len);
        break;
    default:
        ret = NETPROTO_ERR_UNSPECIFIED;
    }

    return ret;
}

netProtoErr_t NetworkProtocolFS::read_file(unsigned short len)
{
    std::vector<uint8_t> buf = std::vector<uint8_t>(len);

#ifdef VERBOSE_HTTP
    Debug_printf("NetworkProtocolFS::read_file(%u)\r\n", len);
#endif

    if (receiveBuffer->length() == 0)
    {
        // Do block read.
        if (read_file_handle(buf.data(), len) == true)
        {
#ifdef VERBOSE_PROTOCOL
            Debug_printf("Nothing new from adapter, bailing.\n");
#endif
            return NETPROTO_ERR_UNSPECIFIED;
        }

        // Append to receive buffer.
        receiveBuffer->insert(receiveBuffer->end(), buf.begin(), buf.end());
        fileSize -= len;
    }
    else
        error = NETWORK_ERROR_SUCCESS;

    // Pass back to base class for translation.
    return NetworkProtocol::read(len);
}

netProtoErr_t NetworkProtocolFS::read_dir(unsigned short len)
{
    netProtoErr_t ret;

    if (receiveBuffer->length() == 0)
    {
        *receiveBuffer = dirBuffer.substr(0, len);
        dirBuffer.erase(0, len);
        dirBuffer.shrink_to_fit();
    }

    ret = NetworkProtocol::read(len);

    return ret;
}

netProtoErr_t NetworkProtocolFS::write(unsigned short len)
{
    is_write = true;
    len = translate_transmit_buffer();
    return write_file(len); // Do more here? not sure.
}

netProtoErr_t NetworkProtocolFS::write_file(unsigned short len)
{
    if (write_file_handle((uint8_t *)transmitBuffer->data(), len) == true)
        return NETPROTO_ERR_UNSPECIFIED;

    transmitBuffer->erase(0, len);
    return NETPROTO_ERR_NONE;
}

netProtoErr_t NetworkProtocolFS::status(NetworkStatus *status)
{
    switch (openMode)
    {
    case OpenMode::FILE:
        return status_file(status);
        break;
    case OpenMode::DIR:
        return status_dir(status);
        break;
    default:
        return NETPROTO_ERR_UNSPECIFIED;
    }
}

#ifdef BUILD_ATARI
#define WAITING_CAP 512
#else
#define WAITING_CAP 65534
#endif

netProtoErr_t NetworkProtocolFS::status_file(NetworkStatus *status)
{
    unsigned int remaining;

    if (opened_write) {
        remaining = fileSize;
    }
    else {
        remaining = fileSize + receiveBuffer->length();
    }

    status->connected = remaining > 0 ? 1 : 0;
    if (is_write)
        status->error = 1;
    else
        status->error = remaining > 0 ? error : NETWORK_ERROR_END_OF_FILE;

#if 0
    // This will reset the status->rxBytesWaiting that we just calculated above
    NetworkProtocol::status(status);
#endif

    return NETPROTO_ERR_NONE;
}

netProtoErr_t NetworkProtocolFS::status_dir(NetworkStatus *status)
{
    status->connected = dirBuffer.length() > 0 ? 1 : 0;
    status->error = dirBuffer.length() > 0 ? error : NETWORK_ERROR_END_OF_FILE;

    NetworkProtocol::status(status);

    return NETPROTO_ERR_NONE;
}

void NetworkProtocolFS::resolve()
{
#ifdef VERBOSE_PROTOCOL
    Debug_printf("NetworkProtocolFS::resolve(%s,%s,%s)\r\n", opened_url->path.c_str(), dir.c_str(), filename.c_str());
#endif

    if (stat() == true) // true = error.
    {
        // File wasn't found, let's try resolving against the crunched filename
        std::string crunched_filename = util_crunch(filename);

        char e[256]; // current entry.

        filename = "*"; // Temporarily reset filename to search for all files.

        if (open_dir_handle() == true) // couldn't open dir, return path as is.
        {
            fserror_to_error();
            return;
        }

        while (read_dir_entry(e, 255) == false)
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
    if (opened_write)
        fileSize = 0;
}

netProtoErr_t NetworkProtocolFS::perform_idempotent_80(PeoplesUrlParser *url, fujiCommandID_t cmd)
{
#ifdef VERBOSE_PROTOCOL
    Debug_printf("NetworkProtocolFS::perform_idempotent_80, url: %s cmd: 0x%02X\r\n", url->url.c_str(), cmd);
#endif
    switch (cmd)
    {
    case FUJICMD_RENAME:
        return rename(url);
    case FUJICMD_DELETE:
        return del(url);
    case FUJICMD_LOCK:
        return lock(url);
    case FUJICMD_UNLOCK:
        return unlock(url);
    case FUJICMD_MKDIR:
        return mkdir(url);
    case FUJICMD_RMDIR:
        return rmdir(url);
    default:
#ifdef VERBOSE_PROTOCOL
        Debug_printf("Uncaught idempotent command: 0x%02X\r\n", cmd);
#endif
        return NETPROTO_ERR_UNSPECIFIED;
    }
}

netProtoErr_t NetworkProtocolFS::rename(PeoplesUrlParser *url)
{
    update_dir_filename(url);

    // Preprocessing routine to parse out comma position.

    size_t comma_pos = filename.find_first_of(",");

    // No comma found, return invalid devicespec error.
    if (comma_pos == std::string::npos)
    {
        error = NETWORK_ERROR_INVALID_DEVICESPEC;
        return NETPROTO_ERR_UNSPECIFIED;
    }

    destFilename = dir + filename.substr(comma_pos + 1);
    filename = dir + filename.substr(0, comma_pos);

#ifdef VERBOSE_PROTOCOL
    Debug_printf("RENAME destfilename, %s, filename, %s\r\n", destFilename.c_str(), filename.c_str());
#endif

    return NETPROTO_ERR_NONE;
}

size_t NetworkProtocolFS::available()
{
    size_t avail;


    switch (openMode)
    {
    case OpenMode::FILE:
#if 0
        if (aux1_open == NETPROTO_OPEN_WRITE)
            return 0;
#endif
        avail = std::min<size_t>(fileSize + receiveBuffer->length(), WAITING_CAP);
        break;
    case OpenMode::DIR:
        avail = receiveBuffer->length();
        if (!avail)
            avail = dirBuffer.length();
        break;
    default:
        avail = 0;
    }

    return avail;
}
