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

protocolError_t NetworkProtocolFS::open(PeoplesUrlParser *urlParser,
                                        fileAccessMode_t access,
                                        netProtoTranslation_t translate)
{
    // Call base class.
    NetworkProtocol::open(urlParser, access, translate);
    fileSize = 0;
    streamMode = access;

    update_dir_filename(opened_url);

    if (mount(urlParser) != PROTOCOL_ERROR::NONE)
        return PROTOCOL_ERROR::UNSPECIFIED;

    if (access == ACCESS_MODE::DIRECTORY || access == ACCESS_MODE::DIRECTORY_ALT)
        return open_dir((apple2Flag_t) translate);

    return open_file();
}

protocolError_t NetworkProtocolFS::open_file()
{
    update_dir_filename(opened_url);

    if (streamMode == ACCESS_MODE::READ || streamMode == ACCESS_MODE::WRITE)
        resolve();
    else
        stat();

    update_dir_filename(opened_url);

    streamType = streamType_t::FILE;

    if (opened_url->path.empty())
        return PROTOCOL_ERROR::UNSPECIFIED;

    return open_file_handle();
}

protocolError_t NetworkProtocolFS::open_dir(apple2Flag_t a2flags)
{
    streamType = streamType_t::DIR;
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

        if (a2flags >= APPLE2_FLAG::IS_A2)
        {
            // Long entry
            if (a2flags == APPLE2_FLAG::IS_80COL) // Apple2 80 col format.
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

void NetworkProtocolFS::set_open_params(fileAccessMode_t access, netProtoTranslation_t translate)
{
    streamMode = (fileAccessMode_t) access;
    translation_mode = (netProtoTranslation_t) (translate & 0x7F);
#ifdef VERBOSE_PROTOCOL
    Debug_printf("Changed open params to streamMode = %d, a2flags = %d. Set translation_mode to %d\r\n", p1, p2, translation_mode);
#endif
}

protocolError_t NetworkProtocolFS::close()
{
    protocolError_t err;
    // call base class.
    NetworkProtocol::close();

    switch (streamType)
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

    was_write = false;

    switch (streamType)
    {
    case streamType_t::FILE:
        ret =  read_file(len);
        break;
    case streamType_t::DIR:
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
    was_write = true;
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
    switch (streamType)
    {
    case streamType_t::FILE:
        return status_file(status);
        break;
    case streamType_t::DIR:
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

    if (streamMode == ACCESS_MODE::WRITE) {
        remaining = fileSize;
    }
    else {
        remaining = fileSize + receiveBuffer->length();
    }

    status->connected = remaining > 0 ? 1 : 0;
    if (was_write)
        status->error = NDEV_STATUS::SUCCESS;
    else
        status->error = remaining > 0 ? error : NDEV_STATUS::END_OF_FILE;

    return PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolFS::status_dir(NetworkStatus *status)
{
    status->connected = dirBuffer.length() > 0 ? 1 : 0;
    status->error = dirBuffer.length() > 0 ? error : NDEV_STATUS::END_OF_FILE;

    NetworkProtocol::status(status);

    return PROTOCOL_ERROR::NONE;
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
    if (streamMode == ACCESS_MODE::WRITE)
        fileSize = 0;
}

protocolError_t NetworkProtocolFS::rename(PeoplesUrlParser *url)
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

size_t NetworkProtocolFS::available()
{
    size_t avail;


    switch (streamType)
    {
    case streamType_t::FILE:
        if (streamMode == ACCESS_MODE::WRITE)
            return 0;
        avail = std::min<size_t>(fileSize + receiveBuffer->length(), WAITING_CAP);
        break;
    case streamType_t::DIR:
        avail = receiveBuffer->length();
        if (!avail)
            avail = dirBuffer.length();
        break;
    default:
        avail = 0;
    }

    return avail;
}
