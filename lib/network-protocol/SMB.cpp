/**
 * NetworkProtocolSMB
 * 
 * Implementation
 */

#include "SMB.h"
#include "status_error_codes.h"
#include "utils.h"
#include <fcntl.h>
#include <string.h>

NetworkProtocolSMB::NetworkProtocolSMB(string *rx_buf, string *tx_buf, string *sp_buf)
    : NetworkProtocolFS(rx_buf, tx_buf, sp_buf)
{
    rename_implemented = true;
    delete_implemented = true;
    mkdir_implemented = true;
    rmdir_implemented = true;
    Debug_printf("NetworkProtocolSMB::ctor\n");
    smb = smb2_init_context();
}

NetworkProtocolSMB::~NetworkProtocolSMB()
{
    Debug_printf("NetworkProtocolSMB::dtor\n");
    smb2_destroy_context(smb);
}

bool NetworkProtocolSMB::open_file_handle()
{
    if (smb == nullptr)
    {
        Debug_printf("NetworkProtocolSMB::open_file_handle() - no smb context. aborting.\n");
        return true;
    }

    // Determine flags
    int flags = 0;

    switch (aux1_open)
    {
    case 4:
        flags |= O_RDONLY;
        break;
    case 8:
        flags |= O_WRONLY | O_CREAT;
        break;
    case 9:
        flags |= O_APPEND | O_CREAT;
        break;
    case 12:
        flags |= O_RDWR;
        break;
    default:
        Debug_printf("NetworkProtocolSMB::open_file_handle() - Uncaught aux1 %d", aux1_open);
    }

    if (flags == 0)
    {
        fserror_to_error();
        return true;
    }

    fh = smb2_open(smb, smb_url->path, flags);

    if (fh == nullptr)
    {
        fserror_to_error();
        return true;
    }

    offset = 0;

    return false;
}

bool NetworkProtocolSMB::open_dir_handle()
{
    if ((smb_dir = smb2_opendir(smb, smb_url->path)) == nullptr)
    {
        fserror_to_error();
        return true;
    }

    return false;
}

bool NetworkProtocolSMB::mount(EdUrlParser *url)
{
    // use mRawURL to bypass our normal URL processing.
    smb_url = smb2_parse_url(smb, url->mRawUrl.c_str());

    if ((smb_error = smb2_connect_share(smb, smb_url->server, smb_url->share, smb_url->user)) != 0)
    {
        fserror_to_error();
        return true;
    }

    return false;
}

bool NetworkProtocolSMB::umount()
{
    if (smb == nullptr)
        return true;

    smb2_disconnect_share(smb);

    if (smb_url == nullptr)
        return true;

    smb2_destroy_url(smb_url);
    return false;
}

void NetworkProtocolSMB::fserror_to_error()
{
    switch (smb_error)
    {
    default:
        error = NETWORK_ERROR_GENERAL;
        break;
    }
}

bool NetworkProtocolSMB::read_file_handle(uint8_t *buf, unsigned short len)
{
    int actual_len;

    if ((actual_len = smb2_pread(smb, fh, buf, len, offset)) != len)
    {
        fserror_to_error();
        return true;
    }

    offset += actual_len;

    return false;
}

bool NetworkProtocolSMB::read_dir_entry(char *buf, unsigned short len)
{
    ent = smb2_readdir(smb, smb_dir);

    if (ent == nullptr)
    {
        error = NETWORK_ERROR_END_OF_FILE;
        return true;
    }

    // Set filename to buffer
    strcpy(buf, ent->name);

    // Get file size/type
    fileSize = ent->st.smb2_size;
    is_directory = ent->st.smb2_type == SMB2_TYPE_DIRECTORY;

    return false;
}

bool NetworkProtocolSMB::close_file_handle()
{
    smb2_close(smb, fh);
    return false;
}

bool NetworkProtocolSMB::close_dir_handle()
{
    smb2_closedir(smb, smb_dir);
    return false;
}

bool NetworkProtocolSMB::write_file_handle(uint8_t *buf, unsigned short len)
{
    int actual_len;

    if ((actual_len = smb2_pwrite(smb, fh, buf, len, offset)) != len)
    {
        fserror_to_error();
        return true;
    }

    offset += actual_len;

    return false;
}

uint8_t NetworkProtocolSMB::special_inquiry(uint8_t cmd)
{
    return 0xff;
}

bool NetworkProtocolSMB::special_00(cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolSMB::special_40(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolSMB::special_80(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolSMB::rename(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolSMB::del(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolSMB::mkdir(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolSMB::rmdir(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolSMB::stat()
{
    struct smb2_stat_64 st;

    smb2_stat(smb,smb_url->path,&st);

    fileSize = st.smb2_size;
    return false;
}

bool NetworkProtocolSMB::lock(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolSMB::unlock(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return false;
}