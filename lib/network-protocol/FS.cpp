/**
 * NetworkProtocolFS
 * 
 * Implementation
 */

#include "FS.h"

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

bool NetworkProtocolFS::open(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    // Call base class.
    NetworkProtocol::open(url, cmdFrame);
    fileSize = 0;

    update_dir_filename(opened_url);

    if (mount(url) == true)
        return true;

    if (cmdFrame->aux1 == 6 || cmdFrame->aux1 == 7)
    {
        return open_dir();
    }
    else
    {
        return open_file();
    }
}

bool NetworkProtocolFS::open_file()
{
    update_dir_filename(opened_url);

    if (aux1_open == 4 || aux1_open == 8)
        resolve();

    update_dir_filename(opened_url);

    openMode = FILE;

    if (opened_url->path.empty())
        return true;

    return open_file_handle();
}

bool NetworkProtocolFS::open_dir()
{
    openMode = DIR;
    dirBuffer.clear();
    dirBuffer.shrink_to_fit();
    update_dir_filename(opened_url);

    // assume everything if no filename.
    if (filename.empty())
        filename = "*";

    Debug_printf("NetworkProtocolFS::open_dir(%s)\r\n", opened_url->url.c_str());

    if (opened_url->path.empty())
    {
        return true;
    }

    if (open_dir_handle() == true)
    {
        fserror_to_error();
        return true;
    }

    std::vector<uint8_t> entryBuffer = std::vector<uint8_t>(ENTRY_BUFFER_SIZE);

    while (read_dir_entry((char *)entryBuffer.data(), ENTRY_BUFFER_SIZE - 1) == false)
    {
        if (aux2_open & 0x80)
        {
            // Long entry
            if (aux2_open == 0x81) // Apple2 80 col format.
                dirBuffer += util_long_entry_apple2_80col(std::string(entryBuffer.begin(), entryBuffer.end()), fileSize, is_directory) + lineEnding;
            else
                dirBuffer += util_long_entry(std::string(entryBuffer.begin(), entryBuffer.end()), fileSize, is_directory) + lineEnding;
        }
        else
        {
            // 8.3 entry
            dirBuffer += util_entry(util_crunch(std::string(entryBuffer.begin(), entryBuffer.end())), fileSize, is_directory, is_locked) + lineEnding;
        }
        fserror_to_error();

        // Clearing the buffer for reuse
        entryBuffer.clear();
        entryBuffer.resize(ENTRY_BUFFER_SIZE, 0); // fenrock was right.
    }

#ifdef BUILD_ATARI
    // Finally, drop a FREE SECTORS trailer.
    dirBuffer += "999+FREE SECTORS\x9b";
#endif /* BUILD_ATARI */

    if (error == NETWORK_ERROR_END_OF_FILE)
        error = NETWORK_ERROR_SUCCESS;

    // No need to free entryBuffer since it's managed by shared_ptr

    return error != NETWORK_ERROR_SUCCESS;
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

bool NetworkProtocolFS::close_file()
{
    return close_file_handle();
}

bool NetworkProtocolFS::close_dir()
{
    return close_dir_handle();
}

bool NetworkProtocolFS::read(unsigned short len)
{
    bool ret;

    switch (openMode)
    {
    case FILE:
        ret =  read_file(len);
        break;
    case DIR:
        ret = read_dir(len);
        break;
    default:
        ret = true;
    }

    return ret;
}

bool NetworkProtocolFS::read_file(unsigned short len)
{
    std::vector<uint8_t> buf = std::vector<uint8_t>(len);

    Debug_printf("NetworkProtocolFS::read_file(%u)\r\n", len);

    if (receiveBuffer->length() == 0)
    {
        // Do block read.
        if (read_file_handle(buf.data(), len) == true)
        {
            Debug_printf("Nothing new from adapter, bailing.\n");
            return true;
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

bool NetworkProtocolFS::read_dir(unsigned short len)
{
    bool ret;

    if (receiveBuffer->length() == 0)
    {
        *receiveBuffer = dirBuffer.substr(0, len);
        dirBuffer.erase(0, len);
        dirBuffer.shrink_to_fit();
    }

    ret = NetworkProtocol::read(len);

    return ret;
}

bool NetworkProtocolFS::write(unsigned short len)
{
    len = translate_transmit_buffer();
    return write_file(len); // Do more here? not sure.
}

bool NetworkProtocolFS::write_file(unsigned short len)
{
    if (write_file_handle((uint8_t *)transmitBuffer->data(), len) == true)
        return true;

    transmitBuffer->erase(0, len);
    return false;
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

bool NetworkProtocolFS::status_file(NetworkStatus *status)
{
    if (aux1_open == 8)
        status->rxBytesWaiting = 0;
    else
#ifdef BUILD_ATARI
        status->rxBytesWaiting = fileSize > 512 ? 512 : fileSize;
#else
        status->rxBytesWaiting = fileSize > 65534 ? 65534 : fileSize;
#endif

    status->connected = fileSize > 0 ? 1 : 0;
    status->error = fileSize > 0 ? error : NETWORK_ERROR_END_OF_FILE;

    NetworkProtocol::status(status);

    return false;
}

bool NetworkProtocolFS::status_dir(NetworkStatus *status)
{
    status->rxBytesWaiting = dirBuffer.length();
    status->connected = dirBuffer.length() > 0 ? 1 : 0;
    status->error = dirBuffer.length() > 0 ? error : NETWORK_ERROR_END_OF_FILE;

    NetworkProtocol::status(status);

    return false;
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

void NetworkProtocolFS::resolve()
{
    Debug_printf("NetworkProtocolFS::resolve(%s,%s,%s)\r\n", opened_url->path.c_str(), dir.c_str(), filename.c_str());

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

            Debug_printf("current entry \"%s\" crunched entry \"%s\"\r\n", current_entry.c_str(), crunched_entry.c_str());

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

    Debug_printf("Resolved to %s\r\n", opened_url->url.c_str());

    // Clear file size, if resolved to write and not append.
    if (aux1_open == 8)
        fileSize = 0;
    
}

bool NetworkProtocolFS::perform_idempotent_80(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    Debug_printf("NetworkProtocolFS::perform_idempotent_80, url: %s cmd: 0x%02X\r\n", url->url.c_str(), cmdFrame->comnd);
    switch (cmdFrame->comnd)
    {
    case 0x20:
        return rename(url, cmdFrame);
    case 0x21:
        return del(url, cmdFrame);
    case 0x23:
        return lock(url, cmdFrame);
    case 0x24:
        return unlock(url, cmdFrame);
    case 0x2A:
        return mkdir(url, cmdFrame);
    case 0x2B:
        return rmdir(url, cmdFrame);
    default:
        Debug_printf("Uncaught idempotent command: 0x%02X\r\n", cmdFrame->comnd);
        return true;
    }
}

bool NetworkProtocolFS::rename(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    update_dir_filename(url);

    // Preprocessing routine to parse out comma position.

    size_t comma_pos = filename.find_first_of(",");

    // No comma found, return invalid devicespec error.
    if (comma_pos == std::string::npos)
    {
        error = NETWORK_ERROR_INVALID_DEVICESPEC;
        return true;
    }

    destFilename = dir + filename.substr(comma_pos + 1);
    filename = dir + filename.substr(0, comma_pos);

    Debug_printf("RENAME destfilename, %s, filename, %s\r\n", destFilename.c_str(), filename.c_str());

    return false;
}

bool NetworkProtocolFS::del(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolFS::mkdir(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolFS::rmdir(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolFS::lock(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolFS::unlock(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    return false;
}
