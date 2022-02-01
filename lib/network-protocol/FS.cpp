/**
 * NetworkProtocolFS
 * 
 * Implementation
 */

#include "../../include/debug.h"

#include "FS.h"
#include "status_error_codes.h"
#include "utils.h"

NetworkProtocolFS::NetworkProtocolFS(string *rx_buf, string *tx_buf, string *sp_buf)
    : NetworkProtocol(rx_buf, tx_buf, sp_buf)
{
    fileSize = 0;
}

NetworkProtocolFS::~NetworkProtocolFS()
{
}

bool NetworkProtocolFS::open(EdUrlParser *url, cmdFrame_t *cmdFrame)
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
    char *entryBuffer = (char *)malloc(256);
    openMode = DIR;
    dirBuffer.clear();
    update_dir_filename(opened_url);

    // assume everything if no filename.
    if (filename.empty())
        filename = "*";

    Debug_printf("NetworkProtocolFS::open_dir(%s)\n", opened_url->toString().c_str());

    if (opened_url->path.empty())
        return true;

    if (open_dir_handle() == true)
    {
        fserror_to_error();
        return true;
    }

    while (read_dir_entry(entryBuffer, 255) == false)
    {
        if (aux2_open & 0x80)
        {
            // Long entry
            dirBuffer += util_long_entry(string(entryBuffer), fileSize, is_directory) + "\x9b";
        }
        else
        {
            // 8.3 entry
            dirBuffer += util_entry(util_crunch(string(entryBuffer)), fileSize, is_directory, is_locked) + "\x9b";
        }
        fserror_to_error();
    }

    // Finally, drop a FREE SECTORS trailer.
    dirBuffer += "999+FREE SECTORS\x9b";

    if (error == NETWORK_ERROR_END_OF_FILE)
        error = NETWORK_ERROR_SUCCESS;

    free(entryBuffer);

    return error != NETWORK_ERROR_SUCCESS;
}

void NetworkProtocolFS::update_dir_filename(EdUrlParser *url)
{
    size_t found = url->path.find_last_of("/");

    dir = url->path.substr(0, found + 1);
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

bool NetworkProtocolFS::read_file(unsigned short len)
{
    uint8_t *buf = (uint8_t *)malloc(len);

    Debug_printf("NetworkProtocolFS::read_file(%u)\n", len);

    if (buf == nullptr)
    {
        Debug_printf("NetworkProtocolTNFS:read_file(%u) could not allocate.\n", len);
        return true; // error
    }

    if (receiveBuffer->length() == 0)
    {
        // Do block read.
        if (read_file_handle(buf, len) == true)
            return true;

        // Append to receive buffer.
        *receiveBuffer += string((char *)buf, len);
        fileSize -= len;
    }
    else
        error = NETWORK_ERROR_SUCCESS;

    // Done with the temporary buffer.
    free(buf);

    // Pass back to base class for translation.
    return NetworkProtocol::read(len);
}

bool NetworkProtocolFS::read_dir(unsigned short len)
{
    if (receiveBuffer->length() == 0)
    {
        *receiveBuffer = dirBuffer.substr(0, len);
        dirBuffer.erase(0, len);
    }

    return NetworkProtocol::read(len);
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
        status->rxBytesWaiting = fileSize > 512 ? 512 : fileSize;
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
    Debug_printf("NetworkProtocolFS::resolve(%s,%s,%s)\n", opened_url->path.c_str(), dir.c_str(), filename.c_str());

    if (stat() == true) // true = error.
    {
        // File wasn't found, let's try resolving against the crunched filename
        string crunched_filename = util_crunch(filename);

        char e[256]; // current entry.

        filename = "*"; // Temporarily reset filename to search for all files.

        if (open_dir_handle() == true) // couldn't open dir, return path as is.
        {
            fserror_to_error();
            return;
        }

        while (read_dir_entry(e, 255) == false)
        {
            string current_entry = string(e);
            string crunched_entry = util_crunch(current_entry);

            Debug_printf("current entry \"%s\" crunched entry \"%s\"\n", current_entry.c_str(), crunched_entry.c_str());

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

    Debug_printf("Resolved to %s\n", opened_url->toString().c_str());

    // Clear file size, if resolved to write and not append.
    if (aux1_open == 8)
        fileSize = 0;
    
}

bool NetworkProtocolFS::perform_idempotent_80(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
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
        Debug_printf("Uncaught idempotent command: %u\n", cmdFrame->comnd);
        return true;
    }
}

bool NetworkProtocolFS::rename(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    update_dir_filename(url);

    // Preprocessing routine to parse out comma position.

    size_t comma_pos = filename.find_first_of(",");

    // No comma found, return invalid devicespec error.
    if (comma_pos == string::npos)
    {
        error = NETWORK_ERROR_INVALID_DEVICESPEC;
        return true;
    }

    destFilename = dir + filename.substr(comma_pos + 1);
    filename = dir + filename.substr(0, comma_pos);

    Debug_printf("RENAME destfilename, %s, filename, %s\n", destFilename.c_str(), filename.c_str());

    return false;
}

bool NetworkProtocolFS::del(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolFS::mkdir(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolFS::rmdir(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolFS::lock(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolFS::unlock(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return false;
}
