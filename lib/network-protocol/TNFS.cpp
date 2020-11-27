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
    tnfs_error = tnfs_open(&mountInfo, filename.c_str(), mode, perms, &fd);
    fserror_to_error();

    return tnfs_error != TNFS_RESULT_SUCCESS;
}

bool NetworkProtocolTNFS::open_dir(string path)
{
    NetworkProtocolFS::open_dir(path);

    if (path.empty())
        return true;

    tnfs_error = tnfs_opendirx(&mountInfo, dir.c_str(), 0, 0, filename.c_str(), 0);

    return tnfs_error != TNFS_RESULT_SUCCESS;
}

bool NetworkProtocolTNFS::mount(string hostName, string path)
{
    strcpy(mountInfo.hostname, hostName.c_str());
    strcpy(mountInfo.mountpath, path.c_str());

    tnfs_error = tnfs_mount(&mountInfo);

    return tnfs_error != TNFS_RESULT_SUCCESS;
}

bool NetworkProtocolTNFS::umount()
{
    tnfs_error = tnfs_umount(&mountInfo);

    return tnfs_error != TNFS_RESULT_SUCCESS;
}

void NetworkProtocolTNFS::fserror_to_error()
{
    switch (tnfs_error)
    {
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
    tnfsStat fs;

    if (tnfs_stat(&mountInfo, &fs, path.c_str()))
    {
        // File wasn't found, let's try resolving against the crunched filename
        string crunched_filename = util_crunch(filename);
        char e[256]; // current entry.

        if (tnfs_opendirx(&mountInfo, dir.c_str(), 0, 0, "*", 0) != 0)
            return "";

        while (tnfs_readdirx(&mountInfo, &fs, e, 255) == 0)
        {
            string current_entry = string(e);
            string crunched_entry = util_crunch(current_entry);

            if (crunched_filename == crunched_entry)
            {
                tnfs_closedir(&mountInfo);
                return dir + "/" + current_entry;
            }
        }
        // We failed to resolve. clear, if we're reading, otherwise pass back original path.
        tnfs_closedir(&mountInfo);
        return (aux1_open == 4 ? "" : path);
    }

    return path;
}

bool NetworkProtocolTNFS::read_file(unsigned short len)
{
    uint8_t* buf = (uint8_t *)malloc(len);
    uint16_t actual_len;

    tnfs_error = tnfs_read(&mountInfo, fd, buf, len, &actual_len);
    fserror_to_error();

    // Append to receive buffer.
    *receiveBuffer += string((char *)buf,len);

    // Done with the temporary buffer.
    free(buf);
    
    // Pass back to base class for translation.
    return NetworkProtocol::read(len);
}

bool NetworkProtocolTNFS::read_dir(unsigned short len)
{
    return tnfs_error != TNFS_RESULT_SUCCESS;
}