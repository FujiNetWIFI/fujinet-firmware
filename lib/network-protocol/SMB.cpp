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
        flags = O_RDONLY;
        break;
    case 8:
        flags = O_WRONLY | O_CREAT;
        break;
    case 9:
        flags = O_APPEND | O_CREAT;
        break;
    case 12:
        flags = O_RDWR;
        break;
    default:
        Debug_printf("NetworkProtocolSMB::open_file_handle() - Uncaught aux1 %d", aux1_open);
    }

    fh = smb2_open(smb, smb_url->path, flags);

    if (fh == nullptr)
    {
        Debug_printf("NetworkProtocolSMB::open_file_handle() - SMB Error %s\n", smb2_get_error(smb));
        fserror_to_error();
        return true;
    }

    offset = 0;

    Debug_printf("DO WE FUCKING GET HERE?!\n");

    return false;
}

bool NetworkProtocolSMB::open_dir_handle()
{
    if ((smb_dir = smb2_opendir(smb, smb_url->path)) == nullptr)
    {
        Debug_printf("NetworkProtocolSMB::open_dir_handle() - ERROR: %s\n", smb2_get_error(smb));
        fserror_to_error();
        return true;
    }

    return false;
}

bool NetworkProtocolSMB::mount(EdUrlParser *url)
{
    string openURL = url->mRawUrl;

    // use mRawURL to bypass our normal URL processing.
    if (openURL.find("SMB:") != string::npos)
    {
        openURL[0] = 's';
        openURL[1] = 'm';
        openURL[2] = 'b';
    }

    if (aux1_open == 6) // temporary
        openURL = openURL.substr(0, openURL.find_last_of("/"));

    Debug_printf("NetworkProtocolSMB::mount() - openURL: %s\n", openURL.c_str());
    smb_url = smb2_parse_url(smb, openURL.c_str());

    smb2_set_security_mode(smb, SMB2_NEGOTIATE_SIGNING_ENABLED);

    if (login != nullptr)
    {
        smb2_set_user(smb, login->c_str());
        smb2_set_password(smb, password->c_str());

        if ((smb_error = smb2_connect_share(smb, smb_url->server, smb_url->share, login->c_str())) != 0)
        {
            Debug_printf("aNetworkProtocolSMB::mount(%s) - could not mount, SMB2 error: %s\n", openURL.c_str(), smb2_get_error(smb));
            fserror_to_error();
            return true;
        }
    }
    else // no u/p
    {
        if ((smb_error = smb2_connect_share(smb, smb_url->server, smb_url->share, smb_url->user)) != 0)
        {
            Debug_printf("aNetworkProtocolSMB::mount(%s) - could not mount, SMB2 error: %s\n", openURL.c_str(), smb2_get_error(smb));
            fserror_to_error();
            return true;
        }
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
    mount(url);

    if (smb2_mkdir(smb, smb_url->path) != 0)
    {
        fserror_to_error();
        Debug_printf("NetworkProtocolSMB::mkdir(%s) SMB error: %s\n",url->mRawUrl.c_str(), smb2_get_error(smb));
    }

    umount();

    return false;
}

bool NetworkProtocolSMB::rmdir(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    mount(url);

    if (smb2_rmdir(smb, smb_url->path) != 0)
    {
        fserror_to_error();
        Debug_printf("NetworkProtocolSMB::rmdir(%s) SMB error: %s\n",url->mRawUrl.c_str(), smb2_get_error(smb));
    }

    umount();

    return false;
}

bool NetworkProtocolSMB::stat()
{
    struct smb2_stat_64 st;

    int ret = smb2_stat(smb, smb_url->path, &st);

    fileSize = st.smb2_size;
    return ret != 0;
}

bool NetworkProtocolSMB::lock(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolSMB::unlock(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return false;
}
