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

    return false;
}

bool NetworkProtocolTNFS::open_dir(string path)
{
    NetworkProtocolFS::open_dir(path);
    error = NETWORK_ERROR_NOT_IMPLEMENTED;
    return true;
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
}