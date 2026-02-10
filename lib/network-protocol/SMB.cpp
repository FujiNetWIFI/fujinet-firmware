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
#include <algorithm>

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

netProtoErr_t NetworkProtocolSMB::open_file_handle()
{
    if (smb == nullptr)
    {
        Debug_printf("NetworkProtocolSMB::open_file_handle() - no smb context. aborting.\r\n");
        return NETPROTO_ERR_UNSPECIFIED;
    }

    // Determine flags
    int flags = 0;

    switch (aux1_open)
    {
    case NETPROTO_OPEN_READ:
        flags = O_RDONLY;
        break;
    case NETPROTO_OPEN_WRITE:
        flags = O_WRONLY | O_CREAT;
        break;
    case NETPROTO_OPEN_APPEND:
        flags = O_APPEND | O_CREAT;
        break;
    case NETPROTO_OPEN_READWRITE:
        flags = O_RDWR;
        break;
    default:
        Debug_printf("NetworkProtocolSMB::open_file_handle() - Uncaught aux1 %d", aux1_open);
    }

    fh = smb2_open(smb, smb_url->path, flags);

    if (fh == nullptr)
    {
        Debug_printf("NetworkProtocolSMB::open_file_handle() - SMB Error %s\r\n", smb2_get_error(smb));
        fserror_to_error();
        return NETPROTO_ERR_UNSPECIFIED;
    }

    offset = 0;

    Debug_printf("DO WE FUCKING GET HERE?!\r\n");

    return NETPROTO_ERR_NONE;
}

netProtoErr_t NetworkProtocolSMB::open_dir_handle()
{
    if ((smb_dir = smb2_opendir(smb, smb_url->path)) == nullptr)
    {
        Debug_printf("NetworkProtocolSMB::open_dir_handle() - ERROR: %s\r\n", smb2_get_error(smb));
        fserror_to_error();
        return NETPROTO_ERR_UNSPECIFIED;
    }

    return NETPROTO_ERR_NONE;
}

std::string lowercase_if_no_lowercase(std::string s)
{
    if (!std::any_of(s.begin(), s.end(),
                     [](unsigned char c) { return std::islower(c); }))
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return std::tolower(c); });
    return s;
}

netProtoErr_t NetworkProtocolSMB::mount(PeoplesUrlParser *url)
{
    std::string openURL = url->url;

    // use mRawURL to bypass our normal URL processing.
    if (openURL.find("SMB:") != std::string::npos)
    {
        openURL[0] = 's';
        openURL[1] = 'm';
        openURL[2] = 'b';
    }

    if (aux1_open == NETPROTO_OPEN_DIRECTORY)
    {
        // When doing a directory listing the Atari DIR command sends
        // the directory path followed by `/<glob>` (usually "*.*")
        // which needs to be removed.
        std::size_t pos = openURL.find_last_of("/");
        if (pos < std::string::npos)
        {
            std::string lastComponent = openURL.substr(pos + 1);
            if (lastComponent.find('*') != std::string::npos ||
                lastComponent.find('?') != std::string::npos)
                openURL = openURL.substr(0, pos);
        }
    }

    Debug_printf("NetworkProtocolSMB::mount() - openURL: %s\r\n", openURL.c_str());
    smb_url = smb2_parse_url(smb, openURL.c_str());
    if (smb_url == nullptr)
    {
        Debug_printf("aNetworkProtocolSMB::mount(%s) - failed to parse URL, SMB2 error: %s\n", openURL.c_str(), smb2_get_error(smb));
        fserror_to_error();
        return NETPROTO_ERR_UNSPECIFIED;
    }

    smb2_set_security_mode(smb, SMB2_NEGOTIATE_SIGNING_ENABLED);

    if (smb_url->user || login != nullptr)
    {
        std::string user, pass;

        if (smb_url->user)
        {
            std::string_view up(smb_url->user);
            auto pos = up.find(':');
            user.assign(up.substr(0, pos));
            if (pos != std::string_view::npos)
                pass.assign(up.substr(pos + 1));

            user = lowercase_if_no_lowercase(user);
            pass = lowercase_if_no_lowercase(pass);
        }
        else
        {
            user = *login;
            pass = *password;
        }

        smb2_set_user(smb, user.c_str());
        smb2_set_password(smb, pass.c_str());

        if ((smb_error = smb2_connect_share(smb, smb_url->server, smb_url->share, user.c_str())) != 0)
        {
            Debug_printf("aNetworkProtocolSMB::mount(%s) - could not mount, SMB2 error: %s\r\n", openURL.c_str(), smb2_get_error(smb));
            fserror_to_error();
            return NETPROTO_ERR_UNSPECIFIED;
        }
    }
    else // no u/p
    {
        if ((smb_error = smb2_connect_share(smb, smb_url->server, smb_url->share, smb_url->user)) != 0)
        {
            Debug_printf("aNetworkProtocolSMB::mount(%s) - could not mount, SMB2 error: %s\r\n", openURL.c_str(), smb2_get_error(smb));
            fserror_to_error();
            return NETPROTO_ERR_UNSPECIFIED;
        }
    }

    return NETPROTO_ERR_NONE;
}

