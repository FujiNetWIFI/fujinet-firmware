/**
 * NetworkProtocolTNFS
 * 
 * Implementation
 */

#include "TNFS.h"
#include "status_error_codes.h"
#include "utils.h"

NetworkProtocolTNFS::NetworkProtocolTNFS(string *rx_buf, string *tx_buf, string *sp_buf)
    : NetworkProtocolFS(rx_buf, tx_buf, sp_buf)
{
}

NetworkProtocolTNFS::~NetworkProtocolTNFS()
{
}

bool NetworkProtocolTNFS::open_file(string path)
{
    Debug_printf("NetworkProtocolTNFS::open_file(%s)\n", path.c_str());
    NetworkProtocolFS::open_file(path);

    if (path.empty())
        return true;

    // Map aux1 to mode and perms for tnfs_open()
    switch (aux1_open)
    {
    case 4:
        mode = 1;
        perms = 0;
        break;
    case 8:
    case 9:
        mode = 0x010B;
        perms = 0x1FF;
        break;
    case 12:
        mode = 0x103;
        perms = 0x1FF;
        break;
    }

    // Do the open.
    tnfs_error = tnfs_open(&mountInfo, path.c_str(), mode, perms, &fd);
    Debug_printf("tnfs_error: %u\n",tnfs_error);
    fserror_to_error();

    return tnfs_error != TNFS_RESULT_SUCCESS;
}

bool NetworkProtocolTNFS::open_dir(string path)
{
    char e[256];

    Debug_printf("NetworkProtocolTNFS::open_dir(%s)\n", path.c_str());
    NetworkProtocolFS::open_dir(path); // also clears directory buffer

    if (path.empty())
        return true;

res    tnfs_error = tnfs_opendirx(&mountInfo, dir.c_str(), 0, 0, filename.c_str(), 0);
    while (tnfs_readdirx(&mountInfo, &fileStat, e, 255) == 0)
    {
        if (aux2_open & 0x80)
        {
            // Long entry
            dirBuffer += util_long_entry(string(e), fileStat.filesize) + "\x9b";
        }
        else
        {
            // 8.3 entry
            dirBuffer += util_entry(util_crunch(string(e)), fileStat.filesize) + "\x9b";
        }
    }

    // Finally, drop a FREE SECTORS trailer.
    dirBuffer += "999+FREE SECTORS\x9b";

    return tnfs_error != TNFS_RESULT_SUCCESS;
}

bool NetworkProtocolTNFS::mount(string hostName, string path)
{
    Debug_printf("NetworkProtocolTNFS::mount(%s,%s)\n", hostName.c_str(), path.c_str());
    strcpy(mountInfo.hostname, hostName.c_str());
    strcpy(mountInfo.mountpath, "/");

    tnfs_error = tnfs_mount(&mountInfo);
    fserror_to_error();

    return tnfs_error != TNFS_RESULT_SUCCESS;
}

bool NetworkProtocolTNFS::umount()
{
    Debug_printf("NetworkProtocolTNFS::umount()\n");
    tnfs_error = tnfs_umount(&mountInfo);

    return tnfs_error != TNFS_RESULT_SUCCESS;
}

void NetworkProtocolTNFS::fserror_to_error()
{
    switch (tnfs_error)
    {
    case -1: // special case for mount
        error = NETWORK_ERROR_GENERAL_TIMEOUT;
        break;
    case TNFS_RESULT_SUCCESS:
        error = NETWORK_ERROR_SUCCESS;
        break;
    case TNFS_RESULT_FILE_NOT_FOUND:
        error = NETWORK_ERROR_FILE_NOT_FOUND;
        break;
    case TNFS_RESULT_READONLY_FILESYSTEM:
    case TNFS_RESULT_ACCESS_DENIED:
        error = NETWORK_ERROR_ACCESS_DENIED;
        break;
    case TNFS_RESULT_NO_SPACE_ON_DEVICE:
        error = NETWORK_ERROR_NO_SPACE_ON_DEVICE;
        break;
    case TNFS_RESULT_END_OF_FILE:
        error = NETWORK_ERROR_END_OF_FILE;
        break;
    default:
        Debug_printf("TNFS uncaught error: %u\n", tnfs_error);
        error = NETWORK_ERROR_GENERAL;
    }
}

