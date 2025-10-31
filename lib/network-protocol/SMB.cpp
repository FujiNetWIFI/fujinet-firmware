/**
 * NetworkProtocolSMB
 *
 * Implementation
 */

#include "SMB.h"

#include <fcntl.h>

#include <cstring>

#include "../../include/debug.h"

#include <smb2/libsmb2.h>
#include <smb2/smb2.h>
//#include <smb2/libsmb2-raw.h>

#include "status_error_codes.h"
#include "utils.h"

#include <vector>

NetworkProtocolSMB::NetworkProtocolSMB(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf)
    : NetworkProtocolFS(rx_buf, tx_buf, sp_buf)
{
    rename_implemented = true;
    delete_implemented = true;
    mkdir_implemented = true;
    rmdir_implemented = true;
    Debug_printf("NetworkProtocolSMB::ctor\r\n");
    smb = smb2_init_context();
}

NetworkProtocolSMB::~NetworkProtocolSMB()
{
    Debug_printf("NetworkProtocolSMB::dtor\r\n");
    smb2_destroy_context(smb);
}

bool NetworkProtocolSMB::open_file_handle()
{
    if (smb == nullptr)
    {
        Debug_printf("NetworkProtocolSMB::open_file_handle() - no smb context. aborting.\r\n");
        return true;
    }

    // Determine flags
    int flags = 0;

#ifdef OBSOLETE
    switch (aux1_open)
    {
    case PROTOCOL_OPEN_READ:
        flags = O_RDONLY;
        break;
    case PROTOCOL_OPEN_WRITE:
        flags = O_WRONLY | O_CREAT;
        break;
    case PROTOCOL_OPEN_APPEND:
        flags = O_APPEND | O_CREAT;
        break;
    case PROTOCOL_OPEN_READWRITE:
        flags = O_RDWR;
        break;
    default:
        Debug_printf("NetworkProtocolSMB::open_file_handle() - Uncaught aux1 %d", aux1_open);
    }
#endif /* OBSOLETE */

    fh = smb2_open(smb, smb_url->path, flags);

    if (fh == nullptr)
    {
        Debug_printf("NetworkProtocolSMB::open_file_handle() - SMB Error %s\r\n", smb2_get_error(smb));
        fserror_to_error();
        return true;
    }

    offset = 0;

    Debug_printf("DO WE FUCKING GET HERE?!\r\n");

    return false;
}

bool NetworkProtocolSMB::open_dir_handle()
{
    if ((smb_dir = smb2_opendir(smb, smb_url->path)) == nullptr)
    {
        Debug_printf("NetworkProtocolSMB::open_dir_handle() - ERROR: %s\r\n", smb2_get_error(smb));
        fserror_to_error();
        return true;
    }

    return false;
}

bool NetworkProtocolSMB::mount(PeoplesUrlParser *url)
{
    std::string openURL = url->url;

    // use mRawURL to bypass our normal URL processing.
    if (openURL.find("SMB:") != std::string::npos)
    {
        openURL[0] = 's';
        openURL[1] = 'm';
        openURL[2] = 'b';
    }

#if 0
    if (aux1_open == 6) // temporary
        openURL = openURL.substr(0, openURL.find_last_of("/"));
#endif

    Debug_printf("NetworkProtocolSMB::mount() - openURL: %s\r\n", openURL.c_str());
    smb_url = smb2_parse_url(smb, openURL.c_str());
    if (smb_url == nullptr)
    {
        Debug_printf("aNetworkProtocolSMB::mount(%s) - failed to parse URL, SMB2 error: %s\n", openURL.c_str(), smb2_get_error(smb));
        fserror_to_error();
        return true;
    }

    smb2_set_security_mode(smb, SMB2_NEGOTIATE_SIGNING_ENABLED);

    if (login != nullptr)
    {
        smb2_set_user(smb, login->c_str());
        smb2_set_password(smb, password->c_str());

        if ((smb_error = smb2_connect_share(smb, smb_url->server, smb_url->share, login->c_str())) != 0)
        {
            Debug_printf("aNetworkProtocolSMB::mount(%s) - could not mount, SMB2 error: %s\r\n", openURL.c_str(), smb2_get_error(smb));
            fserror_to_error();
            return true;
        }
    }
    else // no u/p
    {
        if ((smb_error = smb2_connect_share(smb, smb_url->server, smb_url->share, smb_url->user)) != 0)
        {
            Debug_printf("aNetworkProtocolSMB::mount(%s) - could not mount, SMB2 error: %s\r\n", openURL.c_str(), smb2_get_error(smb));
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

FujiDirection NetworkProtocolSMB::special_inquiry(uint8_t cmd)
{
    return DIRECTION_INVALID;
}

#ifdef OBSOLETE
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
#endif /* OBSOLETE */

bool NetworkProtocolSMB::rename(PeoplesUrlParser *url)
{
    return false;
}

bool NetworkProtocolSMB::del(PeoplesUrlParser *url)
{
    return false;
}

bool NetworkProtocolSMB::mkdir(PeoplesUrlParser *url)
{
    mount(url);

    if (smb2_mkdir(smb, smb_url->path) != 0)
    {
        fserror_to_error();
        Debug_printf("NetworkProtocolSMB::mkdir(%s) SMB error: %s\r\n",url->url.c_str(), smb2_get_error(smb));
    }

    umount();

    return false;
}

bool NetworkProtocolSMB::rmdir(PeoplesUrlParser *url)
{
    mount(url);

    if (smb2_rmdir(smb, smb_url->path) != 0)
    {
        fserror_to_error();
        Debug_printf("NetworkProtocolSMB::rmdir(%s) SMB error: %s\r\n",url->url.c_str(), smb2_get_error(smb));
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

bool NetworkProtocolSMB::lock(PeoplesUrlParser *url)
{
    return false;
}

bool NetworkProtocolSMB::unlock(PeoplesUrlParser *url)
{
    return false;
}

off_t NetworkProtocolSMB::seek(off_t position, int whence)
{
    // fileSize isn't fileSize, it's bytes remaining. Call stat() to fix fileSize
    stat();

    if (whence == SEEK_SET)
        offset = position;
    else if (whence == SEEK_CUR)
        offset += position;
    else if (whence == SEEK_END)
        offset = fileSize - position;

    fileSize -= offset;
    receiveBuffer->clear();

    return offset;
}
