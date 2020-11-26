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

}

bool NetworkProtocolTNFS::open_dir(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    error = NETWORK_ERROR_NOT_IMPLEMENTED;
    return true;
}

bool NetworkProtocolTNFS::mount(string hostName, string path)
{
    memset(&mountInfo,0,sizeof(mountInfo));
    strcpy(mountInfo.hostname,hostName.c_str());
    strcpy(mountInfo.mountpath,path.c_str());

    tnfs_error = tnfs_mount(&mountInfo);

    return tnfs_error != TNFS_RESULT_SUCCESS;
}

bool NetworkProtocolTNFS::umount()
{
    tnfs_error = tnfs_umount(&mountInfo);

    return tnfs_error != TNFS_RESULT_SUCCESS;
}