string NetworkProtocolTNFS::resolve(string path)
{
    Debug_printf("NetworkProtocolTNFS::resolve(%s,%s,%s)\n", path.c_str(),dir.c_str(),filename.c_str());

    if (tnfs_stat(&mountInfo, &fileStat, path.c_str()))
    {
        // File wasn't found, let's try resolving against the crunched filename
        string crunched_filename = util_crunch(filename);

        Debug_printf("XXX Crunched filename: '%s'\n",crunched_filename.c_str());
        char e[256]; // current entry.

        tnfs_opendirx(&mountInfo, dir.c_str(), 0, 0, "*", 0);

        while (tnfs_readdirx(&mountInfo, &fileStat, e, 255) == 0)
        {
            string current_entry = string(e);
            string crunched_entry = util_crunch(current_entry);

            Debug_printf("current entry: %s, crunched entry: %s, crunched filename: %s",current_entry.c_str(),crunched_entry.c_str(),crunched_filename.c_str());

            if (crunched_filename == crunched_entry)
            {
                path = dir + current_entry;
                tnfs_stat(&mountInfo, &fileStat, path.c_str()); // get stat of resolved file.
                break;
            }
        }
        // We failed to resolve. clear, if we're reading, otherwise pass back original path.
        tnfs_closedir(&mountInfo);
    }

    Debug_printf("Resolved to %s\n", path.c_str());
    return path;
}

bool NetworkProtocolTNFS::read_file(unsigned short len)
{
    uint8_t *buf = (uint8_t *)malloc(len);

    Debug_printf("NetworkProtocolTNFS::read_file(%u)\n", len);

    if (buf == nullptr)
    {
        Debug_printf("NetworkProtocolTNFS:read_file(%u) could not allocate.\n", len);
        return true; // error
    }

    if (receiveBuffer->length() == 0)
    {
        // Do block read.
        if (block_read(buf, len) == true)
            return true;

        // Append to receive buffer.
        *receiveBuffer += string((char *)buf, len);
        fileStat.filesize -= len;
        fserror_to_error();
    }
    else
    {
        tnfs_error = TNFS_RESULT_SUCCESS;
        fserror_to_error();
    }

    // Done with the temporary buffer.
    free(buf);

    // Pass back to base class for translation.
    return NetworkProtocol::read(len);
}

bool NetworkProtocolTNFS::read_dir(unsigned short len)
{
    if (receiveBuffer->length()==0)
    {
        *receiveBuffer = dirBuffer.substr(0,len);
        dirBuffer.erase(0,len);
    }

    return NetworkProtocol::read(len);
}

bool NetworkProtocolTNFS::status_file(NetworkStatus *status)
{
    status->rxBytesWaiting = fileStat.filesize > 65535 ? 65535 : fileStat.filesize;
    status->connected = fileStat.filesize > 0 ? 1 : 0;
    status->error = fileStat.filesize > 0 ? error : NETWORK_ERROR_END_OF_FILE;
    return false;
}

bool NetworkProtocolTNFS::status_dir(NetworkStatus *status)
{
    status->rxBytesWaiting = dirBuffer.length();
    status->connected = dirBuffer.length() > 0 ? 1 : 0;
    status->error = dirBuffer.length() > 0 ? error : NETWORK_ERROR_END_OF_FILE;
    return false;
}

bool NetworkProtocolTNFS::close_file()
{
    Debug_printf("NetworkProtocolTNFS::close_file(%u)\n", fd);
    if (fd != 0)
        tnfs_error = tnfs_close(&mountInfo, fd);
    fserror_to_error();
    return tnfs_error != TNFS_RESULT_SUCCESS;
}