netProtoErr_t NetworkProtocolSMB::umount()
{
    if (smb == nullptr)
        return NETPROTO_ERR_UNSPECIFIED;

    smb2_disconnect_share(smb);

    if (smb_url == nullptr)
        return NETPROTO_ERR_UNSPECIFIED;

    smb2_destroy_url(smb_url);
    return NETPROTO_ERR_NONE;
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

netProtoErr_t NetworkProtocolSMB::read_file_handle(uint8_t *buf, unsigned short len)
{
    int actual_len;

    if ((actual_len = smb2_pread(smb, fh, buf, len, offset)) != len)
    {
        fserror_to_error();
        return NETPROTO_ERR_UNSPECIFIED;
    }

    offset += actual_len;

    return NETPROTO_ERR_NONE;
}

netProtoErr_t NetworkProtocolSMB::read_dir_entry(char *buf, unsigned short len)
{
    ent = smb2_readdir(smb, smb_dir);

    if (ent == nullptr)
    {
        error = NETWORK_ERROR_END_OF_FILE;
        return NETPROTO_ERR_UNSPECIFIED;
    }

    // Set filename to buffer
    strcpy(buf, ent->name);

    // Get file size/type
    fileSize = ent->st.smb2_size;
    is_directory = ent->st.smb2_type == SMB2_TYPE_DIRECTORY;

    return NETPROTO_ERR_NONE;
}

netProtoErr_t NetworkProtocolSMB::close_file_handle()
{
    smb2_close(smb, fh);
    return NETPROTO_ERR_NONE;
}

netProtoErr_t NetworkProtocolSMB::close_dir_handle()
{
    smb2_closedir(smb, smb_dir);
    return NETPROTO_ERR_NONE;
}

netProtoErr_t NetworkProtocolSMB::write_file_handle(uint8_t *buf, unsigned short len)
{
    int actual_len;

    if ((actual_len = smb2_pwrite(smb, fh, buf, len, offset)) != len)
    {
        fserror_to_error();
        return NETPROTO_ERR_UNSPECIFIED;
    }

    offset += actual_len;

    return NETPROTO_ERR_NONE;
}

AtariSIODirection NetworkProtocolSMB::special_inquiry(fujiCommandID_t cmd)
{
    return SIO_DIRECTION_INVALID;
}

netProtoErr_t NetworkProtocolSMB::special_00(cmdFrame_t *cmdFrame)
{
    return NETPROTO_ERR_NONE;
}

netProtoErr_t NetworkProtocolSMB::special_40(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    return NETPROTO_ERR_NONE;
}

netProtoErr_t NetworkProtocolSMB::special_80(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    return NETPROTO_ERR_NONE;
}

netProtoErr_t NetworkProtocolSMB::rename(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    return NETPROTO_ERR_NONE;
}

netProtoErr_t NetworkProtocolSMB::del(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    return NETPROTO_ERR_NONE;
}

netProtoErr_t NetworkProtocolSMB::mkdir(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    mount(url);

    if (smb2_mkdir(smb, smb_url->path) != 0)
    {
        fserror_to_error();
        Debug_printf("NetworkProtocolSMB::mkdir(%s) SMB error: %s\r\n",url->url.c_str(), smb2_get_error(smb));
    }

    umount();

    return NETPROTO_ERR_NONE;
}

netProtoErr_t NetworkProtocolSMB::rmdir(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    mount(url);

    if (smb2_rmdir(smb, smb_url->path) != 0)
    {
        fserror_to_error();
        Debug_printf("NetworkProtocolSMB::rmdir(%s) SMB error: %s\r\n",url->url.c_str(), smb2_get_error(smb));
    }

    umount();

    return NETPROTO_ERR_NONE;
}

netProtoErr_t NetworkProtocolSMB::stat()
{
    struct smb2_stat_64 st;

    int ret = smb2_stat(smb, smb_url->path, &st);

    fileSize = st.smb2_size;
    return ret != 0 ? NETPROTO_ERR_UNSPECIFIED : NETPROTO_ERR_NONE;
}

netProtoErr_t NetworkProtocolSMB::lock(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    return NETPROTO_ERR_NONE;
}

netProtoErr_t NetworkProtocolSMB::unlock(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    return NETPROTO_ERR_NONE;
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
