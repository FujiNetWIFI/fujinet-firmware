/**
 * NetworkProtocolTNFS
 * 
 * Implementation
 */

#include "TNFS.h"
#include "status_error_codes.h"

NetworkProtocolTNFS::NetworkProtocolTNFS(string *rx_buf, string *tx_buf, string *sp_buf)
    : NetworkProtocolFS(rx_buf, tx_buf, sp_buf)
{
}

NetworkProtocolTNFS::~NetworkProtocolTNFS()
{
}

bool NetworkProtocolTNFS::open_file(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    // Ask base class to resolve file.
    if (NetworkProtocolFS::open_file(url, cmdFrame)==true)
        return true;

    return true;
}

bool NetworkProtocolTNFS::open_dir(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    NetworkProtocolFS::open_dir(url, cmdFrame);
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
        Debug_printf("TNFS uncaught error: %u\n",tnfs_error);
        error = NETWORK_ERROR_GENERAL;
    }
}