bool NetworkProtocolTNFS::close_dir()
{
    Debug_printf("NetworkProtocolTNFS::close_dir()\n");
    tnfs_error = tnfs_closedir(&mountInfo);
    fserror_to_error();
    return tnfs_error != TNFS_RESULT_SUCCESS;
}

bool NetworkProtocolTNFS::block_read(uint8_t *buf, unsigned short len)
{
    unsigned short total_len = len;
    unsigned short block_len = TNFS_MAX_READWRITE_PAYLOAD;
    uint16_t actual_len;

    while (total_len > 0)
    {
        if (total_len > TNFS_MAX_READWRITE_PAYLOAD)
            block_len = TNFS_MAX_READWRITE_PAYLOAD;
        else
            block_len = total_len;

        tnfs_error = tnfs_read(&mountInfo, fd, buf, block_len, &actual_len);
        if (tnfs_error != 0)
        {
            fserror_to_error();
            return true; // error.
        }
        else
        {
            buf += block_len;
            total_len -= block_len;
        }
    }

    fserror_to_error();
    return false; // no error
}

bool NetworkProtocolTNFS::block_write(uint8_t *buf, unsigned short len)
{
    unsigned short total_len = len;
    unsigned short block_len = TNFS_MAX_READWRITE_PAYLOAD;
    uint16_t actual_len;

    while (total_len > 0)
    {
        if (total_len > TNFS_MAX_READWRITE_PAYLOAD)
            block_len = TNFS_MAX_READWRITE_PAYLOAD;
        else
            block_len = total_len;

        if (tnfs_write(&mountInfo, fd, buf, block_len, &actual_len) != 0)
        {
            return true; // error.
        }
        else
        {
            buf += block_len;
            total_len -= block_len;
        }
    }
    return false; // no error
}

bool NetworkProtocolTNFS::write_file(unsigned short len)
{
    if (block_write((uint8_t *)transmitBuffer->data(), len) == true)
        return true;

    transmitBuffer->erase(0, len);
    return false;
}

uint8_t NetworkProtocolTNFS::special_inquiry(uint8_t cmd)
{
    uint8_t ret;

    switch (cmd)
    {
    case 0x20:      // RENAME
    case 0x21:      // DELETE
    case 0x2A:      // MKDIR
    case 0x2B:      // RMDIR
        ret = 0x80; // Atari to peripheral.
        break;
    default:
        return NetworkProtocolFS::special_inquiry(cmd);
    }

    return ret;
}

bool NetworkProtocolTNFS::special_00(cmdFrame_t *cmdFrame)
{
    switch (cmdFrame->comnd)
    {
    default:
        return NetworkProtocolFS::special_00(cmdFrame);
    }
}

bool NetworkProtocolTNFS::special_40(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    switch (cmdFrame->comnd)
    {
    default:
        return NetworkProtocolFS::special_40(sp_buf, len, cmdFrame);
    }
}

bool NetworkProtocolTNFS::special_80(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    switch (cmdFrame->comnd)
    {
    case 0x20: // RENAME
        return rename(sp_buf, len);
    case 0x21: // DELETE
        return del(sp_buf, len);
    case 0x2A: // MKDIR
        return mkdir(sp_buf, len);
    case 0x2B: // RMDIR
        return rmdir(sp_buf, len);
    default:
        return NetworkProtocolFS::special_80(sp_buf, len, cmdFrame);
    }
}

bool NetworkProtocolTNFS::rename(uint8_t *sp_buf, unsigned short len)
{
    if (NetworkProtocolFS::rename(sp_buf, len) == true)
        return true;

    tnfs_error = tnfs_rename(&mountInfo, filename.c_str(), destFilename.c_str());
    fserror_to_error();

    return true;
}

bool NetworkProtocolTNFS::del(uint8_t *sp_buf, unsigned short len)
{
    return true;
}

bool NetworkProtocolTNFS::mkdir(uint8_t *sp_buf, unsigned short len)
{
    return true;
}

bool NetworkProtocolTNFS::rmdir(uint8_t *sp_buf, unsigned short len)
{
    return true;